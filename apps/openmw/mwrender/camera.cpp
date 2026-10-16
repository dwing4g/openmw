#include "camera.hpp"

#include <osg/Camera>

#include <cmath>
#include <limits>
#include <vector>

#include <components/misc/mathutil.hpp>
#include <components/sceneutil/nodecallback.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/refdata.hpp"

#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/movement.hpp"

#include "../mwphysics/raycasting.hpp"

#include "npcanimation.hpp"

namespace
{

    // The reference simulation uses metres. Morrowind uses roughly 70 units/metre.
    constexpr float sCombatLockOnRange = 15.f * 70.f;
    constexpr float sCombatLockBreakRange = 22.f * 70.f;

    bool isCombatLockTarget(const MWWorld::Ptr& actor)
    {
        return !actor.isEmpty() && actor.isInCell() && actor.getClass().isActor() && !actor.getBase()->isDeleted()
            && actor.getRefData().isEnabled() && actor.getRefData().getBaseNode()
            && !actor.getClass().getCreatureStats(actor).isDead();
    }

    class UpdateRenderCameraCallback : public SceneUtil::NodeCallback<UpdateRenderCameraCallback, osg::Camera*>
    {
    public:
        UpdateRenderCameraCallback(MWRender::Camera* cam)
            : mCamera(cam)
        {
        }

        void operator()(osg::Camera* cam, osg::NodeVisitor* nv)
        {
            // traverse first to update animations, in case the camera is attached to an animated node
            traverse(cam, nv);

            mCamera->updateCamera(cam);
        }

    private:
        MWRender::Camera* mCamera;
    };

}

namespace MWRender
{

    Camera::Camera(osg::Camera* camera)
        : mHeightScale(1.f)
        , mCollisionType(
              (MWPhysics::CollisionType::CollisionType_Default & ~MWPhysics::CollisionType::CollisionType_Actor)
              | MWPhysics::CollisionType_CameraOnly)
        , mCamera(camera)
        , mAnimation(nullptr)
        , mFirstPersonView(true)
        , mMode(Mode::FirstPerson)
        , mVanityAllowed(true)
        , mDeferredRotationAllowed(true)
        , mProcessViewChange(false)
        , mHeight(124.f)
        , mPitch(0.f)
        , mYaw(0.f)
        , mRoll(0.f)
        , mCameraDistance(0.f)
        , mPreferredCameraDistance(0.f)
        , mFocalPointCurrentOffset(osg::Vec2d())
        , mFocalPointTargetOffset(osg::Vec2d())
        , mFocalPointTransitionSpeedCoef(1.f)
        , mSkipFocalPointTransition(true)
        , mPreviousTransitionInfluence(0.f)
        , mShowCrosshair(false)
        , mDeferredRotation(osg::Vec3f())
        , mDeferredRotationDisabled(false)
    {
        mUpdateCallback = new UpdateRenderCameraCallback(this);
        mCamera->addUpdateCallback(mUpdateCallback);
    }

    Camera::~Camera()
    {
        mCamera->removeUpdateCallback(mUpdateCallback);
    }

    osg::Vec3d Camera::calculateTrackedPosition() const
    {
        if (!mTrackingNode)
            return osg::Vec3d();
        osg::NodePathList nodepaths = mTrackingNode->getParentalNodePaths();
        if (nodepaths.empty())
            return osg::Vec3d();
        osg::Matrix worldMat = osg::computeLocalToWorld(nodepaths[0]);
        osg::Vec3d res = worldMat.getTrans();
        if (mMode != Mode::FirstPerson)
            res.z() += mHeight * mHeightScale;
        return res;
    }

    osg::Vec3d Camera::getFocalPointOffset() const
    {
        osg::Vec3d offset;
        offset.x() = mFocalPointCurrentOffset.x() * cos(mYaw);
        offset.y() = mFocalPointCurrentOffset.x() * sin(mYaw);
        offset.z() = mFocalPointCurrentOffset.y();
        return offset;
    }

