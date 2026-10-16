#include "aitactics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace EldenCombat;

    constexpr float AttackDelay = 1.2f;

    void check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void close(float actual, float expected, const char* message)
    {
        check(std::abs(actual - expected) < 0.001f, message);
    }

    Input press(AiAttackButton button)
    {
        Input input;
        switch (button)
        {
            case AiAttackButton::Light:
                input.mLight = { true, true, false };
                break;
            case AiAttackButton::Heavy:
                input.mHeavy = { true, true, false };
                break;
            case AiAttackButton::Left:
                input.mGuard = { true, true, false };
                break;
        }
        return input;
    }

    void advance(Controller& controller, AiTactics& tactics, const Input& input = {}, float seconds = TickSeconds)
    {
        controller.update(input, seconds);
        tactics.update(controller, seconds, AttackDelay);
    }

    void finish(Controller& controller, AiTactics& tactics)
    {
        for (int tick = 0; controller.action() != Action::Idle && tick < 10000; ++tick)
            advance(controller, tactics);
        check(controller.action() == Action::Idle, "action did not finish");
    }

    void attackPauseStartsAfterRecovery()
    {
        Controller controller;
        AiTactics tactics;
        advance(controller, tactics, press(AiAttackButton::Light));
        check(tactics.comboCount() == 1, "opener did not consume one combo attack");
        check(!tactics.attackReady(), "another opener was permitted during a swing");
        finish(controller, tactics);
        close(tactics.attackCooldown(), AttackDelay, "attack animation consumed the pause between chains");
        check(tactics.comboCount() == 0, "idle did not reset the combo count");
        advance(controller, tactics, {}, AttackDelay * 0.5f);
        check(!tactics.attackReady(), "attack pause ended early");
        close(tactics.attackCooldown(), AttackDelay * 0.5f, "idle did not advance the attack pause");
        advance(controller, tactics, {}, AttackDelay * 0.5f + TickSeconds);
        check(tactics.attackReady(), "attack pause did not finish");
        advance(controller, tactics, press(AiAttackButton::Light));
        check(tactics.comboCount() == 1, "next chain inherited the previous combo count");
    }

    void finiteCombination(const Config& config, AiAttackButton button, int maximumCombo, float seconds = TickSeconds)
    {
        Controller controller;
        controller.configure(config);
        AiTactics tactics;
        advance(controller, tactics, press(button), seconds);
        int requests = 0;
        int largestCombo = tactics.comboCount();
        for (int tick = 0; controller.action() != Action::Idle && tick < 20000; ++tick)
        {
            Input input;
            // Ordinary pursuit movement also permits recovery cancellation.
            input.mMoveY = 0.25f;
            if (tactics.canCombo(controller, button, maximumCombo))
            {
                input = press(button);
                input.mMoveY = 0.25f;
                tactics.markComboRequested(controller);
                ++requests;
                check(!tactics.canCombo(controller, button, maximumCombo), "same swing queued a second follow-up");
            }
            advance(controller, tactics, input, seconds);
            largestCombo = std::max(largestCombo, tactics.comboCount());
            check(largestCombo <= maximumCombo, "combo exceeded the configured attack limit");
        }
        check(controller.action() == Action::Idle, "combination did not finish");
        check(requests == maximumCombo - 1, "combination missed a follow-up window");
        check(largestCombo == maximumCombo, "requested follow-up did not become an attack");
        check(tactics.comboCount() == 0, "completed combination did not reset");
        close(tactics.attackCooldown(), AttackDelay, "combination did not receive its full recovery pause");
    }

    void combinations()
    {
        Config config;
        finiteCombination(config, AiAttackButton::Light, 1);
        finiteCombination(config, AiAttackButton::Light, 3);
        finiteCombination(config, AiAttackButton::Light, 6);
        finiteCombination(config, AiAttackButton::Heavy, 3);

        config.mHasShield = false;
        config.mHasLeftWeapon = true;
        config.mLeftWeaponClass = WeaponClass::StraightSword;
        finiteCombination(config, AiAttackButton::Left, 3);
        config.mPairedWeapons = true;
        finiteCombination(config, AiAttackButton::Left, 3);

        config = {};
        config.mSpeedMultiplier = 0.25f;
        finiteCombination(config, AiAttackButton::Light, 3);
        config.mSpeedMultiplier = 5.f;
        finiteCombination(config, AiAttackButton::Light, 3, 1.f / 15.f);
    }

    void slowWeaponsQueueNearTheirCancel()
    {
        Config config;
        config.mSpeedMultiplier = 0.25f;
        Controller controller;
        controller.configure(config);
        AiTactics tactics;
        advance(controller, tactics, press(AiAttackButton::Light));
        const auto definition = controller.definition();
        for (int tick = 0; controller.frame() < definition.mInputFrom && tick < 10000; ++tick)
            advance(controller, tactics);
        check(controller.frame() < definition.mCancelLight - 8.f, "test weapon lacks a long input/cancel interval");
        check(!tactics.canCombo(controller, AiAttackButton::Light, 3), "slow weapon buffered at the early input window");
        for (int tick = 0; !tactics.canCombo(controller, AiAttackButton::Light, 3) && tick < 10000; ++tick)
            advance(controller, tactics);
        check(controller.action() != Action::Idle, "slow weapon missed its combo window");
        check(controller.frame() >= definition.mInputFrom, "combo queued before inputs were accepted");
        check(controller.frame() >= definition.mCancelLight - 8.f, "combo queued too far ahead of its cancel");
        const auto sequence = controller.sequence();
        tactics.markComboRequested(controller);
        advance(controller, tactics, press(AiAttackButton::Light));
        for (int tick = 0; controller.sequence() == sequence && tick < 10000; ++tick)
            advance(controller, tactics);
        check(Controller::isAttack(controller.action()) && tactics.comboCount() == 2,
            "slow weapon's buffered follow-up failed to execute");
    }

    void interruptionKeepsThePause()
    {
        Controller controller;
        AiTactics tactics;
        advance(controller, tactics, press(AiAttackButton::Light));
        Incoming incoming;
        incoming.mDamage = 1.f;
        check(controller.receiveHit(incoming) == HitResult::Hit, "test hit did not interrupt the attack");
        tactics.update(controller, 0.f, AttackDelay);
        check(tactics.comboCount() == 0, "interruption retained the old combo");
        close(tactics.attackCooldown(), AttackDelay, "interruption omitted the attack pause");
        advance(controller, tactics, {}, 0.2f);
        check(controller.action() != Action::Idle, "test hit reaction ended unexpectedly early");
        close(tactics.attackCooldown(), AttackDelay, "hit reaction consumed the attack pause");
        finish(controller, tactics);
        check(tactics.attackCooldown() >= AttackDelay - TickSeconds - 0.001f,
            "hit reaction left no opening before the next attack");

        Input dodge;
        dodge.mMoveX = 1.f;
        dodge.mDodge = { false, true, true };
        advance(controller, tactics, dodge);
        check(controller.action() == Action::Roll, "test dodge did not roll");
        const float beforeRoll = tactics.attackCooldown();
        advance(controller, tactics, {}, 0.2f);
        close(tactics.attackCooldown(), beforeRoll, "roll consumed the pause between attack chains");
    }

    void reactionDelayAndOneAttempt()
    {
        Controller opponent;
        AiTactics tactics;
        opponent.update(press(AiAttackButton::Light), TickSeconds);
        tactics.observeThreat(opponent, TickSeconds);
        check(!tactics.consumeReaction(0.25f), "NPC reacted immediately on attack detection");
        for (int tick = 0; tick < 12; ++tick)
        {
            opponent.update({}, TickSeconds);
            tactics.observeThreat(opponent, TickSeconds);
        }
        check(!tactics.consumeReaction(0.25f), "NPC reacted before its configured delay");
        for (int tick = 0; tick < 5; ++tick)
        {
            opponent.update({}, TickSeconds);
            tactics.observeThreat(opponent, TickSeconds);
        }
        check(tactics.consumeReaction(0.25f), "NPC did not become ready after its reaction delay");
        check(!tactics.consumeReaction(0.f), "same attack rerolled its defensive decision");
        for (int tick = 0; tick < 10; ++tick)
        {
            opponent.update({}, TickSeconds);
            tactics.observeThreat(opponent, TickSeconds);
            check(!tactics.consumeReaction(0.f), "continued attack rearmed a consumed defense");
        }
        while (opponent.action() != Action::Idle)
        {
            opponent.update({}, TickSeconds);
            tactics.observeThreat(opponent, TickSeconds);
        }
        check(!tactics.consumeReaction(0.f), "idle opponent offered a defensive reaction");
        opponent.update(press(AiAttackButton::Light), TickSeconds);
        tactics.observeThreat(opponent, TickSeconds);
        check(tactics.consumeReaction(0.f), "next opponent attack did not rearm defensive choice");
    }

    void chargeReleaseIsOneAttack()
    {
        Controller controller;
        AiTactics tactics;
        const Input held = press(AiAttackButton::Heavy);
        advance(controller, tactics, held);
        tactics.observeThreat(controller, TickSeconds);
        check(controller.action() == Action::Heavy1Charge, "heavy attack did not start charging");
        check(tactics.consumeReaction(0.f), "heavy charge did not offer an initial reaction");
        Input charge;
        charge.mHeavy.mHeld = true;
        for (int tick = 0; controller.frame() < controller.definition().mChargeFrom && tick < 10000; ++tick)
        {
            advance(controller, tactics, charge);
            tactics.observeThreat(controller, TickSeconds);
        }
        advance(controller, tactics);
        tactics.observeThreat(controller, TickSeconds);
        check(controller.action() == Action::Heavy1, "heavy release did not become its ordinary swing");
        check(tactics.comboCount() == 1, "charge release counted as a second combo attack");
        check(!tactics.consumeReaction(0.f), "charge release rerolled the same defensive decision");
    }

    void fullyChargedHeavyCanContinue()
    {
        Controller controller;
        AiTactics tactics;
        advance(controller, tactics, press(AiAttackButton::Heavy));
        bool passedChargeWindow = false;
        bool queuedContinuation = false;
        int largestCombo = tactics.comboCount();
        for (int tick = 0; controller.action() != Action::Idle && tick < 10000; ++tick)
        {
            Input input;
            input.mMoveY = 0.25f;
            if (tactics.comboCount() == 1)
            {
                input.mHeavy.mHeld = controller.frame() <= controller.definition().mChargeTo;
                if (controller.frame() > controller.definition().mChargeTo)
                {
                    check(controller.action() == Action::Heavy1Charge,
                        "full charge should retain its action through the swing and recovery");
                    passedChargeWindow = true;
                }
            }
            const bool charging = (controller.action() == Action::Heavy1Charge
                                      || controller.action() == Action::Heavy2Charge)
                && controller.frame() < controller.definition().mChargeTo;
            if (!charging && tactics.canCombo(controller, AiAttackButton::Heavy, 2))
            {
                input = press(AiAttackButton::Heavy);
                tactics.markComboRequested(controller);
                queuedContinuation = true;
            }
            advance(controller, tactics, input);
            largestCombo = std::max(largestCombo, tactics.comboCount());
        }
        check(controller.action() == Action::Idle, "charged heavy combination did not finish");
        check(passedChargeWindow && queuedContinuation && largestCombo == 2,
            "fully charged heavy could not continue into its planned second attack");
        close(tactics.attackCooldown(), AttackDelay, "charged heavy chain omitted its recovery pause");
    }

    void lightCombinationHeavyFinisher()
    {
        Controller controller;
        AiTactics tactics;
        advance(controller, tactics, press(AiAttackButton::Light));
        int requests = 0;
        int largestCombo = tactics.comboCount();
        bool sawHeavy = false;
        for (int tick = 0; controller.action() != Action::Idle && tick < 10000; ++tick)
        {
            Input input;
            input.mMoveY = 0.25f;
            const auto next = tactics.comboCount() == 2 ? AiAttackButton::Heavy : AiAttackButton::Light;
            if (tactics.canCombo(controller, next, 3))
            {
                input = press(next);
                tactics.markComboRequested(controller);
                ++requests;
            }
            advance(controller, tactics, input);
            largestCombo = std::max(largestCombo, tactics.comboCount());
            sawHeavy |= controller.action() == Action::Heavy1 || controller.action() == Action::Heavy1Charge;
        }
        check(controller.action() == Action::Idle && sawHeavy, "light chain omitted its planned heavy finisher");
        check(requests == 2 && largestCombo == 3, "heavy finisher exceeded the three-attack combination limit");
        close(tactics.attackCooldown(), AttackDelay, "heavy finisher omitted its recovery pause");
    }
}

int main()
{
    try
    {
        attackPauseStartsAfterRecovery();
        combinations();
        slowWeaponsQueueNearTheirCancel();
        interruptionKeepsThePause();
        reactionDelayAndOneAttempt();
        chargeReleaseIsOneAttack();
        fullyChargedHeavyCanContinue();
        lightCombinationHeavyFinisher();
        std::cout << "Elden combat AI tests passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Elden combat AI test failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
