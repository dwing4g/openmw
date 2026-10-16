// Regression for the production CharacterController launch/lock policy.
// Uses the actual combat controller; no graphics or game assets are required.
#include <components/eldencombat/jumpmovement.hpp>
#include <components/eldencombat/landingmovement.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    int checks = 0;
    void check(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
        ++checks;
    }

    void close(float value, float expected, const char* message)
    {
        check(std::abs(value - expected) < 1e-4f, message);
    }

    void sprintLaunch()
    {
        EldenCombat::Controller controller;
        EldenCombat::JumpMovement movement;
        EldenCombat::Input input;
        input.mSprint = true;
        input.mMoveY = 1.f;
        const auto running = controller.update(input, EldenCombat::TickSeconds);
        check(controller.sprinting(), "sprint fixture did not reach sprint");
        constexpr float nativeRunSpeed = 225.f;
        const float groundSpeed = nativeRunSpeed * running.mMovementMultiplier;

        input.mJump = { true, true, false };
        auto jump = controller.update(input, EldenCombat::TickSeconds);
        check(jump.mJumpStarted, "sprint jump was not accepted");
        check(jump.mMovementLocked, "fixture no longer covers the action movement lock");
        check(!controller.sprinting(), "jump must retain separate action state");
        movement.update(jump, true, true, true);
        check(!movement.movementLocked(jump), "accepted jump lost horizontal movement before takeoff");
        close(nativeRunSpeed * movement.multiplier(jump), groundSpeed, "jump lost preceding sprint multiplier");
        auto launch = movement.launch(0.f, nativeRunSpeed * movement.multiplier(jump), 300.f);
        close(launch[1], groundSpeed, "launch replaced sprint speed with jump-height-derived speed");
        close(launch[2], 300.f * .707f, "moving jump changed native vertical strength");
        EldenCombat::limitMovement(launch, jump, true, false, false, EldenCombat::TickSeconds);
        close(launch[1], groundSpeed, "final action lock erased takeoff momentum");

        // Another render update can occur before the fixed physics step consumes
        // the native jump pulse. Releasing input must not replace that impulse.
        input.clearEdges();
        input.mSprint = false;
        input.mMoveY = 0.f;
        jump = controller.update(input, EldenCombat::TickSeconds);
        check(!jump.mJumpStarted, "render wait generated a duplicate jump event");
        movement.update(jump, true, true, true);
        check(movement.pending(), "pending launch expired before the physics step");
        launch = movement.launch(0.f, 0.f, 300.f);
        close(launch[1], groundSpeed, "input release erased a pending launch");
        EldenCombat::limitMovement(launch, jump, true, true, false, EldenCombat::TickSeconds);
        close(launch[1], groundSpeed, "hit stop erased the already committed physical impulse");

        input.mGrounded = false;
        jump = controller.update(input, EldenCombat::TickSeconds);
        movement.update(jump, true, false, false);
        check(!movement.pending(), "airborne movement tried to apply launch inertia again");
        std::array<float, 3> airControl = {10.f, 20.f, 0.f};
        EldenCombat::limitMovement(airControl, jump, false, false, false, EldenCombat::TickSeconds);
        check(airControl[0] == 0.f && airControl[1] == 0.f,
            "jump action lock no longer gates new airborne steering");
        close(launch[1], groundSpeed, "air-control gating changed the velocity handed to physics");

        controller.reset();
        input = {};
        input.mJump = { true, true, false };
        jump = controller.update(input, EldenCombat::TickSeconds);
        movement.update(jump, true, true, true);
        launch = movement.launch(0.f, 0.f, 300.f);
        check(launch[0] == 0.f && launch[1] == 0.f, "stationary jump reused earlier sprint momentum");
        close(launch[2], 300.f, "stationary jump changed native vertical strength");
        movement.update(jump, false, true, true);
        check(!movement.pending(), "disabled Elden movement retained a pending launch");
    }

    void movementLimits()
    {
        EldenCombat::Update update;
        update.mAdvancedSeconds = .005f;
        std::array<float, 3> velocity = {40.f, -80.f, 123.f};
        EldenCombat::limitMovement(velocity, update, false, false, false, .02f);
        close(velocity[0], 10.f, "ordinary movement lost partial-hit-stop scaling");
        close(velocity[1], -20.f, "ordinary strafe lost partial-hit-stop scaling");
        close(velocity[2], 123.f, "horizontal limits changed gravity/jump velocity");

        velocity = {40.f, -80.f, 123.f};
        EldenCombat::limitMovement(velocity, update, true, false, false, .02f);
        close(velocity[0], 40.f, "launch was time-scaled for the entire flight");
        close(velocity[1], -80.f, "diagonal launch lost momentum");
        velocity = {40.f, -80.f, 123.f};
        EldenCombat::limitMovement(velocity, update, false, false, true, .02f);
        close(velocity[0], 40.f, "animation displacement was time-scaled twice");
        update.mMovementLocked = true;
        EldenCombat::limitMovement(velocity, update, false, false, true, .02f);
        check(velocity[0] == 0.f && velocity[1] == 0.f, "ordinary attack movement lock was bypassed");
        close(velocity[2], 123.f, "action lock changed vertical physics velocity");

        EldenCombat::JumpMovement pending;
        update.mJumpStarted = true;
        update.mJumpMovementMultiplier = 1.504f;
        pending.update(update, true, true, true);
        pending.update({}, true, true, false);
        check(!pending.pending(), "consumed native jump pulse did not clear launch state");
    }

    void pendingPulse()
    {
        EldenCombat::Controller controller;
        EldenCombat::Config config;
        config.mStaminaRegen = 0.f;
        controller.configure(config);
        EldenCombat::JumpMovement movement;
        EldenCombat::Input input;
        float nativePulse = 1.f;
        int jumps = 0;
        float spent = 0.f;
        const auto renderUpdate = [&] {
            auto controls = input;
            controls.mJump.mPressed = movement.pressed(input.mJump.mPressed, input.mGrounded, nativePulse > 0.f);
            const auto update = controller.update(controls, EldenCombat::TickSeconds);
            if (update.mJumpStarted)
            {
                ++jumps;
                nativePulse = 1.f;
            }
            spent += update.mStaminaSpent;
            movement.update(update, true, input.mGrounded, nativePulse > 0.f);
        };

        renderUpdate();
        check(jumps == 1 && movement.pending(), "native jump pulse did not produce a pending launch");
        const float initialCost = spent;
        check(initialCost > 0.f, "jump fixture did not charge stamina");
        check(movement.pressed(true, true, true), "pending physics pulse suppressed a new real button edge");

        // Several render updates happen before physics consumes the same pulse.
        // Reinjecting it as a press silently buffers a second jump for landing.
        for (int frame = 0; frame < 3; ++frame)
            renderUpdate();
        check(jumps == 1, "render wait emitted another launch");
        nativePulse = 0.f;
        input.mGrounded = false;
        for (int frame = 0; frame < 6; ++frame)
            renderUpdate();
        input.mGrounded = true;
        for (int frame = 0; frame < 90; ++frame)
            renderUpdate();
        check(jumps == 1, "retained native pulse buffered an automatic jump after landing");
        close(spent, initialCost, "retained native pulse charged stamina twice");
        close(controller.stamina(), config.mMaximumStamina - initialCost, "landing spent unexpected stamina");

        EldenCombat::Update jump;
        jump.mJumpStarted = true;
        jump.mJumpMovementMultiplier = 1.504f;
        nativePulse = 1.f;
        movement.update(jump, true, true, true);
        movement.launch(0.f, 300.f, 250.f);
        movement.reset(nativePulse);
        check(!movement.pending() && nativePulse == 0.f, "state reset retained a launch or native jump pulse");
        check(!movement.pressed(false, true, nativePulse > 0.f), "state reset reissued a canceled jump");
        movement.update(jump, true, true, true);
        const auto stationary = movement.launch(0.f, 0.f, 250.f);
        close(stationary[1], 0.f, "state reset reused cached launch velocity");
        close(stationary[2], 250.f, "state reset reused cached moving-jump height");
    }

    float magnitude(EldenCombat::PlanarMovement velocity)
    {
        return std::hypot(velocity.mX, velocity.mY);
    }

    void landingMomentum()
    {
        constexpr float run = 225.f;
        constexpr float sprint = run * 1.504f;
        constexpr float pi = 3.14159265358979323846f;
        for (int direction = 0; direction < 8; ++direction)
        {
            const float angle = direction * pi / 4.f;
            const EldenCombat::PlanarMovement heading = {std::cos(angle), std::sin(angle)};
            const EldenCombat::PlanarMovement launch = {heading.mX * sprint, heading.mY * sprint};
            const EldenCombat::PlanarMovement running = {heading.mX * run, heading.mY * run};
            EldenCombat::LandingMovement landing;
            landing.land(launch, sprint);
            check(landing.active(), "moving landing did not retain momentum");
            close(magnitude(landing.apply(running, 0.f)), sprint, "landing discarded incoming horizontal speed");
            close(magnitude(landing.apply(running, .1f)), (sprint + run) * .5f,
                "landing did not hand sprint speed back to running evenly");
            close(magnitude(landing.apply(running, .1f)), run, "landing did not converge to requested run speed");
            check(!landing.active(), "landing retained momentum after the handoff");

            landing.land(launch, sprint);
            for (int frame = 0; frame < 20; ++frame)
                close(magnitude(landing.apply(launch, .02f)), sprint, "landing added inertia to requested sprint speed");
        }

        for (const int fps : {30, 60, 144})
        {
            EldenCombat::LandingMovement landing;
            landing.land({0.f, sprint}, sprint);
            float elapsed = 0.f;
            const float step = 1.f / fps;
            EldenCombat::PlanarMovement velocity;
            while (elapsed + step < .20f - 1e-6f)
            {
                elapsed += step;
                velocity = landing.apply({}, step);
                close(velocity.mY, sprint * (1.f - elapsed / .20f),
                    "landing release decay depends on update rate");
                check(landing.active(), "landing stopped before the handoff elapsed");
            }
            velocity = landing.apply({}, .20f - elapsed);
            close(magnitude(velocity), 0.f, "released landing did not stop within 0.20 seconds");
            check(!landing.active(), "landing release left an unbounded slide");
            velocity = landing.apply({run, 0.f}, step);
            close(velocity.mX, run, "expired momentum delayed subsequent ground controls");
        }

        EldenCombat::LandingMovement landing;
        landing.land({0.f, sprint}, sprint);
        auto velocity = landing.apply({run, 0.f}, .1f);
        close(velocity.mX, run * .5f, "landing ignored new steering");
        close(velocity.mY, sprint * .5f, "landing steering erased forward momentum immediately");
        check(magnitude(velocity) <= sprint, "landing steering gained speed");
        velocity = landing.apply({run, 0.f}, .1f);
        close(velocity.mX, run, "landing steering did not reach ground direction");
        close(velocity.mY, 0.f, "landing steering kept old direction indefinitely");

        landing.land({0.f, sprint}, sprint);
        landing.apply({}, .05f);
        landing.collide({run, 0.f}, sprint);
        velocity = landing.apply({}, 0.f);
        close(velocity.mX, run, "landing restored pre-wall velocity after collision");
        close(velocity.mY, 0.f, "landing retained velocity into the wall");
        landing.apply({}, .15f);
        check(!landing.active(), "collision restarted the landing clock");
        landing.land({0.f, sprint}, sprint);
        landing.collide({}, sprint);
        check(!landing.active(), "wall stop retained blocked momentum");
        velocity = landing.apply({}, .01f);
        close(magnitude(velocity), 0.f, "wall stop produced sliding without input");

        // The collision solver may project a large falling velocity onto a
        // slope. Only the pre-collision horizontal speed is transferable.
        landing.land({600.f, 800.f}, run);
        close(magnitude(landing.apply({}, 0.f)), run, "slope landing converted falling speed into a boost");
        landing.collide({600.f, 800.f}, 100.f);
        close(magnitude(landing.apply({}, 0.f)), 100.f, "later collision exceeded its horizontal speed budget");
        landing.land({600.f, 800.f}, 0.f);
        check(!landing.active(), "stationary fall generated horizontal momentum");

        landing.land({0.f, sprint}, sprint);
        landing.reset();
        check(!landing.active(), "teleport, disable, or new jump retained landing momentum");
        close(magnitude(landing.apply({}, 0.f)), 0.f, "reset restored an old landing velocity");
        landing.land({run, 0.f}, run);
        velocity = landing.apply({}, 0.f);
        close(velocity.mX, run, "a subsequent jump reused the previous landing");
        close(velocity.mY, 0.f, "a subsequent jump retained the old direction");

        landing.land({0.f, sprint}, sprint);
        close(magnitude(landing.apply({}, -.1f)), sprint, "negative time reversed landing decay");
        close(magnitude(landing.apply({}, std::numeric_limits<float>::quiet_NaN())), sprint,
            "nonfinite time corrupted landing velocity");
        landing.land({std::numeric_limits<float>::quiet_NaN(), 0.f}, run);
        check(!landing.active(), "nonfinite collision velocity entered landing state");
        landing.land({run, 0.f}, -1.f);
        check(!landing.active(), "negative speed budget retained landing momentum");
        landing.collide({run, 0.f}, run);
        check(!landing.active(), "ordinary ground collision created new landing momentum");
    }

    void landingAction()
    {
        EldenCombat::Controller controller;
        EldenCombat::Input input;
        input.mMoveY = 1.f;
        input.mSprint = true;
        controller.update(input, EldenCombat::TickSeconds);
        check(controller.sprinting(), "landing fixture did not start sprinting");
        input.mJump = {true, true, false};
        auto update = controller.update(input, EldenCombat::TickSeconds);
        check(update.mJumpStarted, "landing fixture did not start jumping");
        input.clearEdges();
        for (int render = 0; render < 3; ++render)
        {
            update = controller.update(input, EldenCombat::TickSeconds);
            check(controller.action() == EldenCombat::Action::Jump,
                "grounded render before physics takeoff canceled the pending jump");
            check(!controller.sprinting(), "pending takeoff restarted grounded sprint early");
        }
        input.mGrounded = false;
        update = controller.update(input, EldenCombat::TickSeconds);
        check(update.mMovementLocked, "short jump fixture did not cover the imported movement lock");
        input.mGrounded = true;
        update = controller.update(input, EldenCombat::TickSeconds);
        check(controller.action() == EldenCombat::Action::Idle,
            "confirmed landing waited for the full imported jump clip");
        check(!update.mMovementLocked, "confirmed landing kept the jump movement lock");
        check(controller.sprinting(), "held sprint did not resume on the first landing tick");
        close(update.mMovementMultiplier, 1.504f, "landing lost the held sprint movement speed");

        controller.reset();
        input = {};
        input.mJump = {true, true, false};
        controller.update(input, EldenCombat::TickSeconds);
        input.clearEdges();
        controller.applyHitStop(.1f);
        input.mGrounded = false;
        update = controller.update(input, .05f);
        check(update.mFrozen && controller.action() == EldenCombat::Action::Jump,
            "airborne hit stop advanced jump action");
        input.mGrounded = true;
        update = controller.update(input, .025f);
        check(update.mFrozen && controller.action() == EldenCombat::Action::Jump,
            "landing ended an action during hit stop");
        update = controller.update(input, .05f);
        check(!update.mFrozen && controller.action() == EldenCombat::Action::Idle && !update.mMovementLocked,
            "airborne observation during hit stop was lost before the landing thawed");

        for (const bool heavy : {false, true})
        {
            controller.reset();
            input = {};
            input.mGrounded = false;
            if (heavy)
                input.mHeavy = {true, true, false};
            else
                input.mLight = {true, true, false};
            controller.update(input, EldenCombat::TickSeconds);
            const auto attack = heavy ? EldenCombat::Action::JumpHeavy : EldenCombat::Action::JumpLight;
            check(controller.action() == attack, "jump-attack landing fixture selected the wrong action");
            input.clearEdges();
            controller.update(input, EldenCombat::TickSeconds);
            input.mGrounded = true;
            input.mMoveY = 1.f;
            input.mSprint = true;
            update = controller.update(input, EldenCombat::TickSeconds);
            check(controller.action() == attack, "touchdown skipped jump-attack recovery");
            check(!controller.sprinting(), "jump-attack landing resumed sprint before recovery");
        }
    }

