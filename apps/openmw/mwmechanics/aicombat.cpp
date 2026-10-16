#include "aicombat.hpp"

#include <algorithm>
#include <cmath>

#include <components/detournavigator/navigatorutils.hpp>
#include <components/esm3/aisequence.hpp>
#include <components/misc/coordinateconverter.hpp>
#include <components/misc/mathutil.hpp>
#include <components/misc/pathgridutils.hpp>
#include <components/misc/rng.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/settings/values.hpp>

#include "../mwphysics/raycasting.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"

#include "../mwbase/dialoguemanager.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/world.hpp"

#include "actorutil.hpp"
#include "aicombataction.hpp"
#include "character.hpp"
#include "combat.hpp"
#include "creaturestats.hpp"
#include "eldencombat.hpp"
#include "movement.hpp"
#include "pathgrid.hpp"
#include "steering.hpp"
#include "weapontype.hpp"

namespace
{

    // chooses an attack depending on probability to avoid uniformity
    std::string_view chooseBestAttack(const ESM::Weapon* weapon);

    bool hitAttemptMatchesTarget(const MWWorld::Ptr& actor, const MWWorld::Ptr& target)
    {
        ESM::RefNum hitNum = actor.getClass().getCreatureStats(actor).getHitAttemptActor();
        return hitNum.isSet() && target.getCellRef().getRefNum() == hitNum;
    }

    bool safeEldenDodge(const MWWorld::Ptr& actor, const osg::Vec3f& localDirection, float meters)
    {
        const auto world = MWBase::Environment::get().getWorld();
        if (!world->isOnGround(actor) || world->isSwimming(actor) || world->isFlying(actor)
            || !actor.getRefData().getBaseNode())
            return false;
        const osg::Vec3f halfExtents = world->getHalfExtents(actor);
        const osg::Vec3f actorPosition = actor.getRefData().getPosition().asVec3();
        const osg::Vec3f direction = actor.getRefData().getBaseNode()->getAttitude() * localDirection;
        const osg::Vec3f source = actorPosition + osg::Vec3f(0.f, 0.f, 0.75f * halfExtents.z());
        const float distance = meters * 70.f + std::max(halfExtents.x(), halfExtents.y());
        const int mask = MWPhysics::CollisionType_World | MWPhysics::CollisionType_HeightMap
            | MWPhysics::CollisionType_Door;
        const auto* rayCasting = world->getRayCasting();
        if (rayCasting->castRay(source, source + direction * distance, mask).mHit)
            return false;
        // Sample the whole route, so a roll cannot cross a gap whose far side has ground.
        for (int sample = 1; sample <= 4; ++sample)
        {
            const osg::Vec3f at = source + direction * (distance * sample / 4.f);
            const osg::Vec3f below = at - osg::Vec3f(0.f, 0.f, 0.75f * halfExtents.z() + 48.f);
            if (!rayCasting->castRay(at, below, mask).mHit)
                return false;
        }
        return true;
    }


}

namespace MWMechanics
{
    AiCombat::AiCombat(const MWWorld::Ptr& actor)
    {
        mTargetActor = actor.getCellRef().getRefNum();
    }

    AiCombat::AiCombat(const ESM::AiSequence::AiCombat* combat)
    {
        mTargetActor = combat->mTargetActor;
    }

    MWWorld::Ptr AiCombat::getTarget() const
    {
        const MWWorld::Ptr target = AiPackage::getTarget();
        if (!target.isEmpty() && !target.getClass().isActor())
            return {};
        return target;
    }

    void AiCombat::init() {}

    /*
     * Current AiCombat movement states (as of 0.29.0), ignoring the details of the
     * attack states such as CombatMove, Strike and ReadyToAttack:
     *
     *    +----(within strike range)----->attack--(beyond strike range)-->follow
     *    |                                 | ^                            | |
     *    |                                 | |                            | |
     *  pursue<---(beyond follow range)-----+ +----(within strike range)---+ |
     *    ^                                                                  |
     *    |                                                                  |
     *    +-------------------------(beyond follow range)--------------------+
     *
     *
     * Below diagram is high level only, the code detail is a little different
     * (but including those detail will just complicate the diagram w/o adding much)
     *
     *    +----------(same)-------------->attack---------(same)---------->follow
     *    |                                 |^^                            |||
     *    |                                 |||                            |||
     *    |       +--(same)-----------------+|+----------(same)------------+||
     *    |       |                          |                              ||
     *    |       |                          | (in range)                   ||
     *    |   <---+         (too far)        |                              ||
     *  pursue<-------------------------[door open]<-----+                  ||
     *    ^^^                                            |                  ||
     *    |||                                            |                  ||
     *    ||+----------evade-----+                       |                  ||
     *    ||                     |    [closed door]      |                  ||
     *    |+----> maybe stuck, check --------------> back up, check door    ||
     *    |         ^   |   ^                          |   ^                ||
     *    |         |   |   |                          |   |                ||
     *    |         |   +---+                          +---+                ||
     *    |         +-------------------------------------------------------+|
     *    |                                                                  |
     *    +---------------------------(same)---------------------------------+
     *
     * FIXME:
     *
     * The new scheme is way too complicated, should really be implemented as a
     * proper state machine.
     *
     * TODO:
     *
     * Use the observer pattern to coordinate attacks, provide intelligence on
     * whether the target was hit, etc.
     */