    void Camera::updateCamera(osg::Camera* cam)
    {
        osg::Quat orient = getOrient();
        osg::Vec3d forward = orient * osg::Vec3d(0, 1, 0);
        osg::Vec3d up = orient * osg::Vec3d(0, 0, 1);

        osg::Vec3d pos = mPosition;
        if (mMode == Mode::FirstPerson)
        {
            // It is a hack. Camera position depends on neck animation.
            // Animations are updated in OSG cull traversal and in order to avoid 1 frame delay we
            // recalculate the position here. Note that it becomes different from mPosition that
            // is used in other parts of the code.
            // TODO: detach camera from OSG animation and get rid of this hack.
            osg::Vec3d recalculatedTrackedPosition = calculateTrackedPosition();
            pos = calculateFirstPersonPosition(recalculatedTrackedPosition);
        }
        cam->setViewMatrixAsLookAt(pos, pos + forward, up);
        mViewMatrix = cam->getViewMatrix();
        mProjectionMatrix = cam->getProjectionMatrix();
    }

    void Camera::update(float duration, bool paused)
    {
        mLockPitch = mLockYaw = false;
        if (mQueuedMode && mAnimation->upperBodyReady())
            setMode(*mQueuedMode);
        if (mProcessViewChange)
            processViewChange();

        // only show the crosshair in game mode
        MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();
        wm->showCrosshair(!wm->isGuiMode() && mShowCrosshair);

        if (!paused)
        {
            updateCombatLockOn(duration);
            updateFocalPointOffset(duration);
        }
        updatePosition();
    }

    MWWorld::Ptr Camera::getCombatLockOnTarget() const
    {
        if (!Settings::game().mEldenRingCombat || mTrackingPtr.isEmpty()
            || mTrackingPtr != MWBase::Environment::get().getWorld()->getPlayerPtr()
            || mTrackingPtr.getClass().getCreatureStats(mTrackingPtr).isDead())
            return {};
        const MWWorld::Ptr target = mCombatLockOnTarget.ptrOrEmpty();
        if (!isCombatLockTarget(target))
            return {};
        const osg::Vec3f delta
            = target.getRefData().getPosition().asVec3() - mTrackingPtr.getRefData().getPosition().asVec3();
        return delta.length2() <= sCombatLockBreakRange * sCombatLockBreakRange ? target : MWWorld::Ptr{};
    }

    void Camera::clearCombatLockOn()
    {
        mCombatLockOnTarget = MWWorld::SafePtr{};
        mCombatRecenterYaw.reset();
        mCombatTargetOccludedTime = 0.f;
    }

    void Camera::toggleCombatLockOn()
    {
        if (!Settings::game().mEldenRingCombat || mTrackingPtr.isEmpty()
            || mTrackingPtr != MWBase::Environment::get().getWorld()->getPlayerPtr()
            || mTrackingPtr.getClass().getCreatureStats(mTrackingPtr).isDead()
            || (mMode != Mode::FirstPerson && mMode != Mode::ThirdPerson))
            return;
        if (!getCombatLockOnTarget().isEmpty())
        {
            clearCombatLockOn();
            return;
        }

        clearCombatLockOn();
        const auto world = MWBase::Environment::get().getWorld();
        const osg::Vec3f position = mTrackingPtr.getRefData().getPosition().asVec3();
        std::vector<MWWorld::Ptr> actors;
        MWBase::Environment::get().getMechanicsManager()->getActorsInRange(position, sCombatLockOnRange, actors);
        const osg::Vec3f forward = getOrient() * osg::Vec3f(0, 1, 0);
        float bestScore = std::numeric_limits<float>::max();
        MWWorld::Ptr best;
        for (const MWWorld::Ptr& actor : actors)
        {
            if (actor == mTrackingPtr || !isCombatLockTarget(actor)
                || !world->getRayCasting()->getLineOfSight(mTrackingPtr, actor))
                continue;
            osg::Vec3f direction = actor.getRefData().getPosition().asVec3() - position;
            direction.z() = 0.f;
            const float distance = direction.normalize();
            const float facing = direction * forward;
            if (distance < 1.f || facing < 0.35f)
                continue;
            const float score = (1.f - facing) * 4.f + distance / sCombatLockOnRange;
            if (score < bestScore)
            {
                bestScore = score;
                best = actor;
            }
        }
        if (!best.isEmpty())
        {
            mCombatLockOnTarget = MWWorld::SafePtr(best);
            setMode(Mode::ThirdPerson, false);
        }
        else
            mCombatRecenterYaw = -mTrackingPtr.getRefData().getPosition().rot[2];
    }

