#include "eldenanimation.hpp"
#include "eldenanatomy.hpp"
#include "eldengrip.hpp"

#include <components/eldencombat/animations.hpp>
#include <components/nifosg/matrixtransform.hpp>
#include <components/sceneutil/nodecallback.hpp>

#include <osg/Matrix>
#include <osg/NodeVisitor>
#include <osg/Drawable>
#include <osg/Geometry>
#include <osg/TriangleFunctor>
#include <osg/observer_ptr>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace MWRender
{
    namespace
    {
        enum Joint : std::size_t
        {
            Pelvis, Spine, Spine1, Spine2, Neck, Head,
            LClavicle, LUpperArm, LForearm, LHand,
            RClavicle, RUpperArm, RForearm, RHand,
            LThigh, LCalf, LFoot, LToe,
            RThigh, RCalf, RFoot, RToe,
            RWeapon, LWeapon,
            LFinger0, LFinger01, LFinger02, LFinger1, LFinger2, LFinger21, LFinger22,
            RFinger0, RFinger01, RFinger02, RFinger1, RFinger2, RFinger21, RFinger22,
            JointCount
        };

        constexpr std::array<std::string_view, JointCount> sJointNames = {
            "Pelvis", "Spine", "Spine1", "Spine2", "Neck", "Head",
            "L_Clavicle", "L_UpperArm", "L_Forearm", "L_Hand",
            "R_Clavicle", "R_UpperArm", "R_Forearm", "R_Hand",
            "L_Thigh", "L_Calf", "L_Foot", "L_Toe0",
            "R_Thigh", "R_Calf", "R_Foot", "R_Toe0", "R_Weapon", "L_Weapon",
            "L_Finger0", "L_Finger01", "L_Finger02", "L_Finger1", "L_Finger2", "L_Finger21", "L_Finger22",
            "R_Finger0", "R_Finger01", "R_Finger02", "R_Finger1", "R_Finger2", "R_Finger21", "R_Finger22",
        };

        constexpr std::size_t sNoJoint = JointCount;
        constexpr std::size_t sNoParent = std::numeric_limits<std::size_t>::max();
        using JointPositions = std::array<osg::Vec3f, JointCount>;

        osg::Vec3f unit(osg::Vec3f value, const osg::Vec3f& fallback)
        {
            if (value.length2() < 1e-10f)
                return fallback;
            value.normalize();
            return value;
        }

        osg::Quat frameRotation(osg::Vec3f primary, osg::Vec3f secondary)
        {
            primary = unit(primary, osg::Vec3f(0, 0, 1));
            secondary -= primary * (secondary * primary);
            if (secondary.length2() < 1e-10f)
            {
                secondary = std::abs(primary.z()) < 0.8f ? osg::Vec3f(0, 0, 1) : osg::Vec3f(0, 1, 0);
                secondary -= primary * (secondary * primary);
            }
            secondary.normalize();
            const osg::Vec3f third = primary ^ secondary;
            // OSG matrices use row vectors. Rebuild the third axis: the ERAN
            // bake reflects the original game basis, which is not a rotation.
            return osg::Matrixf(primary.x(), primary.y(), primary.z(), 0,
                secondary.x(), secondary.y(), secondary.z(), 0,
                third.x(), third.y(), third.z(), 0, 0, 0, 0, 1).getRotate();
        }

        osg::Vec3f fromSource(const std::vector<float>& values, std::size_t offset)
        {
            // Recreation: +X left, +Y up, +Z forward. OpenMW: +X right,
            // +Y forward, +Z up. Positions and axes must use the same transform.
            return osg::Vec3f(-values[offset], values[offset + 2], values[offset + 1]);
        }

        struct AnatomicalFrame
        {
            osg::Vec3f mUp;
            osg::Vec3f mRight;
            osg::Vec3f mForward;
            osg::Vec3f mShoulderRight;
            osg::Vec3f mChestForward;
        };

        AnatomicalFrame anatomy(const JointPositions& joints)
        {
            const osg::Vec3f up = unit(joints[Neck] - joints[Pelvis], osg::Vec3f(0, 0, 1));
            const osg::Vec3f right = unit(joints[RThigh] - joints[LThigh], osg::Vec3f(1, 0, 0));
            const osg::Vec3f shoulders = unit(joints[RUpperArm] - joints[LUpperArm], right);
            return { up, right, unit(up ^ right, osg::Vec3f(0, 1, 0)), shoulders,
                unit(up ^ shoulders, osg::Vec3f(0, 1, 0)) };
        }

        osg::Quat jointFrame(std::size_t joint, const JointPositions& p, const AnatomicalFrame& body,
            const osg::Vec3f& headUp, const osg::Vec3f& headForward,
            const std::array<osg::Vec3f, 10>* limbPlanes = nullptr)
        {
            osg::Vec3f primary = body.mUp;
            osg::Vec3f secondary = body.mRight;
            if (joint <= Neck)
            {
                primary = p[joint + 1] - p[joint];
                // Positions do not encode twist along the central spine.
                // The hip and shoulder lines do: distribute their relative
                // turn over the torso instead of locking every spine bone
                // to the pelvis during a winding sword strike.
                const float chest = std::min(float(joint) / float(Spine2), 1.f);
                secondary = body.mRight * (1.f - chest) + body.mShoulderRight * chest;
            }
            else if (joint == Head)
            {
                primary = headUp;
                secondary = headForward;
            }
            else if (joint >= LClavicle && joint <= RHand)
            {
                const std::size_t arm = joint < RClavicle ? LClavicle : RClavicle;
                const std::size_t finger = arm == LClavicle ? LFinger0 : RFinger0;
                if (joint == arm)
                {
                    primary = p[arm + 1] - p[arm];
                    secondary = body.mUp;
                }
                else if (joint == arm + 3)
                {
                    primary = p[finger + 4] - p[joint];
                    secondary = p[finger + 3] - p[finger + 4];
                }
                else
                {
                    primary = p[joint + 1] - p[joint];
                    secondary = limbPlanes ? (*limbPlanes)[(arm == LClavicle ? 0 : 2) + joint - arm - 1]
                        : EldenPose::bendSecondary(p[arm + 2] - p[arm + 1], p[arm + 3] - p[arm + 2],
                            primary, primary ^ body.mChestForward);
                }
            }
            else if (joint >= LThigh && joint <= RToe)
            {
                const std::size_t leg = joint < RThigh ? LThigh : RThigh;
                if (joint >= leg + 2)
                {
                    primary = p[leg + 3] - p[leg + 2];
                    secondary = limbPlanes ? (*limbPlanes)[leg == LThigh ? 8 : 9]
                        : EldenPose::footSecondary(primary, body.mRight);
                }
                else
                {
                    primary = p[joint + 1] - p[joint];
                    secondary = limbPlanes ? (*limbPlanes)[(leg == LThigh ? 4 : 6) + joint - leg]
                        : EldenPose::kneeSecondary(p[leg + 1] - p[leg], p[leg + 2] - p[leg + 1],
                            primary, body.mRight);
                }
            }
            else if (joint >= LFinger0)
            {
                const std::size_t first = joint < RFinger0 ? LFinger0 : RFinger0;
                const std::size_t digit = joint - first;
                if (digit == 2 || digit == 6)
                    primary = p[joint] - p[joint - 1];
                else if (digit == 3)
                    primary = p[first + 5] - p[first + 4];
                else
                    primary = p[joint + 1] - p[joint];
                secondary = p[first + 3] - p[first + 4];
            }
            return frameRotation(primary, secondary);
        }

        struct LocalPose
        {
            osg::Quat mRotation;
            osg::Vec3f mTranslation;
        };

        void applyPose(osg::MatrixTransform& node, const LocalPose& pose, float scale)
        {
            if (auto* nif = dynamic_cast<NifOsg::MatrixTransform*>(&node))
            {
                nif->setScale(scale);
                nif->setRotation(pose.mRotation);
                nif->setTranslation(pose.mTranslation);
            }
            else
            {
                osg::Matrixf matrix = osg::Matrixf::scale(scale, scale, scale) * osg::Matrixf::rotate(pose.mRotation);
                matrix.setTrans(pose.mTranslation);
                node.setMatrix(matrix);
            }
        }

        struct GripTriangles
        {
            osg::Matrixf mTransform;
            std::vector<EldenPose::GripTriangle>* mTriangles = nullptr;

            void operator()(const osg::Vec3f& a, const osg::Vec3f& b, const osg::Vec3f& c)
            {
                mTriangles->push_back({ a * mTransform, b * mTransform, c * mTransform });
            }
        };

        class GripGeometryVisitor : public osg::NodeVisitor
        {
        public:
            std::vector<EldenPose::GripTriangle> mTriangles;

            GripGeometryVisitor() : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN) {}

            void apply(osg::Drawable& node) override
            {
                osg::TriangleFunctor<GripTriangles> triangles;
                triangles.mTransform = osg::computeLocalToWorld(getNodePath());
                triangles.mTriangles = &mTriangles;
                node.accept(triangles);
            }

            void apply(osg::Geometry& node) override { apply(static_cast<osg::Drawable&>(node)); }
        };
    }

    struct EldenAnimation::Impl
    {
        struct Bone
        {
            osg::ref_ptr<osg::MatrixTransform> mNode;
            std::size_t mParent = sNoParent;
            std::size_t mJoint = sNoJoint;
            LocalPose mRest;
            float mScale = 1.f;
            osg::Matrixf mRestMatrix;
            osg::Matrixf mRestModel;
            osg::Quat mRestModelRotation;
            osg::Quat mCorrection;
            LocalPose mShown;
            LocalPose mFrom;
            osg::Quat mFromModelRotation;
        };

        struct Callback : SceneUtil::NodeCallback<Callback, osg::MatrixTransform*>
        {
            std::weak_ptr<Impl> mState;
            std::size_t mIndex = 0;

            Callback() = default;
            Callback(const Callback& copy, const osg::CopyOp& op)
                : SceneUtil::NodeCallback<Callback, osg::MatrixTransform*>(copy, op)
                , mState(copy.mState)
                , mIndex(copy.mIndex)
            {
            }
            META_Object(MWRender, Callback)

            void operator()(osg::MatrixTransform* node, osg::NodeVisitor* visitor)
            {
                if (const auto state = mState.lock(); state && state->mEnabled)
                {
                    const Bone& bone = state->mBones[mIndex];
                    if (bone.mJoint == sNoJoint && !dynamic_cast<NifOsg::MatrixTransform*>(node))
                        node->setMatrix(bone.mRestMatrix);
                    else
                        applyPose(*node, bone.mShown, bone.mScale);
                }
                traverse(node, visitor);
            }
        };

        struct WeaponCallback : SceneUtil::NodeCallback<WeaponCallback, NifOsg::MatrixTransform*>
        {
            std::weak_ptr<Impl> mState;
            bool mBeforeNative = false;

            WeaponCallback() = default;
            WeaponCallback(const WeaponCallback& copy, const osg::CopyOp& op)
                : SceneUtil::NodeCallback<WeaponCallback, NifOsg::MatrixTransform*>(copy, op)
                , mState(copy.mState)
                , mBeforeNative(copy.mBeforeNative)
            {
            }
            META_Object(MWRender, WeaponCallback)

            void operator()(NifOsg::MatrixTransform* node, osg::NodeVisitor* visitor)
            {
                if (const auto state = mState.lock())
                {
                    if (mBeforeNative)
                        state->restoreWeapon();
                    else if (state->mEnabled && state->mAlignSpear)
                    {
                        // Read the actual displayed parent after pose blending,
                        // not the unblended targets calculated by play().
                        osg::Vec3f translation, scale;
                        osg::Quat parent, scaleOrientation;
                        osg::computeLocalToWorld(state->mWeaponParents).decompose(
                            translation, parent, scale, scaleOrientation);
                        const osg::Quat facing(state->mFacing.shown(), osg::Vec3f(0, 0, 1));
                        state->mNativeWeaponRotation = node->mRotationScale.toOsgMatrix().getRotate();
                        if (!state->mHaveWeaponBlendFrom)
                        {
                            // The native attachment is captured after the body
                            // has turned, so remove its current displayed yaw.
                            state->mWeaponBlendFrom = state->mNativeWeaponRotation * parent * facing.inverse();
                            state->mHaveWeaponBlendFrom = true;
                        }
                        state->mShownWeaponRotation.slerp(
                            state->mWeaponBlend, state->mWeaponBlendFrom, state->mWeaponTargetRotation);
                        state->mShownWeaponRotation *= facing;
                        // Native spear meshes point towards local +Y, exactly
                        // like R_Weapon in the imported animations. Keep the
                        // native attachment's translation and scale intact.
                        node->setRotation(state->mShownWeaponRotation * parent.inverse());
                        state->mWeaponOverridden = true;
                        state->mHaveShownWeapon = true;
                    }
                }
                traverse(node, visitor);
            }
        };

        std::shared_ptr<const EldenCombat::AnimationBank> mBank;
        std::vector<Bone> mBones;
        std::array<std::size_t, JointCount> mOffsets{};
        std::optional<std::size_t> mHeadAxes;
        JointPositions mBindJoints{};
        std::array<bool, JointCount> mHaveBindJoint{};
        std::vector<float> mSample;
        std::vector<float> mLayer;
        std::vector<osg::Quat> mModelRotations;
        std::vector<osg::Matrixf> mModelMatrices;
        std::string mClip;
        std::string mLeftArmClip;
        std::string mRightArmClip;
        std::string mGripClip;
        float mFade = 0.f;
        float mFadeLength = 0.f;
        EldenPose::HeadingBlend mFacing;
        std::array<EldenPose::LimbBendPlane, 10> mLimbPlanes;
        float mFrame = 0.f;
        bool mLoop = false;
        float mLayerFrame = 0.f;
        float mSourcePelvisHeight = 0.f;
        float mScale = 1.f;
        bool mValid = false;
        bool mEnabled = false;

        osg::ref_ptr<NifOsg::MatrixTransform> mWeaponNode;
        osg::NodePath mWeaponParents;
        std::optional<std::size_t> mWeaponAxes;
        osg::Quat mNativeWeaponRotation;
        osg::Quat mWeaponTargetRotation;
        osg::Quat mShownWeaponRotation;
        osg::Quat mWeaponBlendFrom;
        float mWeaponBlend = 1.f;
        bool mAlignSpear = false;
        bool mWeaponOverridden = false;
        bool mHaveShownWeapon = false;
        bool mHaveWeaponBlendFrom = false;

        std::array<std::size_t, 4> mSwordArm{ sNoParent, sNoParent, sNoParent, sNoParent };
        osg::Vec3f mBindRight;
        float mSwordGripDistance = 0.f;
        float mSwordPalmHalfWidth = 0.f;
        float mSwordGeometryScale = 0.f;
        std::vector<osg::observer_ptr<osg::Node>> mSwordGeometry;
        std::optional<osg::Vec3f> mSwordContact;

        void closeSwordGrip(float blend)
        {
            if (!mWeaponNode || mSwordGripDistance <= 0.f || mSwordPalmHalfWidth <= 0.f
                || std::find(mSwordArm.begin(), mSwordArm.end(), sNoParent) != mSwordArm.end())
                return;
            const auto [upper, forearm, hand, rightHand] = mSwordArm;
            if (mBones[forearm].mParent != upper || mBones[hand].mParent != forearm
                || mWeaponParents.empty() || mWeaponParents.back() != mBones[rightHand].mNode.get())
                return;

            // Native ready attachments are constant. Read the last completed
            // native update, then solve before this frame's bone callbacks and
            // skinning cache run. The normal pose fade covers entry transitions.
            const osg::Matrixf attachment = mWeaponNode->getMatrix();
            const osg::Matrixf weaponModel = attachment * mModelMatrices[rightHand];
            const float scale = osg::Matrixf::transform3x3(osg::Vec3f(0, 1, 0), weaponModel).length();
            if (!std::isfinite(scale) || scale < 1e-5f)
                return;
            bool changed = std::abs(scale - mSwordGeometryScale) > 1e-4f * std::max(scale, mSwordGeometryScale)
                || mSwordGeometry.size() != mWeaponNode->getNumChildren();
            for (unsigned int i = 0; !changed && i < mWeaponNode->getNumChildren(); ++i)
                changed = mSwordGeometry[i].get() != mWeaponNode->getChild(i);
            if (changed)
            {
                // Inspect the equipped model in attachment space, once per
                // model/scale. This accommodates short and offset sword hilts.
                GripGeometryVisitor geometry;
                mSwordGeometry.clear();
                for (unsigned int i = 0; i < mWeaponNode->getNumChildren(); ++i)
                {
                    auto* child = mWeaponNode->getChild(i);
                    mSwordGeometry.emplace_back(child);
                    child->accept(geometry);
                }
                mSwordGeometryScale = scale;
                mSwordContact = EldenPose::findSwordSupportGrip(
                    geometry.mTriangles, -mSwordGripDistance / scale, mSwordPalmHalfWidth / scale);
            }
            if (!mSwordContact)
                return;

            // Mirror the native right-hand attachment into the left hand's
            // own bind frame. This locates the palm, rather than the wrist,
            // on the handle without assuming a particular race's bone axes.
            osg::Vec3f grip = osg::Matrixf::transform3x3(attachment.getTrans(), mBones[rightHand].mRestModel);
            grip -= mBindRight * (2.f * (grip * mBindRight));
            grip = osg::Matrixf::transform3x3(grip, osg::Matrixf::inverse(mBones[hand].mRestModel));
            const osg::Vec3f root = mModelMatrices[upper].getTrans();
            const osg::Vec3f elbow = mModelMatrices[forearm].getTrans();
            const osg::Vec3f wrist = mModelMatrices[hand].getTrans();
            const osg::Vec3f desired = *mSwordContact * weaponModel
                - osg::Matrixf::transform3x3(grip, mModelMatrices[hand]);
            const auto solved = EldenPose::solveTwoBone(root, elbow, wrist, wrist + (desired - wrist) * blend);
            osg::Quat upperTurn, forearmTurn;
            upperTurn.makeRotate(elbow - root, solved.mJoint - root);
            forearmTurn.makeRotate(wrist - elbow, solved.mEnd - solved.mJoint);
            const osg::Quat upperRotation = mModelRotations[upper] * upperTurn;
            const osg::Quat forearmRotation = mModelRotations[forearm] * forearmTurn;
            const osg::Quat parentRotation = mBones[upper].mParent == sNoParent
                ? osg::Quat() : mModelRotations[mBones[upper].mParent];
            mBones[upper].mShown.mRotation = upperRotation * parentRotation.inverse();
            mBones[forearm].mShown.mRotation = forearmRotation * upperRotation.inverse();
            mBones[hand].mShown.mRotation = mModelRotations[hand] * forearmRotation.inverse();
            // Translations, segment lengths and the hand's model orientation
            // stay intact. Finger locals therefore retain their imported curl.
        }

        void restoreWeapon()
        {
            if (mWeaponOverridden)
            {
                mWeaponNode->setRotation(mNativeWeaponRotation);
                mWeaponOverridden = false;
            }
        }

        void grip(std::string_view name, bool slidingGrip)
        {
            const auto* clip = mBank->find(name);
            if (!clip)
                return;
            mBank->sample(*clip, 0.f, false, mLayer);
            for (bool left : { true, false })
            {
                const std::size_t finger = left ? LFinger0 : RFinger0;
                const std::size_t weapon = left ? LWeapon : RWeapon;
                const auto axes = mBank->axesOffset(sJointNames[weapon]);
                if (!axes)
                    continue;
                // Spear attacks slide a hand along the shaft. The weapon pivot
                // can move over 30 cm from the wrist; moving knuckles around
                // that pivot stretches the hand and rotates its inferred frame.
                // Carry the stance's curl from the animated wrist instead.
                const std::size_t origin = mOffsets[slidingGrip ? (left ? LHand : RHand) : weapon];
                const auto vectorAt = [](const std::vector<float>& pose, std::size_t at) {
                    return osg::Vec3f(pose[at], pose[at + 1], pose[at + 2]);
                };
                const osg::Vec3f before = vectorAt(mLayer, origin);
                const osg::Vec3f after = vectorAt(mSample, origin);
                for (std::size_t i = 0; i != 7; ++i)
                {
                    const std::size_t offset = mOffsets[finger + i];
                    const osg::Vec3f delta = vectorAt(mLayer, offset) - before;
                    osg::Vec3f position = after;
                    for (std::size_t axis = 0; axis != 3; ++axis)
                    {
                        const std::size_t at = *axes + axis * 3;
                        position += vectorAt(mSample, at) * (delta * vectorAt(mLayer, at));
                    }
                    std::copy_n(position.ptr(), 3, mSample.begin() + offset);
                }
            }
        }

        void overlay(std::string_view name, bool left)
        {
            if (name.empty())
                return;
            const auto* clip = mBank->find(name);
            if (!clip)
                return;
            mBank->sample(*clip, mLayerFrame, true, mLayer);
            const std::size_t chest = mOffsets[Spine2];
            const std::size_t firstArm = left ? LClavicle : RClavicle;
            const std::size_t firstFinger = left ? LFinger0 : RFinger0;
            const std::size_t weapon = left ? LWeapon : RWeapon;
            const auto copyJoint = [&](std::size_t joint) {
                const std::size_t offset = mOffsets[joint];
                for (std::size_t axis = 0; axis != 3; ++axis)
                    mSample[offset + axis]
                        = mLayer[offset + axis] + mSample[chest + axis] - mLayer[chest + axis];
            };
            for (std::size_t i = 0; i != 4; ++i)
                copyJoint(firstArm + i);
            for (std::size_t i = 0; i != 7; ++i)
                copyJoint(firstFinger + i);
            copyJoint(weapon);
            if (const auto offset = mBank->axesOffset(sJointNames[weapon]))
                std::copy_n(mLayer.begin() + *offset, 9, mSample.begin() + *offset);
        }

        void restore()
        {
            restoreWeapon();
            mAlignSpear = false;
            mHaveShownWeapon = false;
            mHaveWeaponBlendFrom = false;
            mFacing.reset();
            if (!mEnabled)
                return;
            mEnabled = false;
            for (const Bone& bone : mBones)
            {
                if (bone.mJoint == sNoJoint)
                {
                    // Keep NIF's decomposed state in step with its matrix.
                    if (dynamic_cast<NifOsg::MatrixTransform*>(bone.mNode.get()))
                        applyPose(*bone.mNode, bone.mRest, bone.mScale);
                    else
                        bone.mNode->setMatrix(bone.mRestMatrix);
                }
                else
                    applyPose(*bone.mNode, bone.mRest, bone.mScale);
            }
            mClip.clear();
            mLeftArmClip.clear();
            mRightArmClip.clear();
            mGripClip.clear();
        }
    };

    EldenAnimation::EldenAnimation(const Animation::NodeMap& nodes, osg::Group& objectRoot,
        std::shared_ptr<const EldenCombat::AnimationBank> bank)
        : mImpl(std::make_shared<Impl>())
    {
        auto& state = *mImpl;
        state.mBank = std::move(bank);
        if (!state.mBank)
            return;
        for (std::size_t joint = 0; joint != JointCount; ++joint)
        {
            const auto offset = state.mBank->jointOffset(sJointNames[joint]);
            if (!offset)
                return;
            state.mOffsets[joint] = *offset;
        }
        state.mHeadAxes = state.mBank->axesOffset("Head");

        std::vector<std::pair<osg::MatrixTransform*, std::size_t>> mapped;
        const auto map = [&](std::size_t joint, std::initializer_list<std::string> names) {
            for (const std::string& name : names)
            {
                const auto found = nodes.find(name);
                if (found != nodes.end())
                {
                    mapped.emplace_back(found->second.get(), joint);
                    return;
                }
            }
        };
        map(Pelvis, { "bip01 pelvis" });
        map(Spine, { "bip01 spine" });
        map(Spine1, { "bip01 spine1" });
        map(Spine2, { "bip01 spine2" });
        map(Neck, { "bip01 neck" });
        map(Head, { "bip01 head" });
        for (bool left : { true, false })
        {
            const std::string prefix = left ? "bip01 l " : "bip01 r ";
            const std::size_t arm = left ? LClavicle : RClavicle;
            const std::size_t leg = left ? LThigh : RThigh;
            const std::size_t finger = left ? LFinger0 : RFinger0;
            map(arm, { prefix + "clavicle" });
            map(arm + 1, { prefix + "upperarm" });
            map(arm + 2, { prefix + "forearm" });
            map(arm + 3, { prefix + "hand" });
            map(leg, { prefix + "thigh" });
            map(leg + 1, { prefix + "calf" });
            map(leg + 2, { prefix + "foot" });
            map(leg + 3, { prefix + "toe0", prefix + "toe" });
            map(finger, { prefix + "finger0", prefix + "thumb1" });
            map(finger + 1, { prefix + "finger01", prefix + "thumb2" });
            map(finger + 2, { prefix + "finger02", prefix + "thumb3" });
            map(finger + 3, { prefix + "finger1", prefix + "pointer1" });
            map(finger + 4, { prefix + "finger2", prefix + "middle1" });
            map(finger + 5, { prefix + "finger21", prefix + "middle2" });
            map(finger + 6, { prefix + "finger22", prefix + "middle3" });
            // Stock Morrowind hands have a separate index-finger chain,
            // Finger1 -> Finger11, alongside the grouped Finger2 -> Finger21.
            // The bake has only the index knuckle; share the middle curl for
            // its remaining joints just as for replacement pointer bones.
            map(finger + 5, { prefix + "finger11" });
            map(finger + 6, { prefix + "finger12" });
            // The bake contains the middle finger's complete curl. Use it for
            // the remaining native fingers, whose individual joints were not
            // exported, while preserving each finger's own bind geometry.
            for (std::string_view digit : { "pointer", "ring", "pinky" })
            {
                if (digit != "pointer")
                    map(finger + 4, { prefix + std::string(digit) + "1" });
                map(finger + 5, { prefix + std::string(digit) + "2" });
                map(finger + 6, { prefix + std::string(digit) + "3" });
            }
        }
        if (mapped.size() < 8)
            return;

        std::unordered_map<osg::MatrixTransform*, std::size_t> index;
        for (const auto& [node, joint] : mapped)
        {
            // osgAnimation bones have a separate skeleton-space cache and
            // traversal protocol. Only NIF bones support this adapter.
            if (!dynamic_cast<NifOsg::MatrixTransform*>(node))
                return;
            const auto paths = node->getParentalNodePaths(&objectRoot);
            if (paths.empty())
                return;
            std::size_t parent = sNoParent;
            for (osg::Node* pathNode : paths.front())
            {
                auto* transform = dynamic_cast<osg::MatrixTransform*>(pathNode);
                if (!transform || pathNode == &objectRoot)
                    continue;
                auto found = index.find(transform);
                if (found == index.end())
                {
                    Impl::Bone bone;
                    bone.mNode = transform;
                    bone.mParent = parent;
                    bone.mRestMatrix = transform->getMatrix();
                    bone.mRest.mTranslation = bone.mRestMatrix.getTrans();
                    if (const auto* nif = dynamic_cast<NifOsg::MatrixTransform*>(transform))
                    {
                        bone.mScale = nif->mScale;
                        bone.mRest.mRotation = nif->mRotationScale.toOsgMatrix().getRotate();
                    }
                    else
                    {
                        osg::Vec3f scale;
                        osg::Quat scaleOrientation;
                        bone.mRestMatrix.decompose(
                            bone.mRest.mTranslation, bone.mRest.mRotation, scale, scaleOrientation);
                        bone.mScale = scale.x();
                    }
                    if (!std::isfinite(bone.mScale) || std::abs(bone.mScale) < 1e-6f)
                        return;
                    bone.mRestModel = bone.mRestMatrix;
                    bone.mRestModelRotation = bone.mRest.mRotation;
                    if (parent != sNoParent)
                    {
                        bone.mRestModel *= state.mBones[parent].mRestModel;
                        bone.mRestModelRotation *= state.mBones[parent].mRestModelRotation;
                    }
                    bone.mShown = bone.mFrom = bone.mRest;
                    found = index.emplace(transform, state.mBones.size()).first;
                    state.mBones.push_back(std::move(bone));
                }
                parent = found->second;
            }
            const auto found = index.find(node);
            if (found == index.end())
                return;
            Impl::Bone& bone = state.mBones[found->second];
            bone.mJoint = joint;
            if (!state.mHaveBindJoint[joint])
            {
                state.mBindJoints[joint] = bone.mRestModel.getTrans();
                state.mHaveBindJoint[joint] = true;
            }
        }
        if (!state.mHaveBindJoint[Pelvis] || !state.mHaveBindJoint[Neck])
            return;

        const auto nativeFingerDirection = [&](std::size_t index) -> osg::Vec3f {
            const Impl::Bone& bone = state.mBones[index];
            for (const Impl::Bone& child : state.mBones)
                if (child.mParent == index && child.mJoint >= LFinger0 && child.mJoint < JointCount)
                    return child.mRestModel.getTrans() - bone.mRestModel.getTrans();
            // A terminal native phalanx still has its own rest rotation.
            // Continue its chain's local bone axis through that rotation;
            // extrapolating the previous model-space segment discards its
            // existing knuckle bend, while a point near the wrist reverses it.
            return bone.mRestModelRotation * unit(bone.mRest.mTranslation, osg::Vec3f(1, 0, 0));
        };

        // A fully absent limb is normal on first-person rigs. A partially
        // present chain does not define the bind segment directions needed
        // for retargeting; retain that replacement rig's native animation
        // instead of calibrating its remaining bones against the origin.
        for (const std::array<std::size_t, 3>& chain : {
                 std::array<std::size_t, 3>{ LUpperArm, LForearm, LHand },
                 std::array<std::size_t, 3>{ RUpperArm, RForearm, RHand },
                 std::array<std::size_t, 3>{ LThigh, LCalf, LFoot },
                 std::array<std::size_t, 3>{ RThigh, RCalf, RFoot },
             })
        {
            const int present = int(state.mHaveBindJoint[chain[0]]) + int(state.mHaveBindJoint[chain[1]])
                + int(state.mHaveBindJoint[chain[2]]);
            if (present != 0 && present != 3)
                return;
        }

        // Some first-person and replacement NIFs omit intermediate torso,
        // finger or leg bones. These virtual joints only define orientations;
        // no absent bone or mesh is created or translated.
        const osg::Vec3f torso = state.mBindJoints[Neck] - state.mBindJoints[Pelvis];
        for (std::size_t joint = Spine; joint <= Spine2; ++joint)
            if (!state.mHaveBindJoint[joint])
                state.mBindJoints[joint] = state.mBindJoints[Pelvis] + torso * (float(joint) / 4.f);
        if (!state.mHaveBindJoint[Head])
            state.mBindJoints[Head] = state.mBindJoints[Neck] + unit(torso, osg::Vec3f(0, 0, 1));
        for (bool left : { true, false })
        {
            const std::size_t arm = left ? LClavicle : RClavicle;
            const std::size_t leg = left ? LThigh : RThigh;
            const std::size_t finger = left ? LFinger0 : RFinger0;
            const float side = left ? -1.f : 1.f;
            if (!state.mHaveBindJoint[leg])
                state.mBindJoints[leg] = state.mBindJoints[Pelvis] + osg::Vec3f(side, 0, 0);
            if (!state.mHaveBindJoint[arm])
                state.mBindJoints[arm] = state.mBindJoints[Spine2];
            const osg::Vec3f hand = state.mBindJoints[arm + 3];
            const osg::Vec3f along = unit(hand - state.mBindJoints[arm + 2], osg::Vec3f(side, 0, 0));
            for (std::size_t j = 0; j != 7; ++j)
                if (!state.mHaveBindJoint[finger + j])
                    state.mBindJoints[finger + j]
                        = hand + along * (1.f + float(j % 3)) + osg::Vec3f(0, j < 3 ? -0.5f : 0.5f * float(j - 3), 0);
            for (std::size_t tip : { finger + 2, finger + 6 })
                if (!state.mHaveBindJoint[tip] && state.mHaveBindJoint[tip - 1])
                    for (std::size_t i = 0; i != state.mBones.size(); ++i)
                        if (state.mBones[i].mJoint == tip - 1)
                        {
                            state.mBindJoints[tip] = state.mBindJoints[tip - 1] + nativeFingerDirection(i);
                            break;
                        }
            if (!state.mHaveBindJoint[leg + 3])
                state.mBindJoints[leg + 3] = state.mBindJoints[leg + 2] + osg::Vec3f(0, 1, 0);
        }
        const AnatomicalFrame bindBody = anatomy(state.mBindJoints);
        state.mBindRight = bindBody.mRight;
        for (std::size_t i = 0; i != state.mBones.size(); ++i)
        {
            Impl::Bone& bone = state.mBones[i];
            if (bone.mJoint != sNoJoint)
            {
                if (bone.mJoint >= LFinger0)
                {
                    const std::size_t first = bone.mJoint < RFinger0 ? LFinger0 : RFinger0;
                    // Several native digits share the baked middle-finger
                    // curl. Calibrate each one's actual bind direction so its
                    // own rest bend/spread does not turn that curl outwards.
                    bone.mCorrection = bone.mRestModelRotation
                        * frameRotation(nativeFingerDirection(i),
                            state.mBindJoints[first + 3] - state.mBindJoints[first + 4]).inverse();
                    continue;
                }
                bone.mCorrection = bone.mRestModelRotation
                    * jointFrame(bone.mJoint, state.mBindJoints, bindBody, bindBody.mUp, bindBody.mChestForward).inverse();
            }
        }

        // Establish vertical scale from leg lengths, never from an animated
        // arm or from the absolute location of the model's origin.
        const auto* standing = state.mBank->find("a000_000000");
        if (!standing)
            standing = state.mBank->find("a000_020000");
        if (!standing)
            return;
        state.mBank->sample(*standing, 0.f, false, state.mSample);
        JointPositions reference;
        for (std::size_t j = 0; j != JointCount; ++j)
            reference[j] = fromSource(state.mSample, state.mOffsets[j]);
        const AnatomicalFrame referenceBody = anatomy(reference);
        for (Impl::Bone& bone : state.mBones)
            if (bone.mJoint == LFoot || bone.mJoint == LToe || bone.mJoint == RFoot || bone.mJoint == RToe)
            {
                const std::size_t foot = bone.mJoint == LFoot || bone.mJoint == LToe ? LFoot : RFoot;
                bone.mCorrection = EldenPose::footCorrection(bone.mRestModelRotation,
                    state.mBindJoints[foot + 1] - state.mBindJoints[foot],
                    jointFrame(bone.mJoint, reference, referenceBody, referenceBody.mUp, referenceBody.mChestForward));
            }
        const float sourceLeg = (reference[LThigh] - reference[LCalf]).length()
            + (reference[LCalf] - reference[LFoot]).length();
        const float targetLeg = (state.mBindJoints[LThigh] - state.mBindJoints[LCalf]).length()
            + (state.mBindJoints[LCalf] - state.mBindJoints[LFoot]).length();
        if (state.mHaveBindJoint[LThigh] && state.mHaveBindJoint[LCalf] && state.mHaveBindJoint[LFoot]
            && sourceLeg > 1e-5f)
            state.mScale = targetLeg / sourceLeg;
        else
            state.mScale = torso.length() / std::max((reference[Neck] - reference[Pelvis]).length(), 1e-5f);
        state.mSourcePelvisHeight = reference[Pelvis].z();
        for (std::size_t i = 0; i < state.mBones.size(); ++i)
        {
            const std::array<std::size_t, 4> arm{ LUpperArm, LForearm, LHand, RHand };
            for (std::size_t j = 0; j < arm.size(); ++j)
                if (state.mBones[i].mJoint == arm[j])
                    state.mSwordArm[j] = i;
        }
        if (const auto* ready = state.mBank->find("a012_000000"); ready
            && state.mHaveBindJoint[RFinger1] && state.mHaveBindJoint[RFinger2]
            && state.mHaveBindJoint[LFinger1] && state.mHaveBindJoint[LFinger2])
        {
            state.mBank->sample(*ready, 0.f, false, state.mSample);
            const float sourcePalm = (fromSource(state.mSample, state.mOffsets[RFinger2])
                - fromSource(state.mSample, state.mOffsets[RHand])).length();
            if (const auto axes = state.mBank->axesOffset("R_Weapon"); axes && sourcePalm > 1e-5f)
            {
                const float palmScale = (state.mBindJoints[RFinger2] - state.mBindJoints[RHand]).length() / sourcePalm;
                const osg::Vec3f separation = fromSource(state.mSample, state.mOffsets[RWeapon])
                    - fromSource(state.mSample, state.mOffsets[LWeapon]);
                state.mSwordGripDistance = (separation * unit(fromSource(state.mSample, *axes + 3), osg::Vec3f(0, 1, 0)))
                    * palmScale;
                state.mSwordPalmHalfWidth = (state.mBindJoints[RFinger1] - state.mBindJoints[RFinger2]).length();
            }
        }
        state.mModelRotations.resize(state.mBones.size());
        state.mModelMatrices.resize(state.mBones.size());
        state.mValid = true;
        for (std::size_t i = 0; i != state.mBones.size(); ++i)
        {
            osg::ref_ptr<Impl::Callback> callback = new Impl::Callback;
            callback->mState = mImpl;
            callback->mIndex = i;
            mControllers.emplace_back(state.mBones[i].mNode, callback);
        }

        state.mWeaponAxes = state.mBank->axesOffset("R_Weapon");
        if (const auto found = nodes.find("weapon bone"); found != nodes.end() && state.mWeaponAxes)
        {
            state.mWeaponNode = dynamic_cast<NifOsg::MatrixTransform*>(found->second.get());
            if (state.mWeaponNode)
            {
                const auto paths = state.mWeaponNode->getParentalNodePaths(&objectRoot);
                if (!paths.empty())
                {
                    state.mWeaponParents = paths.front();
                    state.mWeaponParents.pop_back();
                    std::erase(state.mWeaponParents, &objectRoot);
                    for (bool before : { true, false })
                    {
                        osg::ref_ptr<Impl::WeaponCallback> callback = new Impl::WeaponCallback;
                        callback->mState = mImpl;
                        callback->mBeforeNative = before;
                        auto& controllers = before ? mBeforeNativeControllers : mControllers;
                        controllers.emplace_back(state.mWeaponNode, callback);
                    }
                }
                else
                    state.mWeaponNode = nullptr;
            }
        }
    }

    EldenAnimation::~EldenAnimation()
    {
        clear();
    }

    bool EldenAnimation::valid() const
    {
        return mImpl->mValid;
    }

    bool EldenAnimation::play(std::string_view clipName, float frame, bool loop, float duration, float yaw,
        std::string_view leftArmClip, std::string_view rightArmClip, std::string_view gripClip, bool alignSpear,
        bool twoHandedSword)
    {
        auto& state = *mImpl;
        if (!state.mValid || !std::isfinite(frame) || !std::isfinite(yaw))
        {
            clear();
            return false;
        }
        const auto* clip = state.mBank->find(clipName);
        if (!clip)
        {
            clear();
            return false;
        }
        duration = std::isfinite(duration) ? std::max(duration, 0.f) : 0.f;
        state.mBank->sample(*clip, frame, loop, state.mSample);
        state.mLayerFrame += duration * 30.f;
        state.overlay(leftArmClip, true);
        state.overlay(rightArmClip, false);
        state.grip(gripClip, alignSpear);
        const bool spear = alignSpear && state.mWeaponNode && state.mWeaponAxes;
        // Buffered rolls and other repeated actions can restart the same clip.
        // Blend from the displayed pose when its action clock resets, while
        // ordinary gait loop seams and a frozen action retain their blend.
        const bool restarted = !loop && !state.mLoop && frame < state.mFrame;
        const bool clipChanged = state.mClip != clipName || restarted;
        const bool changed = clipChanged || state.mLeftArmClip != leftArmClip
            || state.mRightArmClip != rightArmClip || state.mGripClip != gripClip || state.mAlignSpear != spear;
        state.mFrame = frame;
        state.mLoop = loop;
        if (!spear)
        {
            state.restoreWeapon();
            state.mHaveShownWeapon = false;
            state.mHaveWeaponBlendFrom = false;
        }
        state.mAlignSpear = spear;
        if (changed)
        {
            // Clip/layer changes already crossfade from the displayed pose.
            // Start each new source pose with its own anatomical reference.
            for (auto& plane : state.mLimbPlanes)
                plane.reset();
            state.mFacing.start(yaw);
            const osg::Quat inverseFacing = osg::Quat(state.mFacing.from(), osg::Vec3f(0, 0, 1)).inverse();
            state.mHaveWeaponBlendFrom = state.mHaveShownWeapon;
            if (state.mHaveShownWeapon)
                state.mWeaponBlendFrom = state.mShownWeaponRotation * inverseFacing;
            state.mClip = clipName;
            state.mLeftArmClip = leftArmClip;
            state.mRightArmClip = rightArmClip;
            state.mGripClip = gripClip;
            state.mFade = 0.f;
            state.mFadeLength = clipChanged ? clip->mBlendFrames / 30.f : 0.12f;
            for (Impl::Bone& bone : state.mBones)
            {
                if (state.mEnabled)
                    bone.mFrom = bone.mShown;
                else
                {
                    bone.mFrom.mTranslation = bone.mNode->getMatrix().getTrans();
                    if (const auto* nif = dynamic_cast<const NifOsg::MatrixTransform*>(bone.mNode.get()))
                        bone.mFrom.mRotation = nif->mRotationScale.toOsgMatrix().getRotate();
                    else
                        bone.mFrom.mRotation = bone.mRest.mRotation;
                }
                const osg::Quat parentRotation
                    = bone.mParent == sNoParent ? inverseFacing : state.mBones[bone.mParent].mFromModelRotation;
                bone.mFromModelRotation = bone.mFrom.mRotation * parentRotation;
            }
        }
        state.mFade += duration;
        float blend = state.mFadeLength > 0.f ? std::clamp(state.mFade / state.mFadeLength, 0.f, 1.f) : 1.f;
        blend = blend * blend * (3.f - 2.f * blend);
        const osg::Quat facing(state.mFacing.update(yaw, blend), osg::Vec3f(0, 0, 1));
        if (spear)
        {
            state.mWeaponBlend = blend;
            state.mWeaponTargetRotation = frameRotation(fromSource(state.mSample, *state.mWeaponAxes),
                fromSource(state.mSample, *state.mWeaponAxes + 3));
        }
        JointPositions positions;
        for (std::size_t j = 0; j != JointCount; ++j)
            positions[j] = fromSource(state.mSample, state.mOffsets[j]);
        const AnatomicalFrame body = anatomy(positions);
        std::array<osg::Vec3f, 10> limbPlanes;
        for (std::size_t i = 0; i != 4; ++i)
        {
            const std::size_t arm = i < 2 ? LClavicle : RClavicle;
            const osg::Vec3f upper = positions[arm + 2] - positions[arm + 1];
            const osg::Vec3f lower = positions[arm + 3] - positions[arm + 2];
            const osg::Vec3f primary = i % 2 == 0 ? upper : lower;
            limbPlanes[i]
                = state.mLimbPlanes[i].secondary(upper, lower, primary, primary ^ body.mChestForward, duration);
        }
        for (std::size_t i = 0; i != 4; ++i)
        {
            const std::size_t leg = i < 2 ? LThigh : RThigh;
            const osg::Vec3f upper = positions[leg + 1] - positions[leg];
            const osg::Vec3f lower = positions[leg + 2] - positions[leg + 1];
            const osg::Vec3f primary = i % 2 == 0 ? upper : lower;
            limbPlanes[i + 4] = state.mLimbPlanes[i + 4].secondary(upper, lower, primary, -body.mRight, duration);
        }
        for (std::size_t i = 0; i != 2; ++i)
        {
            const std::size_t foot = i == 0 ? LFoot : RFoot;
            const osg::Vec3f primary = positions[foot + 1] - positions[foot];
            limbPlanes[i + 8]
                = state.mLimbPlanes[i + 8].secondary(primary, body.mRight ^ primary, primary, body.mRight, duration);
        }
        const osg::Vec3f headUp = state.mHeadAxes ? fromSource(state.mSample, *state.mHeadAxes) : body.mUp;
        const osg::Vec3f headForward
            = state.mHeadAxes ? fromSource(state.mSample, *state.mHeadAxes + 6) : body.mChestForward;
        for (std::size_t i = 0; i != state.mBones.size(); ++i)
        {
            Impl::Bone& bone = state.mBones[i];
            LocalPose pose = bone.mRest;
            const osg::Quat parentRotation = bone.mParent == sNoParent ? osg::Quat() : state.mModelRotations[bone.mParent];
            const osg::Matrixf parentMatrix
                = bone.mParent == sNoParent ? osg::Matrixf() : state.mModelMatrices[bone.mParent];
            if (bone.mJoint != sNoJoint)
            {
                const osg::Quat modelRotation
                    = bone.mCorrection * jointFrame(bone.mJoint, positions, body, headUp, headForward, &limbPlanes);
                osg::Quat shownModelRotation;
                shownModelRotation.slerp(blend, bone.mFromModelRotation, modelRotation);
                // A reversal has two equally short heading arcs. Choose that
                // turn once for the whole body, independently of each bone's
                // pose, so the chest and pelvis cannot take opposite paths.
                shownModelRotation *= facing;
                // Blend the displayed anatomical orientation once. Blending
                // every local joint independently compounds parent and child
                // turns, making the wrist/weapon spin during roll recovery.
                pose.mRotation = shownModelRotation * parentRotation.inverse();
                if (bone.mJoint == Pelvis)
                {
                    osg::Vec3f translation = bone.mRestModel.getTrans();
                    translation.z() += (positions[Pelvis].z() - state.mSourcePelvisHeight) * state.mScale;
                    pose.mTranslation = translation * osg::Matrixf::inverse(parentMatrix);
                }
                pose.mTranslation = bone.mFrom.mTranslation * (1.f - blend) + pose.mTranslation * blend;
                bone.mShown = pose;
            }
            state.mModelRotations[i] = pose.mRotation * parentRotation;
            osg::Matrixf local = bone.mRestMatrix;
            if (bone.mJoint != sNoJoint)
            {
                local = osg::Matrixf::scale(bone.mScale, bone.mScale, bone.mScale)
                    * osg::Matrixf::rotate(pose.mRotation);
                local.setTrans(pose.mTranslation);
            }
            state.mModelMatrices[i] = local * parentMatrix;
        }
        if (twoHandedSword && loop && clipName == "a012_000000" && gripClip == clipName
            && leftArmClip.empty() && rightArmClip.empty())
            state.closeSwordGrip(blend);
        state.mEnabled = true;
        return true;
    }

    void EldenAnimation::clear()
    {
        mImpl->restore();
    }
}