    bool AiCombat::execute(
        const MWWorld::Ptr& actor, CharacterController& characterController, AiState& state, float duration)
    {
        // Get or create temporary storage
        AiCombatStorage& storage = state.get<AiCombatStorage>();

        const MWWorld::Class& actorClass = actor.getClass();
        if (!actorClass.isActor())
            return true;

        MWMechanics::CreatureStats& actorStats = actorClass.getCreatureStats(actor);

        // No combat for dead creatures
        if (actorStats.isDead())
        {
            storage.stopEldenAttack(actor);
            return true;
        }

        const MWWorld::Ptr target = getTarget(); // The target to follow

        // Stop if the target doesn't exist
        if (target.isEmpty() || !target.getCellRef().getCount() || !target.getRefData().isEnabled()
            || target.getClass().getCreatureStats(target).isDead() || !target.getRefData().getBaseNode())
        {
            storage.stopEldenAttack(actor);
            return true;
        }

        if (actor == target) // This should never happen.
        {
            storage.stopEldenAttack(actor);
            return true;
        }

        // No actions for totally static creatures
        if (!actorClass.isMobile(actor))
        {
            storage.stopEldenAttack(actor);
            storage.mFleeState = AiCombatStorage::FleeState_Idle;
            return false;
        }

        if (actorStats.isParalyzed() || actorStats.getKnockedDown())
        {
            // Knockdown also represents Elden guard/stance breaks. Preserve the
            // interrupted chain's pause instead of restarting offense on standing.
            const bool preserveTactics = useEldenCombat(actor);
            if (preserveTactics)
                storage.mEldenTactics.update(actorStats.getEldenCombat(), 0.f,
                    Settings::game().mEldenRingAiAttackDelay * storage.mEldenAttackDelayScale);
            storage.stopEldenAttack(actor, !preserveTactics);
            return false;
        }

        if (!storage.isFleeing())
        {
            const ESM::Weapon* weapon = nullptr;
            bool isRangedCombat = false;

            if (storage.mCurrentAction.get()) // need to wait to init action with its attack range
            {
                // Update every frame. UpdateLOS uses a timer, so the LOS check does not happen every frame.
                updateLOS(actor, target, duration, storage);
                weapon = storage.mCurrentAction->getWeapon();
                storage.mCurrentAction->getCombatRange(isRangedCombat);
                // Recheck reach before choosing steering, even between tactical
                // reaction ticks. Reaching a projected path endpoint is not the
                // same as reaching the target (especially above a slope/ledge).
                storage.mReadyToAttack = storage.mCurrentAction->isAttackingOrSpell() && storage.mLOS
                    && (isRangedCombat ? getDistanceToBounds(actor, target) <= storage.mAttackRange
                                       : isInMeleeReach(actor, target, storage.mAttackRange));
                if (!storage.mReadyToAttack)
                {
                    storage.mShouldApproach = true;
                    storage.stopCombatMove();
                    const float targetReachedTolerance
                        = storage.mLOS && !storage.mUseCustomDestination ? storage.mAttackRange : 0.0f;
                    const osg::Vec3f destination = storage.mUseCustomDestination
                        ? storage.mCustomDestination
                        : target.getRefData().getPosition().asVec3();
                    pathTo(actor, destination, duration,
                        characterController.getSupportedMovementDirections(), targetReachedTolerance);
                }
                else
                {
                    // Attack recovery is an intentional stop, not a failed
                    // attempt to walk to the next path point.
                    mObstacleCheck.clear();
                }

                if (!isRangedCombat && storage.mShouldApproach && storage.mReadyToAttack)
                {
                    if (getDistanceToBounds(actor, target) > 64.f)
                    {
                        storage.stopCombatMove();
                        storage.mMovement.mPosition[1] = 1.f;
                    }
                    else
                    {
                        storage.mShouldApproach = false;
                        storage.mMovement.mPosition[1] = 0.f;
                    }
                }
            }

            storage.updateCombatMove(duration);
            storage.mRotateMove = false;
            if (storage.mReadyToAttack)
            {
                MWBase::World* world = MWBase::Environment::get().getWorld();
                const osg::Vec3f actorPos(actor.getRefData().getPosition().asVec3());
                const osg::Vec3f targetPos(target.getRefData().getPosition().asVec3());
                const osg::Vec3f targetRelativePos = isRangedCombat
                    ? world->aimToTarget(actor, target, true)
                    : targetPos - actorPos + osg::Vec3f(0.f, 0.f,
                        world->getHalfExtents(target).z() - world->getHalfExtents(actor).z());
                storage.mMovement.mRotation[0] = getXAngleToDir(targetRelativePos);
                // using targetRelativePos results in spastic movements since the head is animated
                storage.mMovement.mRotation[2] = getZAngleToDir(targetPos - actorPos);
                updateActorsMovement(actor, duration, storage);
            }

            // Turning must not freeze reaction/path replanning timers or charge
            // releases. Only starting a native attack waits for the turn.
            if (!storage.updateEldenAttack(actor, target, characterController, isRangedCombat, duration)
                && !storage.mRotateMove)
                storage.updateAttack(actor, characterController, weapon, isRangedCombat, duration);
        }
        else
        {
            storage.stopEldenAttack(actor);
            updateFleeing(actor, target, duration, characterController.getSupportedMovementDirections(), storage);
        }
        storage.mActionCooldown -= duration;

        if (storage.mReaction.update(duration) == Misc::TimerStatus::Waiting)
            return false;

        return attack(actor, target, storage, characterController);
    }