    void Camera::updateCombatLockOn(float duration)
    {
        if (!Settings::game().mEldenRingCombat)
        {
            clearCombatLockOn();
            return;
        }
        const MWWorld::Ptr target = getCombatLockOnTarget();
        if (!target.isEmpty())
        {
            const auto world = MWBase::Environment::get().getWorld();
            if (world->getRayCasting()->getLineOfSight(mTrackingPtr, target))
                mCombatTargetOccludedTime = 0.f;
            else
                mCombatTargetOccludedTime += duration;
            if (mCombatTargetOccludedTime > 0.75f)
            {
                clearCombatLockOn();
                return;
            }
            osg::Vec3f direction = target.getRefData().getPosition().asVec3()
                - mTrackingPtr.getRefData().getPosition().asVec3();
            direction.z() += world->getHalfExtents(target).z() - mHeight * mHeightScale;
            const float horizontal = std::hypot(direction.x(), direction.y());
            if (horizontal < 1.f)
                return;
            const float desiredYaw = -std::atan2(direction.x(), direction.y());
            const float desiredPitch = std::clamp(std::atan2(direction.z(), horizontal), -1.2f, 0.7f);
            const float ease = 1.f - std::exp(-8.f * std::max(duration, 0.f));
            setYaw(mYaw + static_cast<float>(Misc::normalizeAngle(desiredYaw - mYaw)) * ease, true);
            setPitch(mPitch + (desiredPitch - mPitch) * ease, true);
            return;
        }
        mCombatLockOnTarget = MWWorld::SafePtr{};
        mCombatTargetOccludedTime = 0.f;
        if (mCombatRecenterYaw && mMode != Mode::Static && !mTrackingPtr.isEmpty())
        {
            const float difference = static_cast<float>(Misc::normalizeAngle(*mCombatRecenterYaw - mYaw));
            const float ease = 1.f - std::exp(-12.f * std::max(duration, 0.f));
            setYaw(mYaw + difference * ease);
            setPitch(mPitch + (-0.24f - mPitch) * ease);
            if (std::abs(difference) < 0.01f)
                mCombatRecenterYaw.reset();
        }
    }

    osg::Vec3d Camera::calculateFirstPersonPosition(const osg::Vec3d& trackedPosition) const
    {
        osg::Vec3d res = trackedPosition;
        osg::Vec2f horizontalOffset
            = Misc::rotateVec2f(osg::Vec2f(mFirstPersonOffset.x(), mFirstPersonOffset.y()), mYaw);
        res.x() += horizontalOffset.x();
        res.y() += horizontalOffset.y();
        res.z() += mFirstPersonOffset.z();
        return res;
    }

    void Camera::updatePosition()
    {
        mTrackedPosition = calculateTrackedPosition();
        if (mMode == Mode::Static)
            return;
        if (mMode == Mode::FirstPerson)
        {
            mPosition = calculateFirstPersonPosition(mTrackedPosition);
            mCameraDistance = 0;
            return;
        }

        constexpr float cameraObstacleLimit = 5.0f;
        constexpr float focalObstacleLimit = 10.f;

        const auto* rayCasting = MWBase::Environment::get().getWorld()->getRayCasting();

        // Adjust focal point to prevent clipping.
        osg::Vec3d focalOffset = getFocalPointOffset();
        osg::Vec3d focal = mTrackedPosition + focalOffset;
        focalOffset.z() += 10.f; // Needed to avoid camera clipping through the ceiling because
                                 // character's head can be a bit higher than the collision area.
        double offsetLen = focalOffset.length();
        if (offsetLen > 0)
        {
            MWPhysics::RayCastingResult result
                = rayCasting->castSphere(focal - focalOffset, focal, focalObstacleLimit, mCollisionType);
            if (result.mHit)
            {
                double adjustmentCoef
                    = -(result.mHitPos + result.mHitNormal * focalObstacleLimit - focal).length() / offsetLen;
                focal += focalOffset * std::max(-1.0, adjustmentCoef);
            }
        }

        // Adjust camera distance.
        mCameraDistance = mPreferredCameraDistance;
        osg::Quat orient
            = osg::Quat(mPitch + mExtraPitch, osg::Vec3d(1, 0, 0)) * osg::Quat(mYaw + mExtraYaw, osg::Vec3d(0, 0, 1));
        osg::Vec3d offset = orient * osg::Vec3d(0.f, -mCameraDistance, 0.f);
        MWPhysics::RayCastingResult result
            = rayCasting->castSphere(focal, focal + offset, cameraObstacleLimit, mCollisionType);
        if (result.mHit)
        {
            mCameraDistance = (result.mHitPos + result.mHitNormal * cameraObstacleLimit - focal).length();
            offset = orient * osg::Vec3d(0.f, -mCameraDistance, 0.f);
        }

        mPosition = focal + offset;
    }

