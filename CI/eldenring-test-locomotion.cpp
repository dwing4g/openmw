// Exercise the movement policy used by CharacterController and Actor directly.
// No graphics context or game data is required.
#include <components/eldencombat/animations.hpp>
#include <components/eldencombat/locomotion.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    constexpr float Pi = 3.14159265358979323846f;
    int checks = 0;

    void check(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
        ++checks;
    }

    void close(float actual, float expected, const char* message)
    {
        check(std::isfinite(actual) && std::abs(actual - expected) < 1e-4f, message);
    }

    float magnitude(EldenCombat::PlanarMovement movement)
    {
        return std::hypot(movement.mX, movement.mY);
    }

    EldenCombat::PlanarMovement heading(float angle, float speed = 1.f)
    {
        return { speed * std::cos(angle), speed * std::sin(angle) };
    }

    void close(EldenCombat::PlanarMovement actual, EldenCombat::PlanarMovement expected, const char* message)
    {
        close(actual.mX, expected.mX, message);
        close(actual.mY, expected.mY, message);
    }

    void directionalSpeed()
    {
        constexpr float maximumSpeed = 225.f;
        for (float amount : { 0.25f, 0.75f, 1.f })
        {
            for (int direction = 0; direction < 16; ++direction)
            {
                const auto target = heading(direction * Pi / 8.f, amount);
                for (bool smooth : { false, true })
                {
                    const auto actual = EldenCombat::smoothMovement(target, target, 1.f / 60.f, smooth, true, .333f);
                    close(magnitude(actual), amount, "steady movement speed depends on direction");
                    for (bool strafing : { false, true })
                        close(EldenCombat::movementSpeed(maximumSpeed, magnitude(actual), strafing, true),
                            maximumSpeed * amount, "Elden strafing still has a directional speed penalty");
                }
            }
        }
        const auto diagonal = EldenCombat::smoothMovement({}, { 1.f, 1.f }, .1f, false, true, .333f);
        close(magnitude(diagonal), 1.f, "diagonal input exceeds maximum speed");
        close(diagonal.mX, diagonal.mY, "diagonal normalization changes direction");
        close(EldenCombat::movementSpeed(maximumSpeed, 1.f, true, false), maximumSpeed * .75f,
            "native movement lost its existing strafe policy");
        close(EldenCombat::movementSpeed(maximumSpeed, .25f, false, false), maximumSpeed * .25f,
            "native forward analog speed changed");
        close(EldenCombat::movementSpeed(0.f, 1.f, false, true), 0.f, "immobile actors can move");
        close(EldenCombat::movementSpeed(maximumSpeed, 0.f, true, true), 0.f, "zero input causes movement");
    }

    void smoothTurns()
    {
        // Include sharp reversals, boundary crossings at +/-pi, analog inputs,
        // different camera headings, and common frame rates.
        for (int fps : { 30, 60, 144 })
        {
            for (float delay : { .1f, .333f, .8f })
            {
                for (float amount : { .25f, 1.f })
                {
                    for (float start : { -Pi * .95f, 0.f, Pi * .95f })
                    {
                        for (float turn : { -Pi, -Pi / 2.f, Pi / 10.f, Pi / 2.f, Pi })
                        {
                            auto current = heading(start, amount);
                            const auto target = heading(start + turn, amount);
                            const float step = 1.f / fps;
                            const int steps = static_cast<int>(std::ceil(delay / step));
                            for (int frame = 0; frame < steps; ++frame)
                            {
                                const auto previous = current;
                                current = EldenCombat::smoothMovement(current, target, step, true, true, delay);
                                close(magnitude(current), amount, "smooth turning changes travel speed");
                                const float turnAngle = std::atan2(previous.mX * current.mY - previous.mY * current.mX,
                                    previous.mX * current.mX + previous.mY * current.mY);
                                check(std::abs(turnAngle) <= Pi * step / delay + 1e-4f,
                                    "smooth turn exceeds the configured turning rate");
                            }
                            close(current, target, "smooth turn has not completed within the configured delay");
                        }
                    }
                }
            }
        }
        const auto halfTurn = EldenCombat::smoothMovement({ 1.f, 0.f }, { -1.f, 0.f }, .1f, true, true, .4f);
        check(halfTurn.mX > 0.f && halfTurn.mY > 0.f,
            "180-degree turn should follow a deterministic arc without stopping");
        close(EldenCombat::smoothMovement({ 1.f, -0.f }, { -1.f, -0.f }, .1f, true, true, .4f), halfTurn,
            "180-degree turn arc changes with signed zero input");
        close(EldenCombat::smoothMovement({ 1.f, 0.f }, { 0.f, 1.f }, .2f, true, true, .4f), { 0.f, 1.f },
            "quarter turn should complete in half the maximum turning delay");
        close(EldenCombat::smoothMovement({ 1.f, 0.f }, { -1.f, 0.f }, .1f, true, true, 0.f), { -1.f, 0.f },
            "zero turning delay produces invalid or stalled movement");
        const auto noTime = EldenCombat::smoothMovement({ 1.f, 0.f }, { 0.f, 1.f }, 0.f, true, true, .333f);
        close(noTime, { 1.f, 0.f }, "zero elapsed time advances a smooth turn");
        const auto negativeTime = EldenCombat::smoothMovement(noTime, { 0.f, 1.f }, -.1f, true, true, .333f);
        close(negativeTime, noTime, "negative elapsed time reverses smoothing");
    }

    void startsStopsAndSettings()
    {
        const auto start = EldenCombat::smoothMovement({}, { 0.f, -1.f }, .1f, true, true, .333f);
        close(start, { 0.f, -.3f }, "smooth start must accelerate along the requested direction");
        auto analog = start;
        for (int frame = 0; frame < 30; ++frame)
            analog = EldenCombat::smoothMovement(analog, { 0.f, -.65f }, 1.f / 60.f, true, true, .333f);
        close(analog, { 0.f, -.65f }, "smooth acceleration overshoots or ignores analog input");
        close(EldenCombat::smoothMovement(analog, {}, .01f, true, true, .333f), {},
            "player input release must stop immediately");
        const auto npcStop = EldenCombat::smoothMovement({ 1.f, 0.f }, {}, .1f, true, false, .333f);
        close(npcStop, { .7f, 0.f }, "NPC stopping does not decelerate along current travel");
        close(EldenCombat::smoothMovement(npcStop, {}, 1.f, true, false, .333f), {},
            "NPC stopping overshoots zero speed");

        // CharacterController stores every return value, including when the
        // setting is off and when first person bypasses smoothing.
        auto current = EldenCombat::smoothMovement({ 0.f, 1.f }, { 1.f, 0.f }, .01f, true, true, .333f);
        current = EldenCombat::smoothMovement(current, { 0.f, -1.f }, 0.f, false, true, .333f);
        close(current, { 0.f, -1.f }, "disabled smoothing retains turning delay");
        current = EldenCombat::smoothMovement(current, { -1.f, 0.f }, .01f, false, true, .333f);
        close(current, { -1.f, 0.f }, "disabled smoothing does not follow direction immediately");
        current = EldenCombat::smoothMovement(current, { -1.f, 0.f }, .01f, true, true, .333f);
        close(current, { -1.f, 0.f }, "re-enabling smoothing restores stale movement");
        current = EldenCombat::smoothMovement(current, {}, 0.f, false, true, .333f);
        current = EldenCombat::smoothMovement(current, {}, .01f, true, true, .333f);
        close(current, {}, "re-enabling smoothing at rest resumes old movement");
        close(EldenCombat::smoothMovement({}, { .6f, .8f }, 0.f, false, false, .333f), { .6f, .8f },
            "first-person bypass still accelerates slowly");
    }

    void poseFollowsTravel()
    {
        auto movement = EldenCombat::PlanarMovement{ 0.f, 1.f };
        for (int frame = 0; frame < 30; ++frame)
        {
            movement = EldenCombat::smoothMovement(movement, { 0.f, -1.f }, 1.f / 60.f, true, true, .333f);
            for (bool sprinting : { false, true })
            {
                const auto turned = EldenCombat::locomotionAnimation(
                    movement.mX, movement.mY, false, true, sprinting, true);
                close(-std::sin(turned.mYaw), movement.mX, "imported pose yaw differs from smoothed travel X");
                close(std::cos(turned.mYaw), movement.mY, "imported pose yaw differs from smoothed travel Y");
                const auto locked = EldenCombat::locomotionAnimation(
                    movement.mX, movement.mY, false, true, sprinting, false);
                if (sprinting)
                    close(locked.mYaw, turned.mYaw, "lock-on sprint no longer follows travel");
                else
                    close(locked.mYaw, 0.f, "lock-on running should retain a target-facing directional pose");
            }
        }
    }

    void stableDirectionalGaits()
    {
        // Steering around a target and analog input can straddle a diagonal
        // every frame. Keep the clip until the direction changes decisively.
        for (float strength : { .1f, .5f, 1.f })
        {
            for (float xSign : { -1.f, 1.f })
            {
                for (float ySign : { -1.f, 1.f })
                {
                    for (bool crouching : { false, true })
                    {
                        for (bool running : { false, true })
                        {
                            EldenCombat::LocomotionAnimationState state;
                            const auto forward = state.update(0.f, ySign * strength, crouching, running, false, false);
                            for (int frame = 0; frame < 120; ++frame)
                            {
                                const float x = xSign * strength * (frame % 2 ? 1.01f : .99f);
                                const auto pose = state.update(x, ySign * strength, crouching, running, false, false);
                                check(pose.mId == forward.mId, "diagonal steering noise restarts the gait blend");
                                close(pose.mNativeSpeed, forward.mNativeSpeed, "diagonal noise changes stride timing");
                                close(pose.mYaw, 0.f, "directional gait stopped facing the target");
                            }
                            const auto side = state.update(xSign * strength, .7f * ySign * strength,
                                crouching, running, false, false);
                            check(side.mId != forward.mId, "deliberate direction change cannot leave the current gait");
                            for (int frame = 0; frame < 120; ++frame)
                                check(state.update(xSign * strength, ySign * strength * (frame % 2 ? 1.01f : .99f),
                                    crouching, running, false, false).mId == side.mId,
                                    "diagonal steering noise restarts the sideways gait blend");
                            const auto reversed = state.update(-xSign * strength, 0.f, crouching, running, false, false);
                            check(reversed.mId != side.mId, "direction hysteresis delays an actual reversal");
                            state.reset();
                            check(state.update(xSign * strength, ySign * strength, crouching, running, false, false).mId
                                    == forward.mId,
                                "resuming movement retained a stale directional gait");
                        }
                    }
                }
            }
        }

        EldenCombat::LocomotionAnimationState state;
        state.update(1.f, 0.f, false, false, false, false);
        check(state.update(1.f, 1.f, false, true, false, false).mId == 20103,
            "changing gait loses the stable travel direction");
        const auto turned = state.update(1.f, 1.f, false, true, false, true);
        check(turned.mId == 20100, "free movement no longer uses the forward gait");
        close(turned.mYaw, -Pi / 4.f, "direction hysteresis altered free movement facing");
        check(state.update(1.f, 1.f, false, true, false, false).mId == 20100,
            "entering lock-on restored an old directional gait");
        state.update(1.f, 0.f, false, true, false, false);
        const auto sprint = state.update(-1.f, 1.f, false, true, true, false);
        check(sprint.mId == 20200, "direction hysteresis altered the sprint clip");
        close(sprint.mYaw, Pi / 4.f, "direction hysteresis altered sprint facing");
        state.update(-1.f, 0.f, false, true, false, false);
        state.update(0.f, 0.f, false, true, false, false);
        check(state.update(-1.f, 1.f, false, true, false, false).mId == 20100,
            "stopping did not reset the directional gait");
    }

    void stableLockOnFacing()
    {
        struct PlayerControls
        {
            float mYaw = 0.f;
            float mLuaYaw = 0.f;

            float update(bool locked, float target, float seconds, bool beforeLua, float advancedFraction = 1.f)
            {
                // Stock player Lua writes movement/run/jump every frame, even
                // at rest. Actors::updateLuaControls then applies the previous
                // Lua yaw and mirrors this frame's native yaw for the next one.
                const float nativeYaw = locked && beforeLua
                    ? EldenCombat::lockOnYawChange(mYaw, target, seconds) : 0.f;
                float rotation = mLuaYaw;
                mLuaYaw = nativeYaw;
                if (locked && !beforeLua)
                    rotation = EldenCombat::lockOnYawChange(mYaw, target, seconds);
                // CharacterController consumes rotation and then scales the
                // applied turn by the part of this frame outside hit-stop.
                rotation *= advancedFraction;
                mYaw = std::remainder(mYaw + rotation, 2.f * Pi);
                return rotation;
            }
        };

        constexpr float target = 5.f * Pi / 180.f;
        PlayerControls delayed;
        for (int frame = 0; frame < 12; ++frame)
            delayed.update(true, target, 1.f / 60.f, true);
        check(std::abs(delayed.mYaw - target) > target / 2.f,
            "lock-on regression fixture does not reproduce the delayed-yaw oscillation");

        for (int fps : { 30, 60, 144 })
        {
            const float step = 1.f / fps;
            for (float initial : { -Pi + .01f, 0.f, Pi - .01f })
            {
                PlayerControls current;
                current.mYaw = initial;
                for (int frame = 0; frame < fps; ++frame)
                {
                    const float previousError = std::abs(std::remainder(target - current.mYaw, 2.f * Pi));
                    const float turn = current.update(true, target, step, false);
                    check(std::abs(turn) <= 4.f * Pi * step + 1e-5f, "lock-on exceeded its turn speed");
                    const float error = std::abs(std::remainder(target - current.mYaw, 2.f * Pi));
                    check(error <= previousError + 1e-5f, "standing lock-on overshot and reversed facing");
                    if (frame >= fps / 2)
                        close(current.mYaw, target, "standing lock-on never settles at the target");
                }
                close(current.mLuaYaw, 0.f, "lock-on correction leaked into the next Lua control sample");
                close(current.update(false, target, step, false), 0.f, "unlocking replays an old lock-on turn");

                // A moving target and crossing +/-pi must use the current angle
                // too, without a delayed correction on the next frame.
                current.mYaw = Pi - .01f;
                for (int frame = 0; frame < fps; ++frame)
                {
                    const float movingTarget = std::remainder(Pi + .3f * std::sin(frame * step), 2.f * Pi);
                    current.update(true, movingTarget, step, false);
                    close(current.mYaw, movingTarget, "lock-on lags or jitters across the angle wrap");
                }
            }
        }

        PlayerControls frozen;
        close(frozen.update(true, target, 1.f / 60.f, false, 0.f), 0.f, "hit-stop rotates the player");
        close(frozen.update(true, target, 1.f / 60.f, false, .5f), target / 2.f,
            "partial hit-stop ignores the active portion of the frame");
        frozen.update(true, target, 1.f / 60.f, false);
        close(frozen.mYaw, target, "thawing replays a stale pre-hit-stop turn");
        close(EldenCombat::lockOnYawChange(0.f, target, 0.f), 0.f, "zero time advances lock-on facing");
        close(EldenCombat::lockOnYawChange(0.f, target, -.1f), 0.f, "negative time advances lock-on facing");
    }

    void stableGroundContact()
    {
        for (int fps : { 30, 60, 144 })
        {
            const float step = 1.f / fps;
            EldenCombat::GroundContactGrace contact;
            check(!contact.update(false, step), "initial airborne pose has phantom ground contact");
            check(contact.update(true, step), "landing does not immediately restore ground pose");
            for (int frame = 0; frame < fps * 2; ++frame)
                check(contact.update(frame % 2 == 0, step),
                    "terrain contact noise alternates ground and falling poses");

            // A sustained fall must become visible after the short grace,
            // regardless of the animation action active during these updates.
            contact.update(true, step);
            const int fallFrame = static_cast<int>(std::ceil(.12f / step));
            for (int frame = 1; frame < fallFrame; ++frame)
                check(contact.update(false, step), "ground pose ends before the terrain contact grace");
            check(!contact.update(false, step), "sustained fall retains the ground pose beyond its grace");
            check(!contact.update(false, step), "airborne pose restores expired ground contact");
            check(contact.update(true, step), "landing after a sustained fall cannot restore the ground pose");
            contact.reset();
            check(!contact.update(false, 0.f), "reset preserves ground contact from the previous location");
        }

        EldenCombat::GroundContactGrace contact;
        check(!contact.update(false, 0.f), "zero-time airborne initialization invents ground contact");
        check(contact.update(true, 0.f), "zero-time observed contact fails to establish the ground pose");
        check(contact.update(false, .06f), "short loss of contact changes the ground pose");
        for (int frame = 0; frame < 120; ++frame)
            check(contact.update(false, 0.f), "frozen pose consumes ground contact grace");
        check(contact.update(false, -.1f), "negative time discards ground contact grace");
        check(!contact.update(false, .06f), "nonpositive time extended ground contact grace");
        contact.update(true, 0.f);
        check(!contact.update(false, 1.f), "long airborne update fails to expire ground contact grace");
    }

    void idleFacing()
    {
        // Actor rotation is applied around -Z in scene.cpp's makeActorOsgQuat;
        // EldenAnimation applies the pose rotation around +Z. Compare vectors
        // in world space so wrapping across +/-pi cannot hide a facing change.
        for (bool turnToMovement : { false, true })
        {
            for (bool sprinting : { false, true })
            {
                for (int direction = 0; direction < 8; ++direction)
                {
                    const auto move = heading(direction * Pi / 4.f);
                    const auto pose = EldenCombat::locomotionAnimation(
                        move.mX, move.mY, false, true, sprinting, turnToMovement);
                    for (float actorYaw : { -Pi + .01f, 0.f, Pi - .01f })
                    {
                        EldenCombat::IdleFacing facing;
                        close(heading(facing.update(actorYaw, pose.mYaw, true)), heading(pose.mYaw),
                            "idle facing changed a moving pose");
                        const auto worldHeading = heading(pose.mYaw - actorYaw);
                        close(heading(facing.update(actorYaw, 0.f, false) - actorYaw), worldHeading,
                            "stopping sideways or backwards changed facing");
                        for (int step = -32; step <= 32; ++step)
                        {
                            const float cameraYaw = actorYaw + step * Pi / 8.f;
                            const float idleYaw = facing.update(cameraYaw, 0.f, false);
                            close(heading(idleYaw - cameraYaw), worldHeading,
                                "orbiting camera rotated the stationary pose");
                            check(std::abs(idleYaw) <= Pi + 1e-6f, "idle facing failed to normalize yaw");
                        }

                        // Moving again must immediately use the selected gait
                        // and actual smoothed travel, without retaining idle yaw.
                        auto travel = EldenCombat::smoothMovement({}, { .6f, -.8f }, .1f, true, true, .333f);
                        auto next = EldenCombat::locomotionAnimation(
                            travel.mX, travel.mY, false, true, sprinting, turnToMovement);
                        const float nextActorYaw = actorYaw + 1.7f;
                        close(heading(facing.update(nextActorYaw, next.mYaw, true)), heading(next.mYaw),
                            "resuming movement retained idle camera offset");
                        close(magnitude(travel), .3f, "idle-facing handoff changed smoothed movement speed");
                        close(heading(facing.update(nextActorYaw, 0.f, false)), heading(next.mYaw),
                            "second stop restored the previous direction");

                        // Teleport, load/new controller, actions, lock-on,
                        // first person and native poses reset this state.
                        facing.reset();
                        close(facing.update(-2.f, 0.f, false), 0.f,
                            "reset restored old facing in a new aiming frame");
                    }
                }
            }
        }
    }
}

int main()
{
    try
    {
        directionalSpeed();
        smoothTurns();
        startsStopsAndSettings();
        poseFollowsTravel();
        stableDirectionalGaits();
        stableLockOnFacing();
        stableGroundContact();
        idleFacing();
        std::cout << "Elden locomotion: " << checks << " checks passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