    bool AiCombat::attack(const MWWorld::Ptr& actor, const MWWorld::Ptr& target, AiCombatStorage& storage,
        CharacterController& characterController)
    {
        const MWWorld::CellStore*& currentCell = storage.mCell;
        bool cellChange = currentCell && (actor.getCell() != currentCell);
        if (!currentCell || cellChange)
        {
            currentCell = actor.getCell();
        }

        const MWWorld::Class& actorClass = actor.getClass();
        MWMechanics::CreatureStats& stats = actorClass.getCreatureStats(actor);

        bool forceFlee = false;
        if (!canFight(actor, target))
        {
            storage.stopEldenAttack(actor);
            storage.stopAttack();
            stats.setAttackingOrSpell(false);
            storage.mActionCooldown = 0.f;
            // Continue combat if target is player or player follower/escorter and an attack has been attempted
            const auto& playerFollowersAndEscorters
                = MWBase::Environment::get().getMechanicsManager()->getActorsSidingWith(MWMechanics::getPlayer());
            bool targetSidesWithPlayer
                = (std::find(playerFollowersAndEscorters.begin(), playerFollowersAndEscorters.end(), target)
                    != playerFollowersAndEscorters.end());
            if ((target == MWMechanics::getPlayer() || targetSidesWithPlayer)
                && (hitAttemptMatchesTarget(actor, target) || hitAttemptMatchesTarget(target, actor)))
                forceFlee = true;
            else // Otherwise end combat
                return true;
        }

        stats.setMovementFlag(CreatureStats::Flag_Run, true);
        float& actionCooldown = storage.mActionCooldown;
        std::unique_ptr<Action>& currentAction = storage.mCurrentAction;

        if (!forceFlee)
        {
            if (actionCooldown > 0)
                return false;

            if (characterController.readyToPrepareAttack())
            {
                currentAction = prepareNextAction(actor, target);
                actionCooldown = currentAction->getActionCooldown();
            }
        }
        else
        {
            currentAction = std::make_unique<ActionFlee>();
            actionCooldown = currentAction->getActionCooldown();
        }

        if (!currentAction)
            return false;

        if (storage.isFleeing() != currentAction->isFleeing())
        {
            if (currentAction->isFleeing())
            {
                storage.stopEldenAttack(actor);
                storage.startFleeing();
                MWBase::Environment::get().getDialogueManager()->say(actor, ESM::RefId::stringRefId("flee"));
                return false;
            }
            else
                storage.stopFleeing();
        }

        bool isRangedCombat = false;
        float& rangeAttack = storage.mAttackRange;
        rangeAttack = currentAction->getCombatRange(isRangedCombat);

        ESM::Position pos = actor.getRefData().getPosition();
        const osg::Vec3f vActorPos(pos.asVec3());
        const osg::Vec3f vTargetPos(target.getRefData().getPosition().asVec3());

        float distToTarget = getDistanceToBounds(actor, target);

        storage.mReadyToAttack = currentAction->isAttackingOrSpell() && storage.mLOS
            && (isRangedCombat ? distToTarget <= rangeAttack : isInMeleeReach(actor, target, rangeAttack));

        if (storage.mReadyToAttack)
        {
            storage.startCombatMove(isRangedCombat, distToTarget, rangeAttack, actor, target);
        }
        else
        {
            storage.mShouldApproach = true;
        }

        // If actor uses custom destination it has to try to rebuild path because environment can change
        // (door is opened between actor and target) or target position has changed and current custom destination
        // is not good enough to attack target.
        if (storage.mCurrentAction->isAttackingOrSpell()
            && ((!storage.mReadyToAttack && !mPathFinder.isPathConstructed())
                || (storage.mUseCustomDestination && (storage.mCustomDestination - vTargetPos).length() > rangeAttack)))
        {
            const MWBase::World* world = MWBase::Environment::get().getWorld();
            // Try to build path to the target.
            const auto agentBounds = world->getPathfindingAgentBounds(actor);
            const DetourNavigator::Flags navigatorFlags = getNavigatorFlags(actor);
            const DetourNavigator::AreaCosts areaCosts = getAreaCosts(actor, navigatorFlags);
            const ESM::Pathgrid* pathgrid = world->getStore().get<ESM::Pathgrid>().search(*actor.getCell()->getCell());
            const auto& pathGridGraph = getPathGridGraph(pathgrid);
            mPathFinder.buildPath(actor, vActorPos, vTargetPos, pathGridGraph, agentBounds, navigatorFlags, areaCosts,
                storage.mAttackRange, PathType::Full);

            if (!mPathFinder.isPathConstructed())
            {
                // If there is no path, try to find a point on a line from the actor position to target projected
                // on navmesh to attack the target from there.
                const auto navigator = world->getNavigator();
                const auto hit
                    = DetourNavigator::raycast(*navigator, agentBounds, vActorPos, vTargetPos, navigatorFlags);

                if (hit.has_value() && (*hit - vTargetPos).length() <= rangeAttack)
                {
                    // If the point is close enough, try to find a path to that point.
                    mPathFinder.buildPath(actor, vActorPos, *hit, pathGridGraph, agentBounds, navigatorFlags, areaCosts,
                        storage.mAttackRange, PathType::Full);
                    if (mPathFinder.isPathConstructed())
                    {
                        // If path to that point is found use it as custom destination.
                        storage.mCustomDestination = *hit;
                        storage.mUseCustomDestination = true;
                    }
                }

                if (!mPathFinder.isPathConstructed())
                {
                    storage.mUseCustomDestination = false;
                    storage.stopAttack();
                    stats.setAttackingOrSpell(false);
                    currentAction = std::make_unique<ActionFlee>();
                    actionCooldown = currentAction->getActionCooldown();
                    storage.startFleeing();
                    MWBase::Environment::get().getDialogueManager()->say(actor, ESM::RefId::stringRefId("flee"));
                }
            }
            else
            {
                storage.mUseCustomDestination = false;
            }
        }

        return false;
    }

    void MWMechanics::AiCombat::updateLOS(
        const MWWorld::Ptr& actor, const MWWorld::Ptr& target, float duration, MWMechanics::AiCombatStorage& storage)
    {
        const float losUpdateDuration = 0.5f;
        if (storage.mUpdateLOSTimer <= 0.f)
        {
            storage.mLOS = MWBase::Environment::get().getWorld()->getLOS(actor, target);
            storage.mUpdateLOSTimer = losUpdateDuration;
        }
        else
            storage.mUpdateLOSTimer -= duration;
    }

