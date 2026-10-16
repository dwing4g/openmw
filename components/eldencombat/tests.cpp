#include "combat.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    using namespace EldenCombat;

    void check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void close(float actual, float expected, const char* message)
    {
        check(std::abs(actual - expected) < 0.001f, message);
    }

    void run(Controller& controller, const Input& input, int ticks)
    {
        for (int tick = 0; tick < ticks; ++tick)
            controller.update(input, TickSeconds);
    }

    void toFrame(Controller& controller, float frame, Input input = {})
    {
        for (int tick = 0; controller.frame() < frame && tick < 2000; ++tick)
            controller.update(input, TickSeconds);
        check(controller.frame() >= frame, "action did not reach the expected frame");
    }

    Input downLight()
    {
        Input input;
        input.mLight = { true, true, false };
        return input;
    }

    void roll(Controller& controller, float x = 0.f, float y = 1.f)
    {
        Input input;
        input.mMoveX = x;
        input.mMoveY = y;
        input.mDodge = { true, true, false };
        controller.update(input, TickSeconds);
        check(controller.action() == Action::Idle, "roll fired before release");
        input.mDodge = { false, false, true };
        controller.update(input, TickSeconds);
    }

    void dodgeAndStamina()
    {
        Controller controller;
        roll(controller);
        check(controller.action() == Action::Roll, "release did not roll");
        close(controller.stamina(), 88.f, "roll stamina cost");
        int invincibleTicks = 0;
        float distance = 0.f;
        while (controller.action() == Action::Roll)
        {
            invincibleTicks += controller.invincible();
            const Update update = controller.update({}, TickSeconds);
            distance += update.mDisplacementY;
        }
        check(invincibleTicks == 26, "medium roll does not provide 13 animation frames of invincibility");
        close(distance, 3.65f, "medium roll distance");

        Controller backstep;
        roll(backstep, 0.f, 0.f);
        check(backstep.action() == Action::Backstep, "directionless dodge should backstep");
        close(backstep.stamina(), 92.f, "backstep stamina cost");
        check(backstep.receiveHit({}) == HitResult::Dodged, "backstep invincibility");

        for (const Load load : { Load::Light, Load::Heavy })
        {
            Controller character;
            Config config;
            config.mLoad = load;
            character.configure(config);
            roll(character, 1.f, 0.f);
            float traveled = 0.f;
            while (character.action() == Action::Roll)
                traveled += character.update({}, TickSeconds).mDisplacementX;
            close(traveled, load == Load::Light ? 4.33f : 3.31f, "load roll distance");
        }

        Controller exhausted;
        exhausted.setVitals(0.f, 100.f);
        Input tap;
        tap.mDodge = { false, true, true };
        tap.mMoveY = 1.f;
        exhausted.update(tap, TickSeconds);
        check(exhausted.action() == Action::Idle, "zero stamina must prevent dodge");
        run(exhausted, {}, 30);
        check(exhausted.action() == Action::Idle, "rejected dodge must not wait for regeneration");
    }

    void sprinting()
    {
        Controller controller;
        Input input;
        input.mMoveY = 1.f;
        input.mDodge = { true, true, false };
        controller.update(input, TickSeconds);
        input.clearEdges();
        run(controller, input, 60);
        check(controller.sprinting(), "holding dodge should sprint");
        close(controller.stamina(), 100.f, "out of combat sprint should be free");
        input.mInCombat = true;
        run(controller, input, 60);
        close(controller.stamina(), 89.f, "in combat sprint drain");
        input.mDodge = { false, false, true };
        controller.update(input, TickSeconds);
        check(controller.action() == Action::Idle && !controller.sprinting(), "sprint release should not roll");

        Controller exhausted;
        exhausted.setVitals(0.1f, 100.f);
        input.mDodge = {};
        input.mSprint = true;
        run(exhausted, input, 30);
        check(!exhausted.sprinting(), "exhausted sprint should require release");
        input.mSprint = false;
        exhausted.update(input, TickSeconds);
        input.mSprint = true;
        exhausted.update(input, TickSeconds);
        check(exhausted.sprinting(), "releasing should rearm sprint");
    }

    void sprintTerrainContact()
    {
        Input input;
        input.mMoveY = 1.f;
        input.mDodge = { true, true, false };
        Controller runner;
        runner.update(input, 0.4f);
        input.clearEdges();
        check(runner.sprinting(), "terrain fixture must establish a held-button sprint");
        input.mInCombat = true;
        // Small steps can repeatedly lose and regain contact while the button
        // stays held. Neither speed nor stamina charging should alternate.
        for (int step = 0; step < 3; ++step)
        {
            input.mGrounded = false;
            const float before = runner.stamina();
            const Update gap = runner.update(input, 0.05f);
            check(runner.sprinting(), "brief terrain contact loss cancelled held sprint");
            close(gap.mMovementMultiplier, 1.504f, "terrain gap dropped sprint speed");
            close(before - runner.stamina(), 0.55f, "terrain gap bypassed combat sprint drain");
            input.mGrounded = true;
            runner.update(input, TickSeconds);
            check(runner.sprinting(), "regaining ground contact interrupted held sprint");
        }
        input.mGrounded = false;
        runner.update(input, 0.f);
        check(runner.sprinting(), "zero-time update consumed sprint ground grace");
        runner.update(input, 0.2f);
        check(!runner.sprinting(), "a genuine fall retained a grounded sprint pose");
        close(runner.update(input, TickSeconds).mMovementMultiplier, 1.f,
            "expired ground grace retained sprint speed");
        input.mGrounded = true;
        runner.update(input, TickSeconds);
        check(runner.sprinting(), "held sprint failed to resume on landing");

        input.mGrounded = false;
        input.mDodge = { false, false, true };
        runner.update(input, TickSeconds);
        check(!runner.sprinting(), "ground grace ignored sprint button release");
        input.mDodge = { true, true, false };
        runner.update(input, 0.4f);
        input.clearEdges();
        check(!runner.sprinting(), "sprint restarted while airborne");
        input.mGrounded = true;
        runner.update(input, TickSeconds);
        check(runner.sprinting(), "airborne sprint hold failed to become active on landing");

        input.mGrounded = false;
        input.mLight = { true, true, false };
        runner.update(input, TickSeconds);
        check(runner.action() == Action::JumpLight && !runner.sprinting(),
            "sprint grace converted an airborne attack into a running attack");
        input.mLight = {};

        for (const Action airborne : { Action::JumpHeavy, Action::PairedJump })
        {
            Controller attacker;
            Config config;
            config.mHasShield = false;
            config.mHasLeftWeapon = true;
            config.mPairedWeapons = true;
            attacker.configure(config);
            Input attack;
            attack.mMoveY = 1.f;
            attack.mSprint = true;
            attacker.update(attack, TickSeconds);
            attack.mGrounded = false;
            if (airborne == Action::JumpHeavy)
                attack.mHeavy = { true, true, false };
            else
                attack.mGuard = { true, true, false };
            attacker.update(attack, TickSeconds);
            check(attacker.action() == airborne && !attacker.sprinting(),
                "ground grace selected a grounded heavy/paired attack while airborne");
        }

        Controller exhausted;
        input.mGrounded = true;
        exhausted.update(input, 0.4f);
        exhausted.setVitals(0.1f, 100.f);
        input.mGrounded = false;
        exhausted.update(input, TickSeconds);
        check(!exhausted.sprinting(), "terrain grace continued sprint after exhaustion");
        input.mGrounded = true;
        run(exhausted, input, 30);
        check(!exhausted.sprinting(), "exhaustion during ground grace did not require button release");

        Controller fresh;
        input.mGrounded = false;
        input.mSprint = true;
        fresh.update(input, 0.4f);
        check(!fresh.sprinting(), "fresh airborne actor could start sprint without ground contact");
    }

    void bufferedDodgeDirection()
    {
        const auto prepare = [](Controller& controller) {
            controller.update(downLight(), TickSeconds);
            toFrame(controller, controller.definition().mDodgeInputFrom);
            check(controller.frame() < controller.definition().mCancelDodge,
                "direction fixture must buffer before the dodge cancel window");
        };
        const auto execute = [](Controller& controller, const Input& movement) {
            Update update;
            for (int tick = 0; tick < 300 && controller.action() != Action::Roll
                && controller.action() != Action::Backstep; ++tick)
                update = controller.update(movement, TickSeconds);
            check(controller.action() == Action::Roll || controller.action() == Action::Backstep,
                "buffered dodge did not execute");
            return update;
        };
        Input request;
        request.mDodge = { false, true, true };
        request.mDodgeDirection = std::array{ -1.f, 0.f };
        Input changedMovement;
        changedMovement.mMoveY = 1.f;

        Controller checked;
        prepare(checked);
        checked.applyHitStop(0.2f);
        const float before = checked.frame();
        checked.update(request, TickSeconds);
        check(checked.action() == Action::Light1, "checked dodge should still be buffered during hitstop");
        Input cleared = request;
        cleared.clearEdges();
        check(!cleared.mDodgeDirection, "clearing edges should consume the direction override");
        run(checked, changedMovement, 5);
        close(checked.frame(), before, "hitstop should freeze the buffered dodge's cancel timer");
        const Update checkedRoll = execute(checked, changedMovement);
        close(checkedRoll.mMoveX, -1.f, "buffered AI dodge lost its checked lateral direction");
        close(checkedRoll.mMoveY, 0.f, "later navigation redirected a checked AI dodge");

        Controller player;
        prepare(player);
        Input playerRequest = request;
        playerRequest.mDodgeDirection.reset();
        playerRequest.mMoveX = -1.f;
        player.update(playerRequest, TickSeconds);
        const Update playerRoll = execute(player, changedMovement);
        close(playerRoll.mMoveX, 0.f, "ordinary dodge should still use movement when executed");
        close(playerRoll.mMoveY, 1.f, "ordinary dodge direction behavior changed");

        Controller backstep;
        prepare(backstep);
        Input backstepRequest = request;
        backstepRequest.mDodgeDirection = std::array{ 0.f, 0.f };
        backstep.update(backstepRequest, TickSeconds);
        execute(backstep, changedMovement);
        check(backstep.action() == Action::Backstep, "checked backstep became a navigation-directed roll");

        Controller stopped;
        prepare(stopped);
        stopped.update(request, TickSeconds);
        stopped.clearRequests();
        run(stopped, {}, 130);
        check(stopped.action() == Action::Idle, "clearing requests should discard a checked dodge");
        roll(stopped);
        const Update newRoll = stopped.update({}, TickSeconds);
        close(newRoll.mMoveX, 0.f, "cleared direction leaked into the next dodge");
        close(newRoll.mMoveY, 1.f, "clearing requests changed the next ordinary dodge");

        Controller overwritten;
        prepare(overwritten);
        overwritten.update(request, TickSeconds);
        overwritten.update(downLight(), TickSeconds);
        while (overwritten.action() == Action::Light1)
            overwritten.update({}, TickSeconds);
        check(overwritten.action() == Action::Light2, "newer attack should replace the checked dodge");
        toFrame(overwritten, overwritten.definition().mDodgeInputFrom);
        overwritten.update(playerRequest, TickSeconds);
        const Update replacementRoll = execute(overwritten, changedMovement);
        close(replacementRoll.mMoveX, 0.f, "overwritten direction leaked into a later dodge");
        close(replacementRoll.mMoveY, 1.f, "replacement dodge should use current movement");
    }

    void chainsAndHitWindows()
    {
        Controller controller;
        controller.update(downLight(), TickSeconds);
        check(controller.action() == Action::Light1, "light attack opener");
        controller.update(downLight(), TickSeconds);
        run(controller, {}, 130);
        check(controller.action() == Action::Idle, "press before listening must be discarded");

        controller.update(downLight(), TickSeconds);
        toFrame(controller, controller.definition().mInputFrom);
        controller.update(downLight(), TickSeconds);
        while (controller.action() == Action::Light1)
            controller.update({}, TickSeconds);
        check(controller.action() == Action::Light2, "queued light should advance the combo");

        Controller stopped;
        stopped.update(downLight(), TickSeconds);
        toFrame(stopped, stopped.definition().mInputFrom);
        stopped.update(downLight(), TickSeconds);
        stopped.clearRequests();
        run(stopped, {}, 130);
        check(stopped.action() == Action::Idle, "stopping tactical ownership should clear queued attacks");
        stopped.update(downLight(), TickSeconds);
        check(stopped.action() == Action::Light1, "new combat should start a fresh chain");

        Controller stamina;
        stamina.update(downLight(), TickSeconds);
        toFrame(stamina, stamina.definition().mHits[0].mFrom - 0.5f);
        close(stamina.stamina(), 100.f, "attack should not charge stamina before hit window");
        stamina.update({}, TickSeconds);
        close(stamina.stamina(), 88.f, "attack should charge stamina at the hit window");
        check(stamina.activeHit().has_value(), "active hit not reported");
        stamina.markHit();
        check(!stamina.activeHit(), "same hit must not land twice");

        Controller lowFps;
        lowFps.update(downLight(), TickSeconds);
        lowFps.update({}, 1.f);
        check(lowFps.frame() > lowFps.definition().mHits[0].mTo, "low FPS fixture must cross the whole window");
        check(lowFps.activeHit().has_value(), "low FPS must preserve a crossed hit window");
        lowFps.update({}, TickSeconds);
        check(!lowFps.activeHit(), "expired missed hit should clear on next update");

        Controller ended;
        Config fast;
        fast.mSpeedMultiplier = 5.f;
        ended.configure(fast);
        ended.update(downLight(), TickSeconds);
        ended.update({}, 1.f);
        check(ended.action() == Action::Idle, "slow frame fixture must finish the entire action");
        check(ended.activeHit().has_value(), "crossed hit is still pending");
        ended.markHit();
        check(!ended.activeHit(), "crossed hit consumption");

        Controller multi;
        Config config;
        config.mWeaponClass = WeaponClass::Twinblade;
        multi.configure(config);
        multi.update(downLight(), TickSeconds);
        multi.update({}, 0.8f);
        check(multi.activeHit().has_value() && multi.activeHit()->mHitIndex == 0, "first multi-hit window");
        multi.markHit();
        check(multi.activeHit().has_value() && multi.activeHit()->mHitIndex == 1, "second multi-hit window");
        multi.markHit();
        check(!multi.activeHit(), "multi-hit consumed more than twice");

        Controller left;
        config.mWeaponClass = WeaponClass::StraightSword;
        config.mLeftWeaponClass = WeaponClass::Dagger;
        config.mHasShield = false;
        config.mHasLeftWeapon = true;
        left.configure(config);
        Input leftPress;
        leftPress.mGuard = { false, true, true };
        left.update(leftPress, TickSeconds);
        check(left.action() == Action::LeftLight1 && left.attack().mLeftHand, "left-hand attack ownership");

        Controller paired;
        config.mLeftWeaponClass = WeaponClass::StraightSword;
        config.mPairedWeapons = true;
        paired.configure(config);
        paired.update(leftPress, TickSeconds);
        paired.update({}, 0.7f);
        check(paired.activeHit()->mPaired && !paired.activeHit()->mLeftHand, "paired right-hand first hit");
        paired.markHit();
        check(paired.activeHit()->mPaired && paired.activeHit()->mLeftHand, "paired left-hand second hit");

        Controller leftGrip;
        config.mPairedWeapons = false;
        config.mWeaponClass = config.mLeftWeaponClass;
        leftGrip.configure(config);
        Input grip = downLight();
        grip.mTwoHanded = true;
        grip.mTwoHandedLeft = true;
        leftGrip.update(grip, TickSeconds);
        check(leftGrip.action() == Action::Light1 && leftGrip.attack().mTwoHanded
            && leftGrip.attack().mLeftHand, "two-handing left routes primary attacks to the left weapon");
        grip.clearEdges();
        toFrame(leftGrip, leftGrip.definition().mHits[0].mFrom, grip);
        check(leftGrip.activeHit()->mLeftHand, "left grip hit retains left-hand ownership");

        Controller shieldGrip;
        config.mWeaponClass = config.mLeftWeaponClass = WeaponClass::Shield;
        config.mHasShield = true;
        shieldGrip.configure(config);
        grip.mLight = { true, true, false };
        shieldGrip.update(grip, TickSeconds);
        check(shieldGrip.action() == Action::Light1 && shieldGrip.attack().mTwoHanded
            && shieldGrip.attack().mLeftHand, "two-handed shield primary attacks retain the shield hand");
    }

    void heavyAttacks()
    {
        Input heavy;
        heavy.mHeavy = { true, true, false };
        Controller held;
        held.update(heavy, TickSeconds);
        check(held.action() == Action::Heavy1Charge, "heavy should enter charge windup");
        heavy.clearEdges();
        toFrame(held, held.definition().mHits[0].mFrom, heavy);
        check(held.action() == Action::Heavy1Charge, "holding heavy should preserve charged attack");
        close(held.activeHit()->mDamageMultiplier, 1.6f, "charged heavy motion value");
        close(held.stamina(), 70.f, "charged heavy stamina");

        Controller released;
        heavy.mHeavy = { true, true, false };
        released.update(heavy, TickSeconds);
        while (released.action() == Action::Heavy1Charge)
            released.update({}, TickSeconds);
        check(released.action() == Action::Heavy1, "releasing should select ordinary heavy");
        close(released.attack().mDamageMultiplier, 1.25f, "ordinary heavy motion value");
        toFrame(released, released.definition().mHits[0].mFrom);
        close(released.stamina(), 80.f, "ordinary heavy stamina");
    }

    void guardAndCounters()
    {
        Input guard;
        guard.mGuard.mHeld = true;
        Controller tooEarly;
        tooEarly.update(guard, TickSeconds);
        Incoming hit;
        hit.mStaminaDamage = 40.f;
        check(tooEarly.receiveHit(hit) == HitResult::Hit, "guard raise delay must apply");

        Controller blocker;
        run(blocker, guard, 8);
        check(blocker.guardUp(), "guard should raise after four frames");
        check(blocker.receiveHit(hit) == HitResult::Blocked, "frontal guard should block");
        close(blocker.stamina(), 78.f, "block stamina drain");
        check(blocker.guardCounterReady(), "block should open guard counter window");
        check(!blocker.guardUp(), "guard recoil must not keep an active guard without held input");
        Input counter;
        counter.mHeavy = { true, true, false };
        blocker.update(counter, TickSeconds);
        run(blocker, {}, 20);
        check(blocker.action() == Action::GuardCounter, "heavy after block should counter");
        check(!blocker.guardCounterReady(), "counter should consume availability");

        Controller heldGuard;
        run(heldGuard, guard, 8);
        check(heldGuard.receiveHit(hit) == HitResult::Blocked, "held guard first hit");
        check(!heldGuard.guardUp(), "guard recoil should expose its recovery window");
        run(heldGuard, guard, 22);
        check(heldGuard.guardUp(), "continued guard hold should resume without another raise delay");

        Controller releasedGuard;
        run(releasedGuard, guard, 8);
        releasedGuard.receiveHit(hit);
        run(releasedGuard, {}, 65);
        check(!releasedGuard.guardUp(), "released guard must stay down after recoil ends");

        Controller rear;
        run(rear, guard, 8);
        hit.mFacingDot = -1.f;
        check(rear.receiveHit(hit) == HitResult::Hit, "shield should not cover attacks from behind");
        hit.mFacingDot = 1.f;

        Controller broken;
        run(broken, guard, 8);
        broken.setVitals(1.f, 100.f);
        check(broken.receiveHit(hit) == HitResult::GuardBroken, "insufficient stamina should break guard");
        close(broken.stamina(), 0.f, "guard break must clamp stamina");
        check(broken.criticalReady(), "guard break should expose a critical opportunity");

        Controller noShield;
        Config config;
        config.mHasShield = false;
        noShield.configure(config);
        run(noShield, guard, 20);
        check(!noShield.guarding(), "one-handed weapon without shield cannot guard");
        guard.mTwoHanded = true;
        run(noShield, guard, 8);
        check(noShield.guardUp(), "two-handed weapon should guard");

        Controller leftGuard;
        leftGuard.configure(config);
        guard.mTwoHandedLeft = true;
        run(leftGuard, guard, 8);
        check(leftGuard.receiveHit(hit) == HitResult::Blocked, "left two-handed grip should guard");
        Input leftCounter;
        leftCounter.mHeavy = { true, true, false };
        leftCounter.mTwoHanded = leftCounter.mTwoHandedLeft = true;
        leftGuard.update(leftCounter, TickSeconds);
        run(leftGuard, leftCounter, 20);
        check(leftGuard.action() == Action::GuardCounter && leftGuard.attack().mLeftHand,
            "left grip should retain its hand through guard recoil and counter");

        Controller regeneration;
        regeneration.setVitals(40.f, 100.f);
        guard.mTwoHanded = false;
        guard.mTwoHandedLeft = false;
        run(regeneration, guard, 12);
        close(regeneration.stamina(), 44.5f, "guard regeneration rate");
    }

    void stanceJumpAndHitStop()
    {
        Controller target;
        Incoming hit;
        hit.mStanceDamage = 35.f;
        check(target.receiveHit(hit) == HitResult::Hit, "first stance hit should not break");
        check(target.receiveHit(hit) == HitResult::StanceBroken, "accumulated stance damage should break");
        check(target.criticalReady() && target.consumeCritical(), "critical should consume vulnerability");
        check(!target.consumeCritical(), "critical vulnerability may only be consumed once");

        Controller recovery;
        recovery.receiveHit(hit);
        run(recovery, {}, 301);
        close(recovery.stance(), 60.f, "stance should recover after five seconds without hits");

        Controller jumper;
        Input input;
        input.mGrounded = false;
        jumper.update(input, TickSeconds);
        hit.mLow = true;
        hit.mAirborneClearance = 0.5f;
        check(jumper.receiveHit(hit) == HitResult::Jumped, "low sweep should miss elevated airborne actor");
        hit.mLow = false;
        check(jumper.receiveHit(hit) == HitResult::Hit, "non-low attack should still hit airborne actor");
        check(jumper.action() == Action::Idle, "air hit should not force a ground reaction");

        Controller jumpAttack;
        input = {};
        input.mJump = { false, true, true };
        jumpAttack.update(input, TickSeconds);
        run(jumpAttack, {}, 12);
        input = {};
        input.mGrounded = false;
        input.mHeavy = { true, true, false };
        jumpAttack.update(input, TickSeconds);
        check(jumpAttack.action() == Action::JumpHeavy, "airborne heavy should select the jump attack");
        close(jumpAttack.stamina(), 70.f, "jump attack pays at initiation");
        input.clearEdges();
        jumpAttack.update(input, 0.6f);
        close(jumpAttack.stamina(), 70.f, "jump attack must not pay twice at the hit window");

        Controller frozen;
        frozen.update(downLight(), TickSeconds);
        frozen.applyHitStop(0.2f);
        const Update update = frozen.update({}, 0.1f);
        check(update.mFrozen, "hit-stop should report a freeze");
        close(update.mAdvancedSeconds, 0.f, "fully frozen update must not advance animation time");
        close(frozen.frame(), 0.f, "hit-stop must freeze action time");
        close(frozen.stamina(), 100.f, "hit-stop must not change stamina");
        frozen.update({}, 0.2f);
        check(frozen.frame() > 0.f, "action should resume after hit-stop");

        Controller scaled;
        scaled.setVitals(250.f, 250.f);
        roll(scaled);
        close(scaled.stamina(), 220.f, "stamina costs must scale to native fatigue maximum");
        scaled.applyHitStop(0.2f);
        scaled.setDead(true);
        check(scaled.receiveHit(hit) == HitResult::Ignored, "dead actors must ignore hits");
        check(!scaled.criticalReady() && scaled.hitStopRemaining() == 0.f,
            "death must clear hit-stop and critical vulnerability");
        scaled.setDead(false);
        check(scaled.action() == Action::Idle, "resurrected actor should reset controller");
        const auto sequence = scaled.sequence();
        scaled.reset();
        check(scaled.sequence() > sequence, "reset must not reuse an earlier action generation");
    }

    void sampledRootMotion()
    {
        ActionDef definition;
        definition.mRootMotion = { { 0.f, 0.f, 0.f }, { 1.f, 2.f, 3.f }, { 2.f, 4.f, 6.f } };
        definition.mRootMotionSpeed = 2.f;
        const auto half = Controller::rootMotionAt(definition, 0.25f);
        close(half[0], 0.5f, "root-motion left interpolation");
        close(half[1], 1.f, "root-motion vertical interpolation");
        close(half[2], 1.5f, "root-motion forward interpolation");
        const auto end = Controller::rootMotionAt(definition, 100.f);
        close(end[2], 6.f, "root-motion samples clamp at the end");
        const auto before = Controller::rootMotionAt(definition, -100.f);
        close(before[2], 0.f, "root-motion samples clamp at the beginning");

        std::array<float, 3> displacement{};
        auto previous = Controller::rootMotionAt(definition, 0.f);
        for (int tick = 1; tick <= 20; ++tick)
        {
            const auto next = Controller::rootMotionAt(definition, tick * 0.05f);
            for (std::size_t axis = 0; axis < displacement.size(); ++axis)
                displacement[axis] += next[axis] - previous[axis];
            previous = next;
        }
        for (std::size_t axis = 0; axis < displacement.size(); ++axis)
            close(displacement[axis], end[axis], "root motion must not depend on subdivision");
        check(Controller::rootMotionAt({}, 1.f) == std::array<float, 3>{}, "empty root-motion samples");
    }

    void partialHitStop()
    {
        Controller paused;
        Controller reference;
        roll(paused);
        roll(reference);
        paused.applyHitStop(0.02f);
        const Update resumed = paused.update({}, 0.1f);
        const Update uninterrupted = reference.update({}, 0.08f);
        check(!resumed.mFrozen, "partial hit-stop must allow the resumed part of the frame to move");
        close(resumed.mAdvancedSeconds, 0.08f, "partial hit-stop must expose only its resumed animation time");
        close(paused.frame(), reference.frame(), "hit-stop must consume exactly its duration");
        close(resumed.mDisplacementY, uninterrupted.mDisplacementY,
            "partial hit-stop must preserve the resumed root motion");

        Controller jumping;
        Input input;
        input.mJump = { true, true, false };
        const Update started = jumping.update(input, TickSeconds);
        check(started.mJumpStarted, "accepted jump must emit its physical launch event");
        input.clearEdges();
        check(!jumping.update(input, TickSeconds).mJumpStarted, "jump must emit only one launch event");
    }

    void menuInputOwnership()
    {
        Controller queued;
        Incoming hit;
        hit.mStanceDamage = 10.f;
        hit.mLevel = HurtLevel::Small;
        queued.receiveHit(hit);
        run(queued, {}, 80);
        queued.setVitals(65.f, 100.f);
        queued.update(downLight(), TickSeconds);
        toFrame(queued, queued.definition().mInputFrom);
        queued.update(downLight(), TickSeconds);
        const float stamina = queued.stamina();
        const float stance = queued.stance();
        queued.cancelInputActions();
        check(queued.action() == Action::Idle && !queued.activeHit(), "menu must cancel buffered/live attacks");
        close(queued.stamina(), stamina, "menu must not refund attack stamina");
        close(queued.stance(), stance, "menu must not restore lost stance");
        run(queued, {}, 160);
        check(queued.action() == Action::Idle, "buffered combo must not resume after menu closes");

        Controller charging;
        Input heavy;
        heavy.mHeavy = { true, true, false };
        heavy.mTwoHanded = heavy.mTwoHandedLeft = true;
        charging.update(heavy, TickSeconds);
        heavy.clearEdges();
        toFrame(charging, charging.definition().mChargeFrom, heavy);
        const float chargeStamina = charging.stamina();
        charging.cancelInputActions();
        close(charging.stamina(), chargeStamina, "menu must preserve charging stamina");
        heavy.mHeavy = { false, false, true };
        charging.update(heavy, TickSeconds);
        run(charging, {}, 180);
        check(charging.action() == Action::Idle && !charging.activeHit(),
            "cleared held heavy must not release a swing after menu closes");

        Controller crossed;
        Config fast;
        fast.mSpeedMultiplier = 5.f;
        crossed.configure(fast);
        crossed.update(downLight(), TickSeconds);
        crossed.update({}, 1.f);
        check(crossed.action() == Action::Idle && crossed.activeHit(), "menu crossed-window fixture");
        crossed.cancelInputActions();
        check(!crossed.activeHit(), "menu must discard crossed windows after their action already ended");

        Controller vulnerable;
        hit.mStanceDamage = 60.f;
        vulnerable.receiveHit(hit);
        const auto reaction = vulnerable.action();
        const auto sequence = vulnerable.sequence();
        vulnerable.cancelInputActions();
        check(vulnerable.action() == reaction && vulnerable.sequence() == sequence && vulnerable.criticalReady(),
            "menu must preserve defensive reaction and critical vulnerability");
        vulnerable.cancelInputActions();
        check(vulnerable.criticalReady(), "repeated menu input cleanup must not consume vulnerability");

        Controller guard;
        Input heldGuard;
        heldGuard.mGuard.mHeld = true;
        run(guard, heldGuard, 8);
        check(guard.guardUp(), "menu guard fixture");
        guard.cancelInputActions();
        check(!guard.guardUp(), "menu must release held guard");
    }

    void postHitProtection()
    {
        Config playerConfig;
        playerConfig.mPostHitProtectionSeconds = 6.f / AnimationFps;
        Incoming hit;
        hit.mDamage = 15.f;
        hit.mStanceDamage = 10.f;
        hit.mStaminaDamage = 5.f;

        Controller player;
        player.configure(playerConfig);
        check(player.receiveHit(hit) == HitResult::Hit, "first player hit must land");
        close(player.postHitProtectionRemaining(), 0.2f, "player protection must last six animation frames");
        const auto reaction = player.sequence();
        const float stance = player.stance();
        check(player.receiveHit(hit) == HitResult::Ignored, "simultaneous crowd contact must be ignored");
        check(player.sequence() == reaction, "ignored crowd contact must not restart the hurt animation");
        close(player.stance(), stance, "ignored crowd contact must not take stance");
        hit.mCritical = true;
        check(player.receiveHit(hit) == HitResult::Ignored, "physical critical must respect post-hit protection");
        hit.mCritical = false;

        player.applyHitStop(0.5f);
        check(player.update({}, 0.1f).mFrozen, "hit-stop fixture must still freeze the reaction");
        close(player.postHitProtectionRemaining(), 0.1f, "hit-stop must not freeze the protection clock");
        check(player.receiveHit(hit) == HitResult::Ignored, "contact inside the remaining window must be ignored");
        close(player.postHitProtectionRemaining(), 0.1f, "ignored contact must not extend protection");
        player.update({}, 0.1f);
        check(player.postHitProtectionRemaining() == 0.f, "protection must expire while hit-stop continues");
        check(player.receiveHit(hit) == HitResult::Hit, "next physical contact must land after 0.20 seconds");

        Controller npc;
        check(npc.receiveHit(hit) == HitResult::Hit && npc.receiveHit(hit) == HitResult::Hit,
            "default NPC contacts must retain their existing behavior");
        check(npc.postHitProtectionRemaining() == 0.f, "default config must not grant NPC protection");

        Controller guarded;
        guarded.configure(playerConfig);
        Input guard;
        guard.mGuard.mHeld = true;
        run(guarded, guard, 8);
        check(guarded.receiveHit(hit) == HitResult::Blocked, "blocked-hit protection fixture");
        check(guarded.postHitProtectionRemaining() == 0.f, "a successful block must not grant protection");
        Controller broken;
        broken.configure(playerConfig);
        run(broken, guard, 8);
        broken.setVitals(1.f, 100.f);
        check(broken.receiveHit(hit) == HitResult::GuardBroken, "guard-break protection fixture");
        check(broken.postHitProtectionRemaining() == 0.f, "zero-damage guard break must not grant protection");

        Controller dodging;
        dodging.configure(playerConfig);
        roll(dodging);
        check(dodging.receiveHit(hit) == HitResult::Dodged, "dodge protection fixture");
        check(dodging.postHitProtectionRemaining() == 0.f, "dodged contact must not grant post-hit protection");
        Controller harmless;
        harmless.configure(playerConfig);
        hit.mDamage = 0.f;
        harmless.receiveHit(hit);
        check(harmless.postHitProtectionRemaining() == 0.f, "zero damage must not grant protection");
        harmless.protectAfterHit(std::numeric_limits<float>::infinity());
        harmless.protectAfterHit(-1.f);
        check(harmless.postHitProtectionRemaining() == 0.f, "invalid damage must not grant protection");

        Controller lifecycle;
        lifecycle.configure(playerConfig);
        // Inactive world adapters may accept a physical hit without forcing an
        // action reaction, and advance the same timer without update().
        lifecycle.protectAfterHit(15.f);
        check(lifecycle.action() == Action::Idle, "inactive protection must not invent an action reaction");
        lifecycle.cancelInputActions();
        lifecycle.update({}, 0.f);
        close(lifecycle.postHitProtectionRemaining(), 0.2f, "paused menus must preserve remaining protection");
        lifecycle.advanceHitProtection(0.075f);
        lifecycle.reset(true);
        lifecycle.protectAfterHit(15.f);
        close(lifecycle.postHitProtectionRemaining(), 0.125f,
            "runtime reset and repeated contacts must preserve, not refresh, the remainder");
        lifecycle.advanceHitProtection(-1.f);
        lifecycle.advanceHitProtection(std::numeric_limits<float>::quiet_NaN());
        close(lifecycle.postHitProtectionRemaining(), 0.125f, "invalid time must not extend protection");
        lifecycle.advanceHitProtection(0.125f);
        check(lifecycle.postHitProtectionRemaining() == 0.f, "inactive protection must expire");
        lifecycle.protectAfterHit(15.f);
        lifecycle.reset();
        check(lifecycle.postHitProtectionRemaining() == 0.f, "load/full reset must clear transient protection");
        lifecycle.protectAfterHit(15.f);
        lifecycle.setDead(true);
        lifecycle.protectAfterHit(15.f);
        check(lifecycle.postHitProtectionRemaining() == 0.f, "death must clear protection and prevent new grants");
        lifecycle.setDead(false);
        check(lifecycle.postHitProtectionRemaining() == 0.f, "resurrection must not restore old protection");
    }

    void sprintJumpLaunch()
    {
        Controller runner;
        Input input;
        input.mSprint = true;
        input.mMoveY = 1.f;
        runner.update(input, TickSeconds);
        check(runner.sprinting(), "sprint-jump fixture must first establish sprint momentum");
        input.mJump.mPressed = true;
        const Update jump = runner.update(input, TickSeconds);
        check(jump.mJumpStarted && !runner.sprinting(), "jump must launch and stop grounded sprint action");
        close(jump.mJumpMovementMultiplier, 1.504f, "jump launch must retain the preceding sprint speed");
        input.clearEdges();
        const Update air = runner.update(input, TickSeconds);
        check(!air.mJumpStarted, "jump momentum must be handed to physics only once");
        close(air.mJumpMovementMultiplier, 1.f, "non-launch updates must not retain a stale sprint multiplier");

        Controller standing;
        input.mSprint = false;
        input.mMoveY = 0.f;
        input.mJump.mPressed = true;
        const Update stationary = standing.update(input, TickSeconds);
        check(stationary.mJumpStarted, "standing jump must remain available");
        close(stationary.mJumpMovementMultiplier, 1.f, "standing jump must not acquire sprint speed");
    }

    void jumpLanding()
    {
        Controller runner;
        Config config;
        config.mStaminaRegen = 0.f;
        runner.configure(config);
        Input input;
        input.mSprint = true;
        input.mMoveY = 1.f;
        input.mInCombat = false;
        runner.update(input, TickSeconds);
        input.mJump.mPressed = true;
        const auto launch = runner.update(input, TickSeconds);
        check(launch.mJumpStarted, "landing fixture failed to launch");
        const float launchStamina = runner.stamina();
        input.clearEdges();
        for (int tick = 0; tick < 3; ++tick)
        {
            const auto pending = runner.update(input, TickSeconds);
            check(pending.mAction == Action::Jump && pending.mMovementLocked && !pending.mJumpStarted,
                "grounded render updates canceled or repeated a pending physics launch");
        }
        input.mGrounded = false;
        run(runner, input, 6);
        input.mGrounded = true;
        const auto landed = runner.update(input, TickSeconds);
        check(landed.mAction == Action::Idle && !landed.mMovementLocked && runner.sprinting(),
            "short jump landing did not resume held sprint immediately");
        check(!landed.mJumpStarted, "landing launched another jump without a button press");
        close(landed.mMovementMultiplier, 1.504f, "short landing lost held sprint speed");
        close(runner.stamina(), launchStamina, "landing charged jump stamina again");

        // A newly accepted jump must observe its own flight. The previous
        // jump's airborne state cannot cancel its pending launch.
        input.mJump.mPressed = true;
        runner.update(input, TickSeconds);
        input.clearEdges();
        const auto secondPending = runner.update(input, TickSeconds);
        check(secondPending.mAction == Action::Jump && secondPending.mMovementLocked,
            "new jump reused the preceding flight's landing state");
        input.mGrounded = false;
        runner.update(input, TickSeconds);
        runner.applyHitStop(3.f * TickSeconds);
        input.mGrounded = true;
        const auto frozenLanding = runner.update(input, TickSeconds);
        check(frozenLanding.mFrozen && frozenLanding.mAction == Action::Jump,
            "landing advanced the jump action during hit-stop");
        const auto thawedLanding = runner.update(input, 4.f * TickSeconds);
        check(!thawedLanding.mFrozen && thawedLanding.mAction == Action::Idle
                && !thawedLanding.mMovementLocked && runner.sprinting(),
            "landing observed during hit-stop was lost after thaw");

        // An airborne attack has its own landing recovery, which cannot be
        // shortened by the plain-jump transition.
        Controller attacker;
        input = {};
        input.mGrounded = false;
        input.mHeavy.mPressed = true;
        attacker.update(input, TickSeconds);
        check(attacker.action() == Action::JumpHeavy, "jump-attack landing fixture selected a ground attack");
        input.clearEdges();
        input.mGrounded = true;
        const auto attackLanding = attacker.update(input, TickSeconds);
        check(attackLanding.mAction == Action::JumpHeavy && attackLanding.mMovementLocked,
            "plain-jump landing policy canceled a jump attack's recovery");
    }
}

int main()
{
    try
    {
        dodgeAndStamina();
        sprinting();
        sprintTerrainContact();
        bufferedDodgeDirection();
        chainsAndHitWindows();
        heavyAttacks();
        guardAndCounters();
        stanceJumpAndHitStop();
        sampledRootMotion();
        partialHitStop();
        menuInputOwnership();
        postHitProtection();
        sprintJumpLaunch();
        jumpLanding();
        std::cout << "EldenCombat behavior tests passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "EldenCombat behavior test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