#ifdef OPENMW_ELDENCOMBAT_IMPORTED_TIMINGS
    void jumpAttackLanding()
    {
        using namespace EldenCombat;
        for (const auto weapon : {WeaponClass::StraightSword, WeaponClass::Spear, WeaponClass::Twinblade})
        for (const auto action : {Action::JumpLight, Action::JumpHeavy, Action::PairedJump})
        for (const bool twoHanded : {false, true})
        for (const float speed : {.75f, 1.f, 1.3f})
        for (const int fps : {30, 60, 144})
        {
            if (action == Action::PairedJump && twoHanded)
                continue;
            Config config;
            config.mWeaponClass = config.mLeftWeaponClass = weapon;
            config.mHasShield = false;
            config.mHasLeftWeapon = config.mPairedWeapons = true;
            config.mStaminaRegen = 0.f;
            config.mSpeedMultiplier = speed;
            const auto air = Controller::actionDefinition(action, config, twoHanded);
            check(air.mLanding && air.mLandingShort, "jump attack lost imported landing clips");
            const float step = 1.f / fps;
            // Early touchdown carries the hit; a completed or connected swing
            // must recover without hitting or paying for the attack again.
            for (int scenario = 0; scenario < 3; ++scenario)
            {
                Controller controller;
                controller.configure(config);
                Input input;
                input.mGrounded = false;
                input.mTwoHanded = twoHanded;
                if (action == Action::JumpHeavy)
                    input.mHeavy = {true, true, false};
                else if (action == Action::PairedJump)
                    input.mGuard = {true, true, false};
                else
                    input.mLight = {true, true, false};
                // A single simulation slice starts at frame zero, even when
                // the next render update lands immediately after the attack.
                const auto started = controller.update(input, TickSeconds);
                check(controller.action() == action, "airborne opener selected the wrong attack");
                const auto sequence = controller.sequence();
                const float stamina = controller.stamina();
                check(started.mStaminaSpent > 0.f, "airborne swing did not pay stamina at initiation");
                input.mHeavy = input.mGuard = input.mLight = {};
                if (scenario == 1)
                {
                    // A long fall outlives the old merged duration. Holding
                    // movement must still leave a landing phase to play.
                    input.mMoveY = 1.f;
                    while (controller.frame() <= air.mDuration + 1.f)
                    {
                        controller.update(input, step);
                        check(controller.action() == action && controller.definition().mAnimation == air.mAnimation,
                            "airborne attack recovered before reaching the ground");
                    }
                    input.mMoveY = 0.f;
                }
                else if (scenario == 2)
                {
                    while (controller.frame() < air.mHits[0].mFrom)
                        controller.update(input, step);
                    check(controller.activeHit().has_value(), "airborne hit window was missed");
                    controller.markHit();
                }
                const float airFrame = controller.frame();
                const bool finished = airFrame >= air.mHits[0].mTo;
                const auto& landing = finished ? *air.mLandingShort : *air.mLanding;
                input.mGrounded = true;
                auto update = controller.update(input, step);
                check(controller.action() == action && controller.definition().mAnimation == landing.mAnimation,
                    "touchdown retained the frozen airborne clip");
                check(controller.sequence() == sequence && !update.mAttackStarted,
                    "touchdown restarted the attack or its swing sound");
                const float initialFrame = finished ? 0.f : std::min(airFrame, landing.mHits[0].mFrom);
                close(controller.frame(), initialFrame + step * AnimationFps,
                    "landing animation did not start on its own clock");

                int hits = 0;
                const int ticks = static_cast<int>(std::ceil(landing.mDuration / AnimationFps / step)) + 2;
                for (int tick = 0; tick < ticks && controller.action() == action; ++tick)
                {
                    while (controller.activeHit())
                    {
                        ++hits;
                        controller.markHit();
                    }
                    const float previousFrame = controller.frame();
                    update = controller.update(input, step);
                    if (controller.action() == action)
                    {
                        check(controller.frame() > previousFrame, "landing pose stopped advancing");
                        check(controller.definition().mAnimation == landing.mAnimation,
                            "grounded updates restarted the landing phase");
                    }
                }
                check(controller.action() == Action::Idle && !update.mMovementLocked,
                    "jump-attack recovery failed to release idle and movement");
                check(scenario == 0 ? hits > 0 : hits == 0,
                    "landing lost an unfinished swing or repeated a completed airborne hit");
                close(controller.stamina(), stamina, "landing charged attack stamina a second time");
            }
        }
    }

    void jumpAttackLandingHitStop()
    {
        using namespace EldenCombat;
        Controller controller;
        Input input;
        input.mGrounded = false;
        input.mHeavy = {true, true, false};
        controller.update(input, TickSeconds);
        const auto air = controller.definition();
        input.mHeavy = {};
        controller.applyHitStop(.1f);
        input.mGrounded = true;
        auto update = controller.update(input, .05f);
        check(update.mFrozen && controller.definition().mAnimation == air.mAnimation,
            "landing advanced an attack during hit-stop");
        update = controller.update(input, .1f);
        check(!update.mFrozen && controller.definition().mAnimation == air.mLanding->mAnimation,
            "landing was lost when hit-stop ended");
        close(controller.frame(), .05f * AnimationFps, "landing advanced through the frozen part of the update");

        // Held run/sprint should leave recovery at its authored movement cancel.
        input.mMoveY = 1.f;
        input.mSprint = true;
        const float cancelMove = controller.definition().mCancelMove;
        while (controller.action() != Action::Idle)
        {
            check(controller.frame() < cancelMove + 1.f, "landing failed to honor movement cancel");
            controller.update(input, TickSeconds);
        }
        update = controller.update(input, TickSeconds);
        check(controller.sprinting() && !update.mMovementLocked,
            "held sprint did not resume after jump-attack recovery");
    }

    void pendingJumpAttack()
    {
        using namespace EldenCombat;
        Controller controller;
        Input input;
        input.mJump = {true, true, false};
        controller.update(input, TickSeconds);
        const float cancelLight = controller.definition().mCancelLight;
        input.mJump = {};
        input.mLight = {true, true, false};
        controller.update(input, TickSeconds);
        input.mLight = {};
        while (controller.frame() < cancelLight + 1.f)
        {
            controller.update(input, TickSeconds);
            check(controller.action() == Action::Jump,
                "buffered attack canceled the physical jump before takeoff");
        }
        input.mGrounded = false;
        controller.update(input, TickSeconds);
        check(controller.action() == Action::JumpLight, "takeoff lost the buffered aerial attack");
        const auto landing = controller.definition().mLanding;
        input.mGrounded = true;
        controller.update(input, TickSeconds);
        check(controller.definition().mAnimation == landing->mAnimation,
            "attack immediately followed by touchdown missed the landing transition");

        controller.reset();
        input = {};
        input.mGrounded = false;
        input.mLight = {true, true, false};
        controller.update(input, TickSeconds);
        input.mLight = {};
        const auto air = controller.definition();
        while (controller.frame() < air.mDuration)
            controller.update(input, TickSeconds);
        input.mLight = {true, true, false};
        controller.update(input, TickSeconds);
        input.mLight = {};
        check(controller.action() == Action::JumpLight && controller.definition().mAnimation == air.mAnimation,
            "buffered follow-up restarted a swing while still airborne");
        input.mGrounded = true;
        controller.update(input, TickSeconds);
        check(controller.definition().mAnimation == air.mLandingShort->mAnimation,
            "buffered follow-up bypassed landing recovery");
        const float cancel = controller.definition().mCancelLight;
        while (controller.action() == Action::JumpLight)
        {
            check(controller.frame() < cancel + 1.f, "landing never released the buffered follow-up");
            controller.update(input, TickSeconds);
        }
        check(controller.action() == Action::Light1, "landing lost the queued ground attack");
    }
#endif
}

int main()
{
    try
    {
        sprintLaunch();
        movementLimits();
        pendingPulse();
        landingMomentum();
        landingAction();
#ifdef OPENMW_ELDENCOMBAT_IMPORTED_TIMINGS
        jumpAttackLanding();
        jumpAttackLandingHitStop();
        pendingJumpAttack();
#endif
        std::cout << "Jump and landing validation passed: " << checks << " assertions\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