    void MWMechanics::AiCombat::updateFleeing(const MWWorld::Ptr& actor, const MWWorld::Ptr& target, float duration,
        MWWorld::MovementDirectionFlags supportedMovementDirections, AiCombatStorage& storage)
    {
        const float blindRunDuration = 1.0f;

        updateLOS(actor, target, duration, storage);

        AiCombatStorage::FleeState& state = storage.mFleeState;
        switch (state)
        {
            case AiCombatStorage::FleeState_None:
                return;

            case AiCombatStorage::FleeState_Idle:
            {
                float triggerDist = getMaxAttackDistance(target);
                const MWWorld::Cell* cellVariant = storage.mCell->getCell();
                if (storage.mLOS && (triggerDist >= 1000 || getDistanceMinusHalfExtents(actor, target) <= triggerDist))
                {
                    const ESM::Pathgrid* pathgrid
                        = MWBase::Environment::get().getESMStore()->get<ESM::Pathgrid>().search(*cellVariant);

                    bool runFallback = true;

                    if (pathgrid != nullptr && !pathgrid->mPoints.empty()
                        && !actor.getClass().isPureWaterCreature(actor))
                    {
                        ESM::Pathgrid::PointList points;
                        const Misc::CoordinateConverter coords
                            = Misc::makeCoordinateConverter(*storage.mCell->getCell());

                        osg::Vec3f localPos = actor.getRefData().getPosition().asVec3();
                        coords.toLocal(localPos);

                        const std::size_t closestPointIndex = Misc::getClosestPoint(*pathgrid, localPos);
                        for (std::size_t i = 0; i < pathgrid->mPoints.size(); i++)
                        {
                            if (i != closestPointIndex
                                && getPathGridGraph(pathgrid).isPointConnected(closestPointIndex, i))
                            {
                                points.push_back(pathgrid->mPoints[i]);
                            }
                        }

                        if (!points.empty())
                        {
                            auto& prng = MWBase::Environment::get().getWorld()->getPrng();
                            ESM::Pathgrid::Point dest = points[Misc::Rng::rollDice(points.size(), prng)];
                            coords.toWorld(dest);

                            state = AiCombatStorage::FleeState_RunToDestination;
                            storage.mFleeDest = ESM::Pathgrid::Point(dest.mX, dest.mY, dest.mZ);

                            runFallback = false;
                        }
                    }

                    if (runFallback)
                    {
                        state = AiCombatStorage::FleeState_RunBlindly;
                        storage.mFleeBlindRunTimer = 0.0f;
                    }
                }
            }
            break;

            case AiCombatStorage::FleeState_RunBlindly:
            {
                // timer to prevent twitchy movement that can be observed in vanilla MW
                if (storage.mFleeBlindRunTimer < blindRunDuration)
                {
                    storage.mFleeBlindRunTimer += duration;

                    storage.mMovement.mRotation[0] = -actor.getRefData().getPosition().rot[0];
                    storage.mMovement.mRotation[2] = osg::PIf
                        + getZAngleToDir(
                            target.getRefData().getPosition().asVec3() - actor.getRefData().getPosition().asVec3());
                    storage.mMovement.mPosition[1] = 1;
                    updateActorsMovement(actor, duration, storage);
                }
                else
                    state = AiCombatStorage::FleeState_Idle;
            }
            break;

            case AiCombatStorage::FleeState_RunToDestination:
            {
                static const float fFleeDistance = MWBase::Environment::get()
                                                       .getESMStore()
                                                       ->get<ESM::GameSetting>()
                                                       .find("fFleeDistance")
                                                       ->mValue.getFloat();

                float dist
                    = (actor.getRefData().getPosition().asVec3() - target.getRefData().getPosition().asVec3()).length();
                if ((dist > fFleeDistance && !storage.mLOS)
                    || pathTo(
                        actor, Misc::Convert::makeOsgVec3f(storage.mFleeDest), duration, supportedMovementDirections))
                {
                    state = AiCombatStorage::FleeState_Idle;
                }
            }
            break;
        };
    }

    void AiCombat::updateActorsMovement(const MWWorld::Ptr& actor, float duration, AiCombatStorage& storage)
    {
        // apply combat movement
        float deltaAngle = storage.mMovement.mRotation[2] - actor.getRefData().getPosition().rot[2];
        osg::Vec2f movement = Misc::rotateVec2f(
            osg::Vec2f(storage.mMovement.mPosition[0], storage.mMovement.mPosition[1]), -deltaAngle);

        MWMechanics::Movement& actorMovementSettings = actor.getClass().getMovementSettings(actor);
        actorMovementSettings.mPosition[0] = movement.x();
        actorMovementSettings.mPosition[1] = movement.y();
        actorMovementSettings.mPosition[2] = storage.mMovement.mPosition[2];

        storage.mRotateMove = false;
        rotateActorOnAxis(actor, 2, actorMovementSettings, storage);
        rotateActorOnAxis(actor, 0, actorMovementSettings, storage);
    }

    void AiCombat::rotateActorOnAxis(
        const MWWorld::Ptr& actor, int axis, MWMechanics::Movement& actorMovementSettings, AiCombatStorage& storage)
    {
        actorMovementSettings.mRotation[axis] = 0;
        bool isRangedCombat = false;
        storage.mCurrentAction->getCombatRange(isRangedCombat);
        float eps = isRangedCombat ? osg::DegreesToRadians(0.5f) : osg::DegreesToRadians(3.f);
        float targetAngleRadians = storage.mMovement.mRotation[axis];
        storage.mRotateMove |= !smoothTurn(actor, targetAngleRadians, axis, eps);
    }

    void AiCombat::writeState(ESM::AiSequence::AiSequence& sequence) const
    {
        auto combat = std::make_unique<ESM::AiSequence::AiCombat>();
        combat->mTargetActor = mTargetActor;

        ESM::AiSequence::AiPackageContainer package;
        package.mType = ESM::AiSequence::Ai_Combat;
        package.mPackage = std::move(combat);
        sequence.mPackages.push_back(std::move(package));
    }

    AiCombatStorage::AiCombatStorage()
        : mAttackCooldown(0.0f)
        , mReaction(MWBase::Environment::get().getWorld()->getPrng())
        , mTimerCombatMove(0.0f)
        , mReadyToAttack(false)
        , mAttack(false)
        , mAttackRange(0.0f)
        , mCombatMove(false)
        , mRotateMove(false)
        , mCell(nullptr)
        , mCurrentAction()
        , mActionCooldown(0.0f)
        , mStrength()
        , mForceNoShortcut(false)
        , mShortcutFailPos()
        , mMovement()
        , mFleeState(FleeState_None)
        , mLOS(false)
        , mUpdateLOSTimer(0.0f)
        , mFleeBlindRunTimer(0.0f)
        , mUseCustomDestination(false)
        , mCustomDestination()
    {
    }