    osg::Quat Camera::getOrient() const
    {
        return osg::Quat(mRoll + mExtraRoll, osg::Vec3d(0, 1, 0)) * osg::Quat(mPitch + mExtraPitch, osg::Vec3d(1, 0, 0))
            * osg::Quat(mYaw + mExtraYaw, osg::Vec3d(0, 0, 1));
    }

    void Camera::setMode(Mode newMode, bool force)
    {
        if (mMode == newMode)
        {
            mQueuedMode = std::nullopt;
            if (newMode != Mode::ThirdPerson)
                clearCombatLockOn();
            return;
        }
        Mode oldMode = mMode;
        if (!force && (newMode == Mode::FirstPerson || oldMode == Mode::FirstPerson) && mAnimation
            && !mAnimation->upperBodyReady())
        {
            // Changing the view will stop all playing animations, so if we are playing
            // anything important, queue the view change for later
            mQueuedMode = newMode;
            return;
        }
        mMode = newMode;
        if (newMode != Mode::ThirdPerson)
            clearCombatLockOn();
        mQueuedMode = std::nullopt;
        if (newMode == Mode::FirstPerson)
            mFirstPersonView = true;
        else if (newMode == Mode::ThirdPerson)
            mFirstPersonView = false;
        calculateDeferredRotation();
        if (oldMode == Mode::FirstPerson || newMode == Mode::FirstPerson)
        {
            instantTransition();
            mProcessViewChange = true;
        }
    }

    void Camera::setFocalPointTargetOffset(const osg::Vec2d& v)
    {
        mFocalPointTargetOffset = v;
        mPreviousTransitionSpeed = mFocalPointTransitionSpeed;
        mPreviousTransitionInfluence = 1.0f;
    }

    void Camera::updateFocalPointOffset(float duration)
    {
        if (duration <= 0)
            return;

        if (mSkipFocalPointTransition)
        {
            mSkipFocalPointTransition = false;
            mPreviousExtraOffset = osg::Vec2d();
            mPreviousTransitionInfluence = 0.f;
            mFocalPointCurrentOffset = mFocalPointTargetOffset;
        }

        osg::Vec2d oldOffset = mFocalPointCurrentOffset;

        if (mPreviousTransitionInfluence > 0)
        {
            mFocalPointCurrentOffset -= mPreviousExtraOffset;
            mPreviousExtraOffset
                = mPreviousExtraOffset / mPreviousTransitionInfluence + mPreviousTransitionSpeed * duration;
            mPreviousTransitionInfluence
                = std::max(0.f, mPreviousTransitionInfluence - duration * mFocalPointTransitionSpeedCoef);
            mPreviousExtraOffset *= mPreviousTransitionInfluence;
            mFocalPointCurrentOffset += mPreviousExtraOffset;
        }

        osg::Vec2d delta = mFocalPointTargetOffset - mFocalPointCurrentOffset;
        if (delta.length2() > 0)
        {
            double coef = duration * (1.0 + 5.0 / delta.length()) * mFocalPointTransitionSpeedCoef
                * (1.0f - mPreviousTransitionInfluence);
            mFocalPointCurrentOffset += delta * std::min(coef, 1.0);
        }
        else
        {
            mPreviousExtraOffset = osg::Vec2d();
            mPreviousTransitionInfluence = 0.f;
        }

        mFocalPointTransitionSpeed = (mFocalPointCurrentOffset - oldOffset) / duration;
    }

    void Camera::toggleViewMode(bool force)
    {
        setMode(mFirstPersonView ? Mode::ThirdPerson : Mode::FirstPerson, force);
    }

    bool Camera::toggleVanityMode(bool enable)
    {
        if (!enable)
            setMode(mFirstPersonView ? Mode::FirstPerson : Mode::ThirdPerson, false);
        else if (mVanityAllowed)
            setMode(Mode::Vanity, false);
        return (mMode == Mode::Vanity) == enable;
    }

    void Camera::setSneakOffset(float offset)
    {
        mAnimation->setFirstPersonOffset(osg::Vec3f(0, 0, -offset));
    }

    void Camera::setYaw(float angle, bool force)
    {
        if (!mLockYaw || force)
            mYaw = static_cast<float>(Misc::normalizeAngle(angle));
        if (force)
            mLockYaw = true;
    }

