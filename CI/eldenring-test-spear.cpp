// Exercise the production retarget callbacks on a native-style NIF hierarchy.
// No game assets are required. Optionally pass player_anims.bin to also check
// the extracted spear heavy attacks and sword roll/jump recovery blends.
#include <apps/openmw/mwrender/eldenanimation.hpp>
#include <components/eldencombat/animations.hpp>
#include <components/nifosg/matrixtransform.hpp>
#include <components/sceneutil/nodecallback.hpp>

#include <osg/NodeVisitor>
#include <osg/Geometry>
#include <osg/Geode>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace
{
    int checks = 0;
    void check(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
        ++checks;
    }

    bool close(const osg::Vec3f& a, const osg::Vec3f& b)
    {
        return (a - b).length() < 2e-4f;
    }

    bool close(const osg::Quat& a, const osg::Quat& b)
    {
        return close(a * osg::Vec3f(1, 0, 0), b * osg::Vec3f(1, 0, 0))
            && close(a * osg::Vec3f(0, 1, 0), b * osg::Vec3f(0, 1, 0));
    }

    void u32(std::string& out, std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            out.push_back(static_cast<char>(value >> shift));
    }

    void f32(std::string& out, float value)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        u32(out, bits);
    }

    void name(std::string& out, std::string_view value)
    {
        out.push_back(static_cast<char>(value.size()));
        out += value;
    }

    constexpr std::array<std::string_view, 38> joints = {
        "Pelvis", "Spine", "Spine1", "Spine2", "Neck", "Head",
        "L_Clavicle", "L_UpperArm", "L_Forearm", "L_Hand",
        "R_Clavicle", "R_UpperArm", "R_Forearm", "R_Hand",
        "L_Thigh", "L_Calf", "L_Foot", "L_Toe0", "R_Thigh", "R_Calf", "R_Foot", "R_Toe0",
        "R_Weapon", "L_Weapon",
        "L_Finger0", "L_Finger01", "L_Finger02", "L_Finger1", "L_Finger2", "L_Finger21", "L_Finger22",
        "R_Finger0", "R_Finger01", "R_Finger02", "R_Finger1", "R_Finger2", "R_Finger21", "R_Finger22",
    };

    std::shared_ptr<const EldenCombat::AnimationBank> fixtureBank(
        bool weaponAxes = true, bool animatedBody = false, bool windingTorso = false)
    {
        // Express the fixture in OpenMW's coordinates, then encode ERAN's basis.
        std::array<osg::Vec3f, 38> p = {
            osg::Vec3f(0, 0, .9), {0, 0, 1.1}, {0, 0, 1.2}, {0, 0, 1.4}, {0, 0, 1.5}, {0, 0, 1.7},
            {-.08, 0, 1.5}, {-.15, 0, 1.45}, {-.3, 0, 1.3}, {-.45, 0, 1.2},
            {.08, 0, 1.5}, {.15, 0, 1.45}, {.3, 0, 1.3}, {.45, 0, 1.2},
            {-.1, 0, .85}, {-.1, 0, .45}, {-.1, 0, .05}, {-.1, .1, .05},
            {.1, 0, .85}, {.1, 0, .45}, {.1, 0, .05}, {.1, .1, .05},
            {.45, .08, 1.2}, {-.45, .08, 1.2},
        };
        for (int side = 0; side != 2; ++side)
            for (int finger = 0; finger != 7; ++finger)
                p[24 + side * 7 + finger] = p[side == 0 ? 9 : 13]
                    + osg::Vec3f(.02f * (finger % 3 + 1), finger < 3 ? -.02f : .02f * (finger - 3), 0);
        std::string bytes = "ERAN";
        u32(bytes, joints.size());
        u32(bytes, weaponAxes ? 3 : 1);
        u32(bytes, 5);
        for (const auto joint : joints)
            name(bytes, joint);
        name(bytes, "Head");
        if (weaponAxes)
        {
            name(bytes, "R_Weapon");
            name(bytes, "L_Weapon");
        }
        const auto vector = [&](const osg::Vec3f& v) {
            f32(bytes, -v.x());
            f32(bytes, v.z());
            f32(bytes, v.y());
        };
        for (std::string_view clip : { "a000_000000", "a012_000000", "a013_000000", "a036_032505", "a036_032500" })
        {
            auto positions = p;
            if (clip == "a012_000000")
            {
                positions[8] = { -.20f, .16f, 1.2f };
                positions[9] = { -.08f, .22f, 1.22f };
                positions[12] = { .14f, .12f, 1.23f };
                positions[13] = { .08f, .16f, 1.27f };
                for (int side = 0; side != 2; ++side)
                {
                    const int first = 24 + side * 7;
                    const osg::Vec3f hand = positions[side == 0 ? 9 : 13];
                    const float direction = side == 0 ? -1.f : 1.f;
                    for (int digit = 0; digit != 7; ++digit)
                        positions[first + digit] = hand
                            + osg::Vec3f(digit < 3 ? -.03f : (digit == 3 ? -.02f : 0.f),
                                .08f + .02f * (digit < 3 ? digit : std::max(digit - 4, 0)), 0) * direction;
                }
                positions[22] = positions[13] + osg::Vec3f(0, .08f, 0);
                positions[23] = positions[22] - osg::Vec3f(0, .196f, 0);
            }
            name(bytes, clip);
            f32(bytes, 6.f);
            u32(bytes, 2);
            for (int frame = 0; frame != 2; ++frame)
            {
                const osg::Quat bodyRotation(animatedBody && frame == 1 ? .8f : 0.f, osg::Vec3f(0, 0, 1));
                const osg::Quat torsoRotation(
                    windingTorso && clip == "a036_032500" ? .4f : 0.f, osg::Vec3f(0, 0, 1));
                for (std::size_t j = 0; j != p.size(); ++j)
                {
                    // Shift the weapon pivot, while the hand itself stays still.
                    osg::Vec3f position = positions[j]
                        + (j == 22 && frame == 1 ? osg::Vec3f(0, .32f, 0) : osg::Vec3f());
                    if ((j >= 6 && j <= 13) || j >= 22)
                        position = torsoRotation * position;
                    vector(bodyRotation * position);
                }
                vector({0, 0, 1}); vector({1, 0, 0}); vector({0, 1, 0});
                if (weaponAxes)
                    for (int side = 0; side != 2; ++side)
                    {
                        const bool turn = clip == "a036_032500";
                        vector(turn ? osg::Vec3f(0, 1, 0) : osg::Vec3f(-1, 0, 0));
                        vector(turn ? osg::Vec3f(1, 0, 0) : osg::Vec3f(0, 1, 0));
                        vector({0, 0, 1}); // Mirrored third axis must be rebuilt.
                    }
            }
        }
        std::istringstream stream(bytes);
        return std::make_shared<EldenCombat::AnimationBank>(stream);
    }

    struct NativeWeapon : SceneUtil::NodeCallback<NativeWeapon, NifOsg::MatrixTransform*>
    {
        osg::Quat mRotation = osg::Quat(1.57079632679, osg::Vec3f(1, 0, 0));
        osg::Vec3f mTranslation = {6.2637f, .7815f, 2.3277f};
        float mScale = 1.25f;
        bool mWriteRotation = true;
        void operator()(NifOsg::MatrixTransform* node, osg::NodeVisitor* visitor)
        {
            if (mWriteRotation)
                node->setRotation(mRotation);
            node->setTranslation(mTranslation);
            node->setScale(mScale);
            traverse(node, visitor);
        }
    };

    struct Update : osg::NodeVisitor
    {
        Update() : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN) { setVisitorType(UPDATE_VISITOR); }
        void apply(osg::Node& node) override
        {
            if (auto* callback = node.getUpdateCallback())
                callback->run(&node, this);
            else
                traverse(node);
        }
        void apply(osg::Group& node) override { apply(static_cast<osg::Node&>(node)); }
        void apply(osg::Transform& node) override { apply(static_cast<osg::Node&>(node)); }
        void apply(osg::MatrixTransform& node) override { apply(static_cast<osg::Node&>(node)); }
    };

    struct Rig
    {
        osg::ref_ptr<osg::Group> mRoot = new osg::Group;
        MWRender::Animation::NodeMap mNodes;
        osg::ref_ptr<NifOsg::MatrixTransform> mHand, mWeapon;
        osg::ref_ptr<NativeWeapon> mNative = new NativeWeapon;
        std::unique_ptr<MWRender::EldenAnimation> mAnimation;

        NifOsg::MatrixTransform* bone(std::string key, osg::Group* parent, osg::Vec3f translation)
        {
            osg::ref_ptr<NifOsg::MatrixTransform> node = new NifOsg::MatrixTransform(Nif::NiTransform::getIdentity());
            node->setName(key);
            node->setTranslation(translation);
            parent->addChild(node);
            mNodes.emplace(std::move(key), node);
            return node.get();
        }

        explicit Rig(std::shared_ptr<const EldenCombat::AnimationBank> bank, bool armHelper = false,
            bool stockFingers = false)
        {
            auto* pelvis = bone("bip01 pelvis", mRoot.get(), {0, 0, 60});
            auto* spine = bone("bip01 spine", pelvis, {0, 0, 10});
            auto* neck = bone("bip01 neck", spine, {0, 0, 20});
            bone("bip01 head", neck, {0, 0, 10});
            for (const char* side : { "l", "r" })
            {
                const std::string prefix = std::string("bip01 ") + side;
                auto* thigh = bone(prefix + " thigh", pelvis, {side[0] == 'l' ? -6.f : 6.f, 0, -3});
                auto* calf = bone(prefix + " calf", thigh, {0, 0, -24});
                auto* foot = bone(prefix + " foot", calf, {0, 0, -24});
                bone(prefix + " toe0", foot, {0, 6, 0});
            }
            auto* armParent = neck;
            if (armHelper)
            {
                armParent = bone("arm helper", neck, {0, 0, 0});
                armParent->setRotation(osg::Quat(.3f, osg::Vec3f(0, 1, 0)));
            }
            auto* upper = bone("bip01 r upperarm", armParent, {10, 0, 0});
            auto* forearm = bone("bip01 r forearm", upper, {15, 0, 0});
            forearm->setScale(1.1f);
            mHand = bone("bip01 r hand", forearm, {15, 0, 0});
            if (stockFingers)
            {
                auto* leftUpper = bone("bip01 l upperarm", neck, {-10, 0, 0});
                auto* leftForearm = bone("bip01 l forearm", leftUpper, {-15, 0, 0});
                auto* leftHand = bone("bip01 l hand", leftForearm, {-15, 0, 0});
                for (const char* side : { "l", "r" })
                {
                    const std::string prefix = std::string("bip01 ") + side + " finger";
                    auto* hand = side[0] == 'l' ? leftHand : mHand.get();
                    // Stock hands have two joints per digit, with a separate
                    // index and grouped remaining fingers. The terminal
                    // phalanges are already bent in the native bind pose.
                    auto* thumb = bone(prefix + "0", hand, {3, -2, 0});
                    thumb->setRotation(osg::Quat(.45, osg::Vec3f(0, 0, 1)));
                    bone(prefix + "01", thumb, {2.49f, 0, 0})
                        ->setRotation(osg::Quat(-.454, osg::Vec3f(0, 0, 1)));
                    auto* pointer = bone(prefix + "1", hand, {4, -1, 0});
                    pointer->setRotation(osg::Quat(.2, osg::Vec3f(0, 0, 1)));
                    bone(prefix + "11", pointer, {2.90f, 0, 0})
                        ->setRotation(osg::Quat(1.126, osg::Vec3f(0, 0, 1)));
                    auto* middle = bone(prefix + "2", hand, {4, 1, 0});
                    bone(prefix + "21", middle, {2.90f, 0, 0})
                        ->setRotation(osg::Quat(.829, osg::Vec3f(0, 0, 1)));
                }
            }
            else
            {
                bone("bip01 r finger1", mHand.get(), {4, -1, 0});
                bone("bip01 r finger2", mHand.get(), {4, 1, 0});
            }
            mWeapon = bone("weapon bone", mHand.get(), mNative->mTranslation);
            mWeapon->setRotation(mNative->mRotation);
            mAnimation = std::make_unique<MWRender::EldenAnimation>(mNodes, *mRoot, std::move(bank));
            check(mAnimation->valid(), "fixture rig was rejected");
            // Model base_anim's permanent controller: restore must precede it.
            mWeapon->addUpdateCallback(mNative.get());
            for (const auto& [node, callback] : mAnimation->beforeNativeControllers())
            {
                callback->setNestedCallback(node->getUpdateCallback());
                node->setUpdateCallback(callback.get());
            }
            for (const auto& [node, callback] : mAnimation->controllers())
                node->addUpdateCallback(callback.get());
        }

        void update() { Update visitor; mRoot->accept(visitor); }
        osg::Quat modelRotation(osg::Node* node) const
        {
            osg::Vec3f translation, scale;
            osg::Quat rotation, scaleOrientation;
            osg::computeLocalToWorld(node->getParentalNodePaths(mRoot.get()).front()).decompose(
                translation, rotation, scale, scaleOrientation);
            return rotation;
        }
        osg::Matrixf modelMatrix(osg::Node* node) const
        {
            return osg::computeLocalToWorld(node->getParentalNodePaths(mRoot.get()).front());
        }
        osg::Vec3f tip() const { return modelRotation(mWeapon.get()) * osg::Vec3f(0, 1, 0); }
        void play(std::string_view clip, float frame = 0, float duration = 1, bool spear = true, float yaw = 0,
            std::string_view grip = "a013_000000")
        {
            check(mAnimation->play(clip, frame, false, duration, yaw, {}, {}, grip, spear), "clip rejected");
            update();
        }
    };

    void poseBlending()
    {
        auto bank = fixtureBank();
        Rig rig(bank, true), target(bank, true);
        target.play("a000_000000", 0, 1, false, 0, "a000_000000");
        const osg::Quat finalHand = target.modelRotation(target.mHand.get());
        // Two large native local turns can almost cancel in model space.
        // Independently interpolating each local turn makes the hand take
        // the long way around while entering the imported stance.
        const osg::Quat turn(170.f * 3.14159265359f / 180.f, osg::Vec3f(0, 0, 1));
        for (const char* name : { "bip01 r upperarm", "bip01 r forearm" })
            static_cast<NifOsg::MatrixTransform*>(rig.mNodes.at(name).get())->setRotation(turn);
        const osg::Quat nativeHand = rig.modelRotation(rig.mHand.get());
        rig.play("a000_000000", 0, .1f, false, 0, "a000_000000");
        osg::Quat expected;
        expected.slerp(.5f, nativeHand, finalHand);
        check(close(rig.modelRotation(rig.mHand.get()), expected),
            "native parent/child turns compounded into an imported wrist twist");
        check(close(rig.mWeapon->mRotationScale.toOsgMatrix().getRotate(), rig.mNative->mRotation),
            "sword pose blend changed the native attachment rotation");

        // An interrupted transition must start from the displayed orientation,
        // including an unmapped ancestor between retargeted bones.
        const osg::Quat interrupted = rig.modelRotation(rig.mHand.get());
        target.play("a036_032500", 0, 1, false, .7f, "a000_000000");
        rig.play("a036_032500", 0, .1f, false, .7f, "a000_000000");
        expected.slerp(.5f, interrupted,
            target.modelRotation(target.mHand.get()) * osg::Quat(-.7f, osg::Vec3f(0, 0, 1)));
        expected = expected * osg::Quat(.35f, osg::Vec3f(0, 0, 1));
        check(close(rig.modelRotation(rig.mHand.get()), expected),
            "interrupted transition lost the displayed wrist orientation");

        rig.mAnimation->clear();
        const osg::Quat restored = rig.modelRotation(rig.mHand.get());
        rig.play("a000_000000", 0, .1f, false, 0, "a000_000000");
        expected.slerp(.5f, restored, finalHand);
        check(close(rig.modelRotation(rig.mHand.get()), expected),
            "reactivating imported animation reused a stale model-space blend origin");
    }

    constexpr std::array<const char*, 10> directionNodes = { "bip01 pelvis", "bip01 spine", "bip01 neck",
        "bip01 r upperarm", "bip01 r hand", "bip01 l thigh", "bip01 l calf", "bip01 r thigh",
        "bip01 r calf", "weapon bone" };
    using Rotations = std::array<osg::Quat, directionNodes.size()>;

    Rotations rotations(const Rig& rig)
    {
        Rotations result;
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] = rig.modelRotation(rig.mNodes.at(directionNodes[i]).get());
        return result;
    }

    void checkDirectionBlend(const Rig& rig, const Rotations& from, const Rotations& target,
        float sourceYaw, float shownYaw, float amount, const char* message)
    {
        const osg::Quat removeSourceYaw(-sourceYaw, osg::Vec3f(0, 0, 1));
        const osg::Quat heading(shownYaw, osg::Vec3f(0, 0, 1));
        const Rotations shown = rotations(rig);
        for (std::size_t i = 0; i < shown.size(); ++i)
        {
            osg::Quat expected;
            expected.slerp(amount, from[i] * removeSourceYaw, target[i]);
            check(close(shown[i], expected * heading), message);
        }
    }

    void directionReversals()
    {
        constexpr float pi = 3.14159265359f;
        constexpr float degrees = pi / 180.f;
        const auto bank = fixtureBank(true, false, true);
        Rig forward(bank, true), wound(bank, true);
        forward.play("a000_000000");
        wound.play("a036_032500");
        const Rotations forwardPose = rotations(forward), woundPose = rotations(wound);

        // A small change in torso winding straddles the 180-degree boundary:
        // independent world-space bone blends turn the chest against the legs.
        for (float direction : { 1.f, -1.f })
        {
            const bool positive = direction > 0.f;
            const char* fromClip = positive ? "a000_000000" : "a036_032500";
            const char* targetClip = positive ? "a036_032500" : "a000_000000";
            const Rotations& from = positive ? forwardPose : woundPose;
            const Rotations& target = positive ? woundPose : forwardPose;
            Rig rig(bank, true);
            rig.play(fromClip);
            const float turn = direction * (pi - .05f);
            rig.play(targetClip, 0, .1f, true, turn);
            checkDirectionBlend(rig, from, target, 0, turn * .5f, .5f,
                "opposite movement turned upper and lower body in different directions");
        }

        // Crossing the yaw representation seam must take the short 20-degree turn.
        Rig seam(bank, true);
        seam.play("a000_000000", 0, 1, true, 170.f * degrees);
        const Rotations beforeSeam = rotations(seam);
        seam.play("a036_032500", 0, .1f, true, -170.f * degrees);
        checkDirectionBlend(seam, beforeSeam, woundPose, 170.f * degrees, pi, .5f,
            "yaw seam made the character spin or split during a direction change");

        // A moving yaw target must keep its turn direction across the antipode
        // and retain the original fade, even when the clip remains unchanged.
        Rig moving(bank, true);
        moving.play("a000_000000");
        moving.play("a036_032500", 0, .05f, true, 170.f * degrees);
        moving.play("a036_032500", 0, .1f, true, -170.f * degrees);
        checkDirectionBlend(moving, forwardPose, woundPose, 0, 190.f * degrees * .84375f, .84375f,
            "same-clip yaw update reversed or restarted the active body turn");
        moving.play("a036_032500", 0, .05f, true, -170.f * degrees);
        checkDirectionBlend(moving, forwardPose, woundPose, 0, 190.f * degrees, 1.f,
            "direction change did not finish at the current heading");

        // Interrupt at the displayed pose, then blend from its shared heading.
        Rig interrupted(bank, true);
        interrupted.play("a000_000000");
        interrupted.play("a036_032500", 0, .1f, true, 170.f * degrees);
        const Rotations shown = rotations(interrupted);
        interrupted.play("a000_000000", 0, 0, true, -170.f * degrees);
        checkDirectionBlend(interrupted, shown, forwardPose, 85.f * degrees, 85.f * degrees, 0.f,
            "interrupted reversal jumped away from the displayed pose");
        interrupted.play("a000_000000", 0, .1f, true, -170.f * degrees);
        checkDirectionBlend(interrupted, shown, forwardPose, 85.f * degrees, 137.5f * degrees, .5f,
            "interrupted reversal lost the displayed heading or pose");

        // At exactly opposite headings either shared direction is valid. Every
        // bone must nevertheless preserve the same heading-relative pose.
        for (float turn : { pi, -pi })
        {
            Rig opposite(bank, true);
            opposite.play("a000_000000");
            opposite.play("a036_032500", 0, .1f, true, turn);
            const Rotations actual = rotations(opposite);
            osg::Quat pelvis;
            pelvis.slerp(.5f, forwardPose[0], woundPose[0]);
            for (std::size_t i = 1; i < actual.size(); ++i)
            {
                osg::Quat expected;
                expected.slerp(.5f, forwardPose[i], woundPose[i]);
                check(close(actual[i] * actual[0].inverse(), expected * pelvis.inverse()),
                    "exactly opposite headings selected conflicting body turn directions");
            }
        }
    }

    void regression()
    {
        Rig rig(fixtureBank());
        rig.play("a036_032505");
        check(close(rig.tip(), {0, 1, 0}), "forward thrust spear points vertically instead of forward");
        check(close(rig.mWeapon->getMatrix().getTrans(), rig.mNative->mTranslation), "attachment offset changed");
        check(std::abs(rig.mWeapon->mScale - rig.mNative->mScale) < 1e-5, "attachment scale changed");
        const auto firstHand = rig.modelRotation(rig.mHand.get());
        rig.play("a036_032505", 1);
        check(close(rig.tip(), {0, 1, 0}), "sliding grip changed spear direction");
        check(close(rig.modelRotation(rig.mHand.get()), firstHand), "sliding shaft dragged fingers away from wrist");

        const osg::Quat previous = rig.modelRotation(rig.mWeapon.get());
        rig.play("a036_032500", 0, .1f);
        osg::Quat expected;
        const osg::Quat target = osg::Matrixf(0, 1, 0, 0, 1, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1).getRotate();
        expected.slerp(.5, previous, target);
        check(close(rig.modelRotation(rig.mWeapon.get()), expected), "weapon blend did not cancel displayed parent pose");
        rig.play("a036_032500", 1);
        check(close(rig.tip(), {1, 0, 0}), "weapon axes were replaced with a constant correction");
        rig.play("a036_032505", 0, 1, true, 1.57079632679f);
        check(close(rig.tip(), {-1, 0, 0}), "actor-local animation yaw was omitted");

        rig.mNative->mRotation = osg::Quat(.42, osg::Vec3f(0, 0, 1));
        rig.play("a036_032505");
        rig.mAnimation->clear();
        check(close(rig.mWeapon->mRotationScale.toOsgMatrix().getRotate(), rig.mNative->mRotation),
            "clear failed to restore embedded native rotation");
        rig.play("a036_032505");
        rig.mNative->mWriteRotation = false;
        rig.play("a036_032505", 1);
        rig.mAnimation->clear();
        check(close(rig.mWeapon->mRotationScale.toOsgMatrix().getRotate(), rig.mNative->mRotation),
            "translation-only native track inherited previous spear rotation");
        rig.mNative->mWriteRotation = true;
        rig.play("a036_032505", 0, 1, false);
        check(close(rig.mWeapon->mRotationScale.toOsgMatrix().getRotate(), rig.mNative->mRotation),
            "non-spear attachment was overridden");
        rig.play("a036_032505");
        check(!rig.mAnimation->play("missing", 0, false, 1, 0, {}, {}, {}, true), "missing clip accepted");
        check(close(rig.mWeapon->mRotationScale.toOsgMatrix().getRotate(), rig.mNative->mRotation),
            "missing clip left spear override active");

        Rig noAxes(fixtureBank(false));
        noAxes.play("a036_032505");
        check(close(noAxes.mWeapon->mRotationScale.toOsgMatrix().getRotate(), noAxes.mNative->mRotation),
            "bank without weapon axes lost native fallback");
    }

    void repeatedActions()
    {
        const auto bank = fixtureBank(true, true);
        Rig reference(bank);
        const auto play = [](Rig& rig, float frame, bool loop, float duration) {
            check(rig.mAnimation->play("a036_032505", frame, loop, duration), "transition clip rejected");
            rig.update();
            return rig.modelRotation(rig.mNodes.at("bip01 pelvis").get());
        };
        const auto target = [&](float frame) { return play(reference, frame, false, 1.f); };
        const osg::Quat start = target(0.f);
        const osg::Quat end = target(1.f);
        check(!close(start, end), "transition fixture has no animated body rotation");

        for (const float step : { 1.f / 30.f, 1.f / 60.f, 1.f / 144.f })
        {
            Rig rig(bank);
            play(rig, 1.f, false, 1.f);
            const auto actual = play(rig, 0.f, false, step);
            const float amount = step / .2f;
            osg::Quat expected;
            expected.slerp(amount * amount * (3.f - 2.f * amount), end, start);
            check(close(actual, expected), "repeated action snapped to the first frame of the same clip");
        }

        Rig rig(bank);
        play(rig, 1.f, false, 1.f);
        play(rig, 0.f, false, .1f);
        const auto advancing = play(rig, .5f, false, .05f);
        osg::Quat expected;
        expected.slerp(.84375, end, target(.5f));
        check(close(advancing, expected), "forward playback restarted the action transition blend");
        check(close(play(rig, .5f, false, 0.f), advancing), "hit-stop changed the displayed transition pose");
        check(close(play(rig, .75f, false, .05f), target(.75f)), "resumed action did not finish its blend");

        play(rig, .75f, true, 1.f);
        check(close(play(rig, 0.f, true, 1.f / 60.f), start), "ordinary loop wrap restarted the action blend");
        check(close(play(rig, .5f, true, 1.f / 60.f), target(.5f)), "loop playback retained a spurious blend");
    }

    void fingerCurl(const std::shared_ptr<const EldenCombat::AnimationBank>& bank, std::string_view stance)
    {
        Rig rig(bank, false, true);
        const auto* clip = bank->find(stance);
        check(clip != nullptr, "finger stance missing");
        std::vector<float> sample;
        bank->sample(*clip, 0, false, sample);
        const auto position = [&](const std::string& name) {
            const auto offset = bank->jointOffset(name);
            check(offset.has_value(), "finger source joint missing");
            return osg::Vec3f(-sample[*offset], sample[*offset + 2], sample[*offset + 1]);
        };
        std::array<osg::Vec3f, 12> translations;
        const std::array<std::string_view, 6> nativeDigits = { "0", "01", "1", "11", "2", "21" };
        const std::array<std::string_view, 6> sourceBegin = { "0", "01", "2", "21", "2", "21" };
        const std::array<std::string_view, 6> sourceEnd = { "01", "02", "21", "22", "21", "22" };
        for (int side = 0; side != 2; ++side)
            for (std::size_t digit = 0; digit != nativeDigits.size(); ++digit)
            {
                const std::string key = std::string(side == 0 ? "bip01 l finger" : "bip01 r finger")
                    + std::string(nativeDigits[digit]);
                translations[side * 6 + digit] = rig.mNodes.at(key)->getMatrix().getTrans();
            }
        for (int entry = 0; entry != 2; ++entry)
        {
            rig.play(stance, 0, 1, false, 0, stance);
            for (int side = 0; side != 2; ++side)
                for (std::size_t digit = 0; digit != nativeDigits.size(); ++digit)
                {
                    const std::string key = std::string(side == 0 ? "bip01 l finger" : "bip01 r finger")
                        + std::string(nativeDigits[digit]);
                    const std::string source = side == 0 ? "L_Finger" : "R_Finger";
                    osg::Vec3f expected = position(source + std::string(sourceEnd[digit]))
                        - position(source + std::string(sourceBegin[digit]));
                    expected.normalize();
                    auto* node = rig.mNodes.at(key).get();
                    check(close(rig.modelRotation(node) * osg::Vec3f(1, 0, 0), expected),
                        "native finger turned away from the imported curl");
                    check(close(node->getMatrix().getTrans(), translations[side * 6 + digit]),
                        "finger retarget changed native phalanx length");
                }
            rig.mAnimation->clear();
        }
    }

    void swordSupportGrip()
    {
        const auto bank = fixtureBank();
        Rig rig(bank, false, true), reference(bank, false, true);
        const auto attachHandle = [](Rig& target, bool useGeode = true) {
            osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
            for (float y : { -18.f, 2.f })
                for (const osg::Vec3f corner : { osg::Vec3f(-.5f, 0, -.5f), osg::Vec3f(.5f, 0, -.5f),
                         osg::Vec3f(.5f, 0, .5f), osg::Vec3f(-.5f, 0, .5f) })
                    vertices->push_back(corner + osg::Vec3f(0, y, 0));
            osg::ref_ptr<osg::DrawElementsUInt> indices = new osg::DrawElementsUInt(GL_TRIANGLES);
            for (unsigned side = 0; side != 4; ++side)
            {
                const unsigned next = (side + 1) % 4;
                for (unsigned index : { side, next, next + 4, side, next + 4, side + 4 })
                    indices->push_back(index);
            }
            osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
            geometry->setVertexArray(vertices.get());
            geometry->addPrimitiveSet(indices.get());
            osg::ref_ptr<osg::MatrixTransform> meshOffset = new osg::MatrixTransform;
            meshOffset->setMatrix(osg::Matrixf::translate(.25f, 0, .4f));
            if (useGeode)
            {
                osg::ref_ptr<osg::Geode> geode = new osg::Geode;
                geode->addDrawable(geometry.get());
                meshOffset->addChild(geode.get());
            }
            else
                meshOffset->addChild(geometry.get()); // Native NIFs attach drawables directly.
            target.mWeapon->addChild(meshOffset.get());
            target.update(); // Finish the native attachment's scale/offset first.
        };
        attachHandle(rig);
        attachHandle(reference);
        auto* leftHand = rig.mNodes.at("bip01 l hand").get();
        const osg::Matrixf leftBind = rig.modelMatrix(leftHand);
        const osg::Matrixf rightBind = rig.modelMatrix(rig.mHand.get());
        osg::Vec3f nativePalm = osg::Matrixf::transform3x3(rig.mNative->mTranslation, rightBind);
        nativePalm.x() = -nativePalm.x();
        nativePalm = osg::Matrixf::transform3x3(nativePalm, osg::Matrixf::inverse(leftBind));
        const float nativePalmLength = (rig.modelMatrix(rig.mNodes.at("bip01 r finger2").get()).getTrans()
            - rightBind.getTrans()).length();
        const float gripDistance = .196f * nativePalmLength / .08f;
        std::vector<std::pair<osg::MatrixTransform*, osg::Vec3f>> translations;
        for (const auto& [name, node] : rig.mNodes)
            translations.emplace_back(node.get(), node->getMatrix().getTrans());
        const auto play = [](Rig& target, bool correction, std::string_view clip = "a012_000000",
                              bool loop = true, float duration = 1.f, std::string_view left = {},
                              bool spear = false, float yaw = 0.f) {
            check(target.mAnimation->play(clip, 0, loop, duration, yaw, left, {}, "a012_000000", spear, correction),
                "support grip fixture clip rejected");
            target.update();
        };
        const auto palm = [&] { return nativePalm * rig.modelMatrix(leftHand); };
        const auto contact = [&] {
            const osg::Matrixf weapon = rig.modelMatrix(rig.mWeapon.get());
            const float scale = osg::Matrixf::transform3x3(osg::Vec3f(0, 1, 0), weapon).length();
            return osg::Vec3f(.25f, -gripDistance / scale, .4f) * weapon;
        };
        for (float yaw : { 0.f, 1.1f })
        {
            play(reference, false, "a012_000000", true, 1.f, {}, false, yaw);
            play(rig, false, "a012_000000", true, 1.f, {}, false, yaw);
            check((palm() - contact()).length() > .1f, "support grip fixture was already closed");
            play(rig, true, "a012_000000", true, 1.f, {}, false, yaw);
            check(close(palm(), contact()), "supporting palm did not reach the visible sword handle");
            check(close(rig.modelRotation(leftHand), reference.modelRotation(reference.mNodes.at("bip01 l hand").get())),
                "closing the supporting arm rotated its hand/finger curl");
            check(close(rig.modelMatrix(rig.mWeapon.get()).getTrans(), reference.modelMatrix(reference.mWeapon.get()).getTrans())
                    && close(rig.modelRotation(rig.mWeapon.get()), reference.modelRotation(reference.mWeapon.get())),
                "supporting arm correction moved the sword or leading hand");
            for (const auto& [node, translation] : translations)
                check(close(node->getMatrix().getTrans(), translation), "support grip changed a native bone length");
            const osg::Vec3f held = palm();
            play(rig, true, "a012_000000", true, 0.f, {}, false, yaw);
            check(close(palm(), held), "frozen support grip accumulated another correction");
            rig.update();
            check(close(palm(), held), "repeated scene traversal accumulated a support correction");
        }

        // Native stance entry and action recovery must begin at the displayed
        // pose, including the corrected elbow, before advancing their fade.
        rig.mAnimation->clear();
        play(rig, false, "a000_000000");
        const osg::Vec3f entry = palm();
        play(rig, true, "a012_000000", true, 0.f);
        check(close(palm(), entry), "entering sword ready snapped its supporting hand");
        play(rig, true, "a012_000000", true, .1f);
        const osg::Vec3f halfway = palm();
        play(rig, true, "a012_000000", true, 0.f);
        check(close(palm(), halfway), "paused ready fade changed its support solve");
        play(rig, true, "a012_000000", true, .1f);
        check(close(palm(), contact()), "ready fade did not complete its handle contact");
        const osg::Vec3f ready = palm();
        play(rig, true, "a036_032505", false, 0.f);
        check(close(palm(), ready), "leaving sword ready discarded its displayed support pose");

        // The flag alone must never override attacks, overlays or spear poses.
        for (const auto& [clip, loop, layer, spear] : {
                 std::tuple{ "a012_000000", false, "", false },
                 std::tuple{ "a036_032505", false, "", false },
                 std::tuple{ "a000_000000", true, "", false },
                 std::tuple{ "a012_000000", true, "a013_000000", false },
                 std::tuple{ "a013_000000", true, "", true },
             })
        {
            play(rig, true, clip, loop, 1.f, layer, spear);
            play(reference, false, clip, loop, 1.f, layer, spear);
            for (const char* name : { "bip01 l upperarm", "bip01 l forearm", "bip01 l hand" })
                check(close(rig.modelRotation(rig.mNodes.at(name).get()), reference.modelRotation(reference.mNodes.at(name).get()))
                        && close(rig.modelMatrix(rig.mNodes.at(name).get()).getTrans(), reference.modelMatrix(reference.mNodes.at(name).get()).getTrans()),
                    "sword support correction escaped its ready-stance scope");
        }
        rig.mWeapon->removeChildren(0, rig.mWeapon->getNumChildren());
        attachHandle(rig, false);
        play(rig, true);
        check(close(palm(), contact()), "replacement NIF drawable did not refresh the supporting grip");
        rig.mWeapon->removeChildren(0, rig.mWeapon->getNumChildren());
        play(rig, true);
        play(reference, false);
        check(close(rig.modelMatrix(leftHand).getTrans(),
                  reference.modelMatrix(reference.mNodes.at("bip01 l hand").get()).getTrans()),
            "removing the equipped mesh retained an obsolete support contact");
    }

    void realClips(const char* path)
    {
        std::ifstream file(path, std::ios::binary);
        auto bank = std::make_shared<EldenCombat::AnimationBank>(file);
        for (const std::string_view stance : { "a000_000000", "a010_000000", "a012_000000", "a013_000000" })
            fingerCurl(bank, stance);
        Rig rig(bank);
        const auto axes = bank->axesOffset("R_Weapon");
        check(axes.has_value(), "real bank lacks weapon axes");
        std::vector<float> sample;
        for (const auto name : { "a036_030500", "a036_030505", "a036_032500", "a036_032505" })
        {
            const auto* clip = bank->find(name);
            check(clip != nullptr, "real spear heavy clip missing");
            for (std::size_t frame = 0; frame < clip->mFrames; ++frame)
            {
                rig.play(name, static_cast<float>(frame));
                bank->sample(*clip, static_cast<float>(frame), false, sample);
                osg::Vec3f expected(-sample[*axes + 3], sample[*axes + 5], sample[*axes + 4]);
                expected.normalize();
                check(close(rig.tip(), expected), "real spear tip diverged from imported weapon axis");
            }
        }

        Rig stance(bank);
        stance.play("a000_000000", 0, 1, false, 0, "a000_000000");
        const osg::Quat targetHand = stance.modelRotation(stance.mHand.get());
        const auto* idle = bank->find("a000_000000");
        check(idle && idle->mBlendFrames > 0.f, "real stance lacks its recovery blend");
        for (const auto& [name, frame] : { std::pair{ "a000_027100", 10.f },
                 std::pair{ "a000_027100", 15.f }, std::pair{ "a000_027110", 22.f },
                 std::pair{ "a000_202020", 24.f }, std::pair{ "a000_202040", 15.f } })
            for (float yaw : { 0.f, 1.57079632679f })
            {
                rig.play(name, frame, 1, false, yaw, "a000_000000");
                const osg::Quat from = rig.modelRotation(rig.mHand.get());
                rig.play("a000_000000", 0, idle->mBlendFrames / 60.f, false, 0, "a000_000000");
                osg::Quat expected;
                expected.slerp(.5f, from * osg::Quat(-yaw, osg::Vec3f(0, 0, 1)), targetHand);
                expected = expected * osg::Quat(yaw * .5f, osg::Vec3f(0, 0, 1));
                check(close(rig.modelRotation(rig.mHand.get()), expected),
                    "real roll/jump recovery compounded body rotations into a sword wrist twist");
            }
    }
}

int main(int argc, char** argv)
{
    try
    {
        regression();
        poseBlending();
        directionReversals();
        repeatedActions();
        fingerCurl(fixtureBank(), "a013_000000");
        swordSupportGrip();
        if (argc > 1)
            realClips(argv[1]);
        std::cout << "Spear retarget validation passed: " << checks << " assertions\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