    void AiCombatStorage::startCombatMove(bool isDistantCombat, float distToTarget, float rangeAttack,
        const MWWorld::Ptr& actor, const MWWorld::Ptr& target)
    {
        if (!isDistantCombat && useEldenMelee(actor))
        {
            // Elden tactics provide collision-checked defensive steps/rolls.
            // Native random sidesteps otherwise keep restarting locomotion and
            // fighting against their committed attack and dodge movement.
            stopCombatMove();
            mMovement.mPosition[1] = 0.f;
            return;
        }
        auto& prng = MWBase::Environment::get().getWorld()->getPrng();

        // get the range of the target's weapon
        MWWorld::Ptr targetWeapon = MWWorld::Ptr();
        const MWWorld::Class& targetClass = target.getClass();

        if (targetClass.hasInventoryStore(target))
        {
            ESM::RefId weaponTypeId;
            MWWorld::ContainerStoreIterator weaponSlot = MWMechanics::getActiveWeapon(target, &weaponTypeId);
            if (isWeaponType(weaponTypeId))
                targetWeapon = *weaponSlot;
        }

        bool targetUsesRanged = false;
        float rangeAttackOfTarget = ActionWeapon(targetWeapon).getCombatRange(targetUsesRanged);

        if (mMovement.mPosition[0])
        {
            mTimerCombatMove = 0.1f + 0.1f * Misc::Rng::rollClosedProbability(prng);
            mCombatMove = true;
        }
        // dodge movements (for NPCs and bipedal creatures)
        // Note: do not use for ranged combat yet since in couple with back up behaviour can move actor out of cliff
        else if (actor.getClass().isBipedal(actor) && !isDistantCombat)
        {
            float moveDuration = 0;
            double angleToTarget
                = Misc::normalizeAngle(mMovement.mRotation[2] - actor.getRefData().getPosition().rot[2]);
            // Apply a big side step if enemy tries to get around and come from behind.
            // Otherwise apply a random side step (kind of dodging) with some probability
            // if actor is within range of target's weapon.
            if (std::abs(angleToTarget) > osg::PI / 4)
                moveDuration = 0.2f;
            else if (distToTarget <= rangeAttackOfTarget && Misc::Rng::rollClosedProbability(prng) < 0.25)
                moveDuration = 0.1f + 0.1f * Misc::Rng::rollClosedProbability(prng);
            if (moveDuration > 0)
            {
                mMovement.mPosition[0] = Misc::Rng::rollProbability(prng) < 0.5 ? 1.0f : -1.0f; // to the left/right
                mTimerCombatMove = moveDuration;
                mCombatMove = true;
            }
        }

        mMovement.mPosition[1] = 0;
        if (isDistantCombat)
        {
            // Backing up behaviour
            // Actor backs up slightly further away than opponent's weapon range
            // (in vanilla - only as far as opponent's weapon range),
            // or not at all if opponent is using a ranged weapon

            if (targetUsesRanged
                || distToTarget > rangeAttackOfTarget * 1.5) // Don't back up if the target is wielding ranged weapon
                return;

            // actor should not back up into water
            if (MWBase::Environment::get().getWorld()->isUnderwater(MWWorld::ConstPtr(actor), 0.5f))
                return;

            int mask
                = MWPhysics::CollisionType_World | MWPhysics::CollisionType_HeightMap | MWPhysics::CollisionType_Door;

            // Actor can not back up if there is no free space behind
            // Currently we take the 35% of actor's height from the ground as vector height.
            // This approach allows us to detect small obstacles (e.g. crates) and curved walls.
            osg::Vec3f halfExtents = MWBase::Environment::get().getWorld()->getHalfExtents(actor);
            osg::Vec3f pos = actor.getRefData().getPosition().asVec3();
            osg::Vec3f source = pos + osg::Vec3f(0, 0, 0.75f * halfExtents.z());
            osg::Vec3f fallbackDirection = actor.getRefData().getBaseNode()->getAttitude() * osg::Vec3f(0, -1, 0);
            osg::Vec3f destination = source + fallbackDirection * (halfExtents.y() + 16);

            const auto* rayCasting = MWBase::Environment::get().getWorld()->getRayCasting();
            bool isObstacleDetected = rayCasting->castRay(source, destination, mask).mHit;
            if (isObstacleDetected)
                return;

            // Check if there is nothing behind - probably actor is near cliff.
            // A current approach: cast ray 1.5-yard ray down in 1.5 yard behind actor from 35% of actor's height.
            // If we did not hit anything, there is a cliff behind actor.
            source = pos + osg::Vec3f(0, 0, 0.75f * halfExtents.z()) + fallbackDirection * (halfExtents.y() + 96);
            destination = source - osg::Vec3f(0, 0, 0.75f * halfExtents.z() + 96);
            bool isCliffDetected = !rayCasting->castRay(source, destination, mask).mHit;
            if (isCliffDetected)
                return;

            mMovement.mPosition[1] = -1;
        }
    }

    void AiCombatStorage::updateCombatMove(float duration)
    {
        if (mCombatMove)
        {
            mTimerCombatMove -= duration;
            if (mTimerCombatMove <= 0)
            {
                stopCombatMove();
            }
        }
    }

    void AiCombatStorage::stopCombatMove()
    {
        mTimerCombatMove = 0;
        mMovement.mPosition[0] = 0;
        mCombatMove = false;
    }

    void AiCombatStorage::stopEldenAttack(const MWWorld::Ptr& actor, bool resetTactics)
    {
        if (mEldenActive)
        {
            auto& stats = actor.getClass().getCreatureStats(actor);
            stats.getEldenCombatInput() = {};
            stats.getEldenCombat().clearRequests();
            stats.setAttackingOrSpell(false);
            mAttack = false;
            mEldenActive = false;
            mEldenHeavyHold = 0.f;
            mEldenGuardHold = 0.f;
            mEldenDefenseCooldown = 0.f;
            mEldenDecisionTimer = 0.f;
            mEldenComboButton = EldenCombat::AiAttackButton::Light;
            mEldenAttackRequestSequence = 0;
            mEldenDodgeRequestSequence = 0;
            mEldenComboLimit = 1;
            mEldenAttackQueued = false;
            mEldenDodgeQueued = false;
            mEldenHeavyFinisher = false;
        }
        if (resetTactics)
        {
            mEldenTactics.reset();
            mEldenAttackDelayScale = 1.f;
        }
    }

