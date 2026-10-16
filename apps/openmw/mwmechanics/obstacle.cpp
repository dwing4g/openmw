#include "obstacle.hpp"

#include <components/detournavigator/agentbounds.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"

#include "movement.hpp"

namespace MWMechanics
{
    namespace
    {
        // NOTE: determined empirically but probably need further tweaking
        constexpr float distanceSameSpot = 0.5f;
        constexpr float durationSameSpot = 1.5f;
        constexpr float durationToEvade = 1;
    }

    bool proximityToDoor(const MWWorld::Ptr& actor, float minDist)
    {
        if (getNearbyDoor(actor, minDist).isEmpty())
            return false;
        else
            return true;
    }

    struct GetNearbyDoorVisitor
    {
        MWWorld::Ptr mResult;

        GetNearbyDoorVisitor(const MWWorld::Ptr& actor, const float minDist)
            : mPos(actor.getRefData().getPosition().asVec3())
            , mDir(actor.getRefData().getBaseNode()->getAttitude() * osg::Vec3f(0, 1, 0))
            , mMinDist(minDist)
        {
            mPos.z() = 0;
            mDir.normalize();
        }

        bool operator()(const MWWorld::Ptr& ptr)
        {
            MWWorld::LiveCellRef<ESM::Door>& ref = *static_cast<MWWorld::LiveCellRef<ESM::Door>*>(ptr.getBase());
            if (!ptr.getRefData().isEnabled() || ref.isDeleted())
                return true;

            if (ptr.getClass().getDoorState(ptr) != MWWorld::DoorState::Idle)
                return true;

            const float doorRot = ref.mData.getPosition().rot[2] - ptr.getCellRef().getPosition().rot[2];
            if (doorRot != 0)
                return true;

            osg::Vec3f doorPos(ref.mData.getPosition().asVec3());
            doorPos.z() = 0;

            osg::Vec3f actorToDoor = doorPos - mPos;
            // Door is not close enough
            if (actorToDoor.length2() > mMinDist * mMinDist)
                return true;

            actorToDoor.normalize();
            const float angle = std::acos(mDir * actorToDoor);

            // Allow 60 degrees angle between actor and door
            if (angle < -osg::PI / 3 || angle > osg::PI / 3)
                return true;

            mResult = ptr;
            return false; // found, stop searching
        }

    private:
        osg::Vec3f mPos, mDir;
        float mMinDist;
    };

    const MWWorld::Ptr getNearbyDoor(const MWWorld::Ptr& actor, float minDist)
    {
        GetNearbyDoorVisitor visitor(actor, minDist);
        actor.getCell()->forEachType<ESM::Door>(visitor);
        return visitor.mResult;
    }

    ObstacleCheck::ObstacleCheck() = default;

    void ObstacleCheck::clear()
    {
        mWalkState = WalkState::Initial;
    }

    bool ObstacleCheck::isEvading() const
    {
        return mWalkState == WalkState::Evade;
    }

    /*
     * input   - actor, duration (time since last check)
     * output  - true if evasive action needs to be taken
     *
     * Walking state transitions (player greeting check not shown):
     *
     * Initial ----> Norm  <--------> CheckStuck -------> Evade ---+
     *               ^ ^ | f             ^   |       t    ^   |    |
     *               | | |               |   |            |   |    |
     *               | +-+               +---+            +---+    | u
     *               | any                < t              < u     |
     *               +---------------------------------------------+
     *
     * f = one reaction time
     * t = how long before considered stuck
     * u = how long to move sideways
     *
     */
    void ObstacleCheck::update(const MWWorld::Ptr& actor, const osg::Vec3f& destination, float duration,
        MWWorld::MovementDirectionFlags supportedMovementDirection)
    {
        const auto position = actor.getRefData().getPosition().asVec3();

        if (mWalkState == WalkState::Initial)
        {
            mWalkState = WalkState::Norm;
            mStateDuration = 0;
            mPrev = position;
            mInitialDistance = (destination - position).length();
            mDestination = destination;
            return;
        }

        if (mWalkState != WalkState::Evade)
        {
            if (mDestination != destination)
            {
                mInitialDistance = (destination - mPrev).length();
                mDestination = destination;
            }

            const float distSameSpot = distanceSameSpot * actor.getClass().getCurrentSpeed(actor) * duration;
            const float prevDistance = (destination - mPrev).length();
            const float currentDistance = (destination - position).length();
            const float movedDistance = prevDistance - currentDistance;
            const float movedFromInitialDistance = mInitialDistance - currentDistance;

            mPrev = position;

            if (movedDistance >= distSameSpot && movedFromInitialDistance >= distSameSpot)
            {
                mWalkState = WalkState::Norm;
                mStateDuration = 0;
                return;
            }

            if (mWalkState == WalkState::Norm)
            {
                mWalkState = WalkState::CheckStuck;
                mStateDuration = duration;
                mInitialDistance = (destination - position).length();
                return;
            }

            mStateDuration += duration;
            if (mStateDuration < durationSameSpot)
            {
                return;
            }

            mWalkState = mEvadeDirection.advance(supportedMovementDirection) ? WalkState::Evade : WalkState::Norm;
            mStateDuration = 0;
            return;
        }

        mStateDuration += duration;
        if (mStateDuration >= durationToEvade)
        {
            // tried to evade, assume all is ok and start again
            mWalkState = WalkState::Norm;
            mStateDuration = 0;
            mPrev = position;
        }
    }

    void ObstacleCheck::takeEvasiveAction(MWMechanics::Movement& actorMovement) const
    {
        actorMovement.mPosition[0] = mEvadeDirection.get().mX;
        actorMovement.mPosition[1] = mEvadeDirection.get().mY;
    }
}