    void Camera::setPitch(float angle, bool force)
    {
        const float epsilon = 0.000001f;
        float limit = static_cast<float>(osg::PI_2) - epsilon;
        if (!mLockPitch || force)
            mPitch = std::clamp(angle, -limit, limit);
        if (force)
            mLockPitch = true;
    }

    void Camera::setStaticPosition(const osg::Vec3d& pos)
    {
        if (mMode != Mode::Static)
            throw std::runtime_error("setStaticPosition can be used only if camera is in Static mode");
        mPosition = pos;
    }

    void Camera::attachTo(const MWWorld::Ptr& ptr)
    {
        if (mTrackingPtr != ptr)
            clearCombatLockOn();
        mTrackingPtr = ptr;
        if (mMode == Mode::Static || ptr.isEmpty())
            return;
        // Scripts read position before the first update.
        mTrackedPosition = ptr.getRefData().getPosition().asVec3();
        mTrackedPosition.z() += mHeight;
        mPosition = mTrackedPosition;
    }

    void Camera::setAnimation(NpcAnimation* anim)
    {
        mAnimation = anim;
        mProcessViewChange = true;
    }

    void Camera::processViewChange()
    {
        if (mTrackingPtr.isEmpty())
            return;
        if (mMode == Mode::FirstPerson)
        {
            mAnimation->setViewMode(NpcAnimation::VM_FirstPerson);
            mTrackingNode = mAnimation->getNode("Camera");
            if (!mTrackingNode)
                mTrackingNode = mAnimation->getNode("Head");
            mHeightScale = 1.f;
        }
        else
        {
            mAnimation->setViewMode(NpcAnimation::VM_Normal);
            SceneUtil::PositionAttitudeTransform* transform = mTrackingPtr.getRefData().getBaseNode();
            mTrackingNode = transform;
            if (transform)
                mHeightScale = transform->getScale().z();
            else
                mHeightScale = 1.f;
        }
        mProcessViewChange = false;
    }

    void Camera::applyDeferredPreviewRotationToPlayer(float dt)
    {
        if (mMode != Mode::ThirdPerson || mTrackingPtr.isEmpty())
            return;

        osg::Vec3f rot = mDeferredRotation;
        float delta = rot.normalize();
        delta = std::min(delta, (delta + 1.f) * 3 * dt);
        rot *= delta;
        mDeferredRotation -= rot;

        if (mDeferredRotationDisabled)
        {
            mDeferredRotationDisabled = delta > 0.0001;
            rotateCameraToTrackingPtr();
            return;
        }

        auto& movement = mTrackingPtr.getClass().getMovementSettings(mTrackingPtr);
        movement.mRotation[0] += rot.x();
        movement.mRotation[1] += rot.y();
        movement.mRotation[2] += rot.z();
        if (std::abs(mDeferredRotation.z()) > 0.0001)
        {
            float s = std::sin(mDeferredRotation.z());
            float c = std::cos(mDeferredRotation.z());
            float x = movement.mPosition[0];
            float y = movement.mPosition[1];
            movement.mPosition[0] = x * c + y * s;
            movement.mPosition[1] = x * -s + y * c;
        }
    }

    void Camera::rotateCameraToTrackingPtr()
    {
        if (mMode == Mode::Static || mTrackingPtr.isEmpty())
            return;
        setPitch(-mTrackingPtr.getRefData().getPosition().rot[0] - mDeferredRotation.x());
        setYaw(-mTrackingPtr.getRefData().getPosition().rot[2] - mDeferredRotation.z());
    }

    void Camera::instantTransition()
    {
        mSkipFocalPointTransition = true;
        mDeferredRotationDisabled = false;
        mDeferredRotation = osg::Vec3f();
        rotateCameraToTrackingPtr();
    }

    void Camera::calculateDeferredRotation()
    {
        if (mMode == Mode::Static)
        {
            mDeferredRotation = osg::Vec3f();
            return;
        }
        MWWorld::Ptr ptr = mTrackingPtr;
        if (mMode == Mode::Preview || mMode == Mode::Vanity || ptr.isEmpty())
            return;
        if (mFirstPersonView)
        {
            instantTransition();
            return;
        }

        mDeferredRotation.x()
            = static_cast<float>(Misc::normalizeAngle(-ptr.getRefData().getPosition().rot[0] - mPitch));
        mDeferredRotation.z() = static_cast<float>(Misc::normalizeAngle(-ptr.getRefData().getPosition().rot[2] - mYaw));
        if (!mDeferredRotationAllowed)
            mDeferredRotationDisabled = true;
    }

}