    bool AiCombatStorage::updateEldenAttack(const MWWorld::Ptr& actor, const MWWorld::Ptr& target,
        CharacterController& characterController, bool distantCombat, float duration)
    {
        const auto world = MWBase::Environment::get().getWorld();
        if (!useEldenMelee(actor) || distantCombat || !mCurrentAction || !mCurrentAction->getSpell().empty()
            || !mCurrentAction->isAttackingOrSpell() || world->isSwimming(actor) || world->isFlying(actor)
            || !world->isActorCollisionEnabled(actor))
        {
            stopEldenAttack(actor);
            return false;
        }

        using EldenAction = EldenCombat::Action;
        using AttackButton = EldenCombat::AiAttackButton;
        const auto& actorClass = actor.getClass();
        auto& stats = actorClass.getCreatureStats(actor);
        auto& combat = stats.getEldenCombat();
        auto& input = stats.getEldenCombatInput();
        if (!stats.isEldenCombatActive())
        {
            // Character updates this flag after AI. A temporary suspension may
            // therefore remain visible for one frame after paralysis/animation ends.
            mEldenTactics.update(combat, 0.f, Settings::game().mEldenRingAiAttackDelay * mEldenAttackDelayScale);
            stopEldenAttack(actor, false);
            return true;
        }
        const auto config = eldenCombatConfig(actor);
        const auto& settings = Settings::game();
        auto& prng = world->getPrng();
        input.mTwoHanded = false;
        input.mTwoHandedLeft = false;
        if (actorClass.hasInventoryStore(actor))
        {
            ESM::RefId type;
            getActiveWeapon(actor, &type);
            input.mTwoHanded = isWeaponType(type) && (getWeaponType(type)->mFlags & ESM::WeaponType::TwoHanded);
        }
        const auto action = combat.action();
        const auto& definition = combat.definition();
        const float stamina = std::max(0.f, stats.getFatigue().getCurrent())
            / std::max(1.f, stats.getFatigue().getModified());
        const bool biped = actorClass.isBipedal(actor);
        const bool canGuard = biped && (config.mHasShield || input.mTwoHanded);
        const float distance = getDistanceToBounds(actor, target);
        const bool inReach = mReadyToAttack && !mRotateMove && mLOS
            && distance <= std::max(64.f, mAttackRange * 1.1f);

        mEldenActive = true;
        input.mAIControlled = true;
        input.mLockedOn = biped;
        input.mSprint = distance > std::max(300.f, mAttackRange * 3.f) && stamina > 0.55f;
        input.mLight.mHeld = false;
        input.mCritical.mHeld = false;
        input.mDodge.mHeld = false;
        mEldenHeavyHold = std::max(0.f, mEldenHeavyHold - duration);
        mEldenGuardHold = std::max(0.f, mEldenGuardHold - duration);
        const bool heavyHeld = mEldenHeavyHold > 0.f;
        input.mHeavy.mReleased |= input.mHeavy.mHeld && !heavyHeld;
        input.mHeavy.mHeld = heavyHeld;
        input.mGuard.mHeld = canGuard && mEldenGuardHold > 0.f && stamina > 0.1f;
        mEldenDefenseCooldown = std::max(0.f, mEldenDefenseCooldown - duration);
        mEldenDecisionTimer = std::max(0.f, mEldenDecisionTimer - duration);
        mEldenTactics.update(combat, duration, settings.mEldenRingAiAttackDelay * mEldenAttackDelayScale);
        if (mEldenAttackQueued && mEldenAttackRequestSequence != combat.sequence())
            mEldenAttackQueued = false;
        if (mEldenDodgeQueued && mEldenDodgeRequestSequence != combat.sequence())
            mEldenDodgeQueued = false;
        else if (mEldenDodgeQueued && mEldenDefenseCooldown <= 0.f)
        {
            // Discard a dodge that could not execute (for example after losing
            // stamina/grounding), rather than keeping its route indefinitely.
            combat.clearRequests();
            mEldenDodgeQueued = false;
        }
        if (mEldenDodgeQueued || action == EldenAction::Roll || action == EldenAction::CrouchRoll
            || action == EldenAction::Backstep)
            actorClass.getMovementSettings(actor).mRotation[2] = 0.f;

        // Observe the telegraph, not the damage callback. Recovery and attacks
        // facing elsewhere are not threats. One swing gets one defensive choice.
        const auto& opponent = target.getClass().getCreatureStats(target).getEldenCombat();
        mEldenTactics.observeThreat(opponent, duration);
        bool threatened = false;
        float threatRemaining = 0.f;
        if (mLOS && useEldenMelee(target) && EldenCombat::Controller::isAttack(opponent.action())
            && distance <= getMaxAttackDistance(target) * 1.15f + 32.f)
        {
            osg::Vec3f towardActor = actor.getRefData().getPosition().asVec3()
                - target.getRefData().getPosition().asVec3();
            towardActor.z() = 0.f;
            towardActor.normalize();
            const float yaw = target.getRefData().getPosition().rot[2];
            const bool facing = (osg::Vec3f(std::sin(yaw), std::cos(yaw), 0.f) * towardActor) > 0.5f;
            for (std::size_t hit = 0; facing && hit < opponent.definition().mHitCount; ++hit)
            {
                const auto& window = opponent.definition().mHits[hit];
                const float untilHit = (window.mFrom - opponent.frame()) / EldenCombat::AnimationFps;
                if (opponent.frame() < window.mTo && untilHit <= 0.65f)
                {
                    threatened = true;
                    threatRemaining = std::max(threatRemaining,
                        (window.mTo - opponent.frame()) / EldenCombat::AnimationFps);
                }
            }
        }

        mAttack = EldenCombat::Controller::isAttack(action);
        stats.setAttackingOrSpell(mAttack);
        const bool idle = action == EldenAction::Idle;
        const bool canRequest = idle || combat.frame() >= definition.mInputFrom;
        const bool canBlockNow = canGuard && stamina >= 0.35f
            && (idle || combat.frame() >= definition.mCancelGuard);
        const bool canDodgeNow = biped && stamina >= 0.18f && world->isOnGround(actor)
            && (idle || combat.frame() >= std::max(definition.mDodgeInputFrom, definition.mCancelDodge));

        auto abandonChain = [&] {
            // A buffered continuation otherwise wins over a held guard in the
            // controller. Stop charging and remove it before committing to defense.
            combat.clearRequests();
            mEldenAttackQueued = false;
            mEldenComboLimit = mEldenTactics.comboCount();
            mEldenHeavyHold = 0.f;
            input.mHeavy = {};
            input.mLight = {};
            input.mCritical = {};
            input.mGuard = {};
        };

        // Defense remains available during the post-chain pause and is evaluated
        // every frame, independently of the slower opener decision timer.
        if (biped && threatened && combat.hitStopRemaining() <= 0.f && mEldenDefenseCooldown <= 0.f
            && (canBlockNow || canDodgeNow) && mEldenTactics.consumeReaction(settings.mEldenRingAiReactionTime))
        {
            if (canBlockNow && Misc::Rng::rollProbability(prng) < settings.mEldenRingAiBlockChance)
            {
                abandonChain();
                mEldenGuardHold = std::clamp(threatRemaining + 0.15f, 0.35f, 1.f);
                input.mGuard.mHeld = true;
                mEldenDefenseCooldown = 0.45f + 0.25f * Misc::Rng::rollProbability(prng);
                return true;
            }
            if (canDodgeNow && Misc::Rng::rollProbability(prng) < settings.mEldenRingAiDodgeChance)
            {
                const auto directions = characterController.getSupportedMovementDirections();
                const float rollDistance = EldenCombat::Controller::actionDefinition(
                    EldenAction::Roll, config).mDistance;
                const float backstepDistance = EldenCombat::Controller::actionDefinition(
                    EldenAction::Backstep, config).mDistance;
                const bool tryBackstep = Misc::Rng::rollProbability(prng) < 0.2f;
                osg::Vec3f direction(0.f, -1.f, 0.f);
                const bool backstep = tryBackstep && (directions & MWWorld::MovementDirectionFlag_Back)
                    && safeEldenDodge(actor, direction, backstepDistance);
                bool safe = backstep;
                const float side = Misc::Rng::rollProbability(prng) < 0.5f ? -1.f : 1.f;
                // Try the other side or a backwards roll if the first route is blocked.
                for (const osg::Vec3f candidate : { osg::Vec3f(side, 0.f, 0.f),
                         osg::Vec3f(-side, 0.f, 0.f), osg::Vec3f(0.f, -1.f, 0.f) })
                {
                    if (safe)
                        break;
                    const auto supported = candidate.y() < 0.f ? MWWorld::MovementDirectionFlag_Back
                        : (candidate.x() < 0.f ? MWWorld::MovementDirectionFlag_Left
                                              : MWWorld::MovementDirectionFlag_Right);
                    if ((directions & supported) && safeEldenDodge(actor, candidate, rollDistance))
                    {
                        direction = candidate;
                        safe = true;
                    }
                }
                if (safe)
                {
                    abandonChain();
                    // Snapshot the route we checked. Pathfinding and avoidance may
                    // change ordinary movement before the controller accepts this.
                    input.mDodgeDirection = std::array<float, 2>{ backstep ? 0.f : direction.x(),
                        backstep ? 0.f : direction.y() };
                    input.mDodge = { false, true, true };
                    actorClass.getMovementSettings(actor).mRotation[2] = 0.f;
                    mEldenDodgeQueued = true;
                    mEldenDodgeRequestSequence = combat.sequence();
                    mEldenGuardHold = 0.f;
                    mEldenDefenseCooldown = 0.8f + 0.5f * Misc::Rng::rollProbability(prng);
                    return true;
                }
            }
        }

        if (mEldenDodgeQueued)
            return true;
        // Guard recoil can outlast the counter window. Queue the counter during
        // recoil, but never use it to bypass an outstanding post-chain pause.
        if (inReach && canRequest && (idle || action == EldenAction::GuardHit)
            && combat.guardCounterReady() && stamina >= 0.22f && mEldenTactics.attackReady()
            && !mEldenAttackQueued && combat.hitStopRemaining() <= 0.f)
        {
            mEldenGuardHold = 0.f;
            input.mGuard = {};
            input.mHeavy = { true, true, false };
            mEldenHeavyHold = 0.1f;
            mEldenComboButton = AttackButton::Heavy;
            mEldenComboLimit = 1;
            mEldenHeavyFinisher = false;
            mEldenAttackDelayScale = 1.f + 0.4f * Misc::Rng::rollProbability(prng);
            mEldenAttackQueued = true;
            mEldenAttackRequestSequence = combat.sequence();
            mAttack = true;
            stats.setAttackingOrSpell(true);
            return true;
        }

        // Do not leave an old follow-up buffered after losing the target or
        // exhausting stamina. Defensive requests have their own lifetime.
        if (!inReach || stamina < 0.2f || input.mGuard.mHeld)
        {
            if (mEldenAttackQueued)
                abandonChain();
            return true;
        }
        if (!canRequest || combat.hitStopRemaining() > 0.f)
            return true;

        const int comboLimit = std::min(mEldenComboLimit, settings.mEldenRingAiMaxComboAttacks.get());
        auto pressAttack = [&](AttackButton button, bool charge) {
            if (button == AttackButton::Heavy)
            {
                const auto heavyDefinition = EldenCombat::Controller::actionDefinition(
                    EldenAction::Heavy1Charge, config, input.mTwoHanded);
                mEldenHeavyHold = charge ? heavyDefinition.mChargeTo / EldenCombat::AnimationFps + 0.2f : 0.1f;
                input.mHeavy = { true, true, false };
            }
            else if (button == AttackButton::Left)
                input.mGuard = { false, true, true };
            else
                input.mLight = { false, true, true };
            mEldenAttackQueued = true;
            mEldenAttackRequestSequence = combat.sequence();
        };

        // Follow the weapon's input/cancel windows each frame. Planning the limit
        // and finisher once avoids rerolling until a continuation becomes inevitable.
        if (mAttack)
        {
            if ((action == EldenAction::Heavy1Charge || action == EldenAction::Heavy2Charge)
                && combat.frame() < definition.mChargeTo)
                return true;
            const bool heavyFinisher = mEldenHeavyFinisher && mEldenTactics.comboCount() == comboLimit - 1;
            const AttackButton next = heavyFinisher ? AttackButton::Heavy : mEldenComboButton;
            const float requiredStamina = next == AttackButton::Heavy ? 0.4f : 0.28f;
            if (stamina >= requiredStamina && mEldenTactics.canCombo(combat, next, comboLimit))
            {
                pressAttack(next, false);
                mEldenTactics.markComboRequested(combat);
            }
            return true;
        }
        if (!idle || !mEldenTactics.attackReady() || mEldenDecisionTimer > 0.f
            || !characterController.readyToStartAttack())
            return true;

        mEldenDecisionTimer = 0.18f + 0.12f * Misc::Rng::rollProbability(prng);
        mEldenAttackDelayScale = 1.f + 0.4f * Misc::Rng::rollProbability(prng);
        mEldenComboLimit = 1 + Misc::Rng::rollDice(settings.mEldenRingAiMaxComboAttacks.get(), prng);
        mEldenHeavyFinisher = false;
        if (biped && opponent.criticalReady())
        {
            input.mCritical = { false, true, true };
            input.mCriticalAvailable = true;
            mEldenComboLimit = 1;
            mEldenAttackQueued = true;
            mEldenAttackRequestSequence = combat.sequence();
        }
        else
        {
            const bool counter = combat.guardCounterReady() && stamina >= 0.22f;
            const bool heavy = counter || (stamina >= 0.4f && Misc::Rng::rollProbability(prng) < 0.3f);
            const bool useLeft = !heavy && config.mHasLeftWeapon && !config.mHasShield && !input.mTwoHanded
                && Misc::Rng::rollProbability(prng) < (config.mPairedWeapons ? 0.55f : 0.35f);
            mEldenComboButton = heavy ? AttackButton::Heavy : (useLeft ? AttackButton::Left : AttackButton::Light);
            if (heavy)
                mEldenComboLimit = std::min(mEldenComboLimit, 2);
            else if (!useLeft)
                mEldenHeavyFinisher = stamina >= 0.45f && Misc::Rng::rollProbability(prng) < 0.3f;
            const bool charge = heavy && !counter && stamina >= 0.65f && !threatened
                && Misc::Rng::rollProbability(prng) < 0.55f;
            pressAttack(mEldenComboButton, charge);
        }
        mAttack = true;
        stats.setAttackingOrSpell(true);
        return true;
    }

    void AiCombatStorage::updateAttack(const MWWorld::Ptr& actor, CharacterController& characterController,
        const ESM::Weapon* weapon, bool distantCombat, float duration)
    {
        const MWWorld::Class& actorClass = actor.getClass();
        MWMechanics::CreatureStats& actorStats = actorClass.getCreatureStats(actor);

        if (mAttack)
        {
            float attackStrength = characterController.calculateWindUp();
            if (characterController.readyToPrepareAttack() || attackStrength >= mStrength || attackStrength == -1.f)
                mAttack = false;
        }
        else if (mReadyToAttack && characterController.readyToStartAttack())
        {
            if (mAttackCooldown <= 0)
            {
                mAttack = true; // attack starts just now

                if (!distantCombat)
                    characterController.setAIAttackType(chooseBestAttack(weapon));

                auto& prng = MWBase::Environment::get().getWorld()->getPrng();
                mStrength = Misc::Rng::rollClosedProbability(prng);

                const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();

                bool canShout = true;
                ESM::RefId spellId = mCurrentAction->getSpell();
                if (!spellId.empty())
                {
                    const ESM::Spell* spell = store.get<ESM::Spell>().find(spellId);
                    if (spell->mEffects.mList.empty() || spell->mEffects.mList[0].mData.mRange != ESM::RT_Target)
                        canShout = false;
                }

                if (canShout)
                {
                    // Say a provoking combat phrase
                    const int iVoiceAttackOdds
                        = store.get<ESM::GameSetting>().find("iVoiceAttackOdds")->mValue.getInteger();
                    if (Misc::Rng::roll0to99(prng) < iVoiceAttackOdds)
                    {
                        MWBase::Environment::get().getDialogueManager()->say(actor, ESM::RefId::stringRefId("attack"));
                    }
                }

                if (mCurrentAction->getActionCooldown() > 0.f)
                {
                    mAttackCooldown = mCurrentAction->getActionCooldown();
                }
                else
                {
                    std::string_view delayGmst = actor.getClass().isNpc() ? "fCombatDelayNPC" : "fCombatDelayCreature";
                    const float baseDelay = store.get<ESM::GameSetting>().find(delayGmst)->mValue.getFloat();
                    mAttackCooldown = std::min(baseDelay + 0.01f * Misc::Rng::roll0to99(prng), baseDelay + 0.9f);
                }
            }
            else
                mAttackCooldown -= duration;
        }

        actorStats.setAttackingOrSpell(mAttack);
    }

    void AiCombatStorage::stopAttack()
    {
        mMovement.mPosition[0] = 0;
        mMovement.mPosition[1] = 0;
        mMovement.mPosition[2] = 0;
        mReadyToAttack = false;
        mAttack = false;
    }

    void AiCombatStorage::startFleeing()
    {
        stopFleeing();
        mFleeState = FleeState_Idle;
    }

    void AiCombatStorage::stopFleeing()
    {
        mMovement.mPosition[0] = 0;
        mMovement.mPosition[1] = 0;
        mMovement.mPosition[2] = 0;
        mFleeState = FleeState_None;
        mFleeDest = ESM::Pathgrid::Point(0, 0, 0);
    }

    bool AiCombatStorage::isFleeing() const
    {
        return mFleeState != FleeState_None;
    }
}

namespace
{

    std::string_view chooseBestAttack(const ESM::Weapon* weapon)
    {
        if (weapon != nullptr)
        {
            // the more damage attackType deals the more probability it has
            int slash = (weapon->mData.mSlash[0] + weapon->mData.mSlash[1]) / 2;
            int chop = (weapon->mData.mChop[0] + weapon->mData.mChop[1]) / 2;
            int thrust = (weapon->mData.mThrust[0] + weapon->mData.mThrust[1]) / 2;

            auto& prng = MWBase::Environment::get().getWorld()->getPrng();
            float roll = Misc::Rng::rollClosedProbability(prng) * (slash + chop + thrust);
            if (roll <= slash)
                return "slash";
            else if (roll <= (slash + thrust))
                return "thrust";
            else
                return "chop";
        }
        return MWMechanics::CharacterController::getRandomAttackType();
    }

}
