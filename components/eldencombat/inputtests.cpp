#include "../../apps/openmw/mwinput/eldeninput.hpp"

#include <iostream>
#include <stdexcept>

namespace
{
    using namespace MWInput;

    void check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void shortTaps()
    {
        EldenInput input;
        for (int action : { A_Use, A_Dodge, A_Jump, A_LockOn })
        {
            input.set(action, true, true);
            input.set(action, false, true);
            const auto tap = input.consume(action);
            check(tap.mPressed && tap.mReleased && !tap.mHeld, "short tap lost an edge");
            const auto consumed = input.consume(action);
            check(!consumed.mPressed && !consumed.mReleased, "edges consumed more than once");
        }
    }

    void chargeLatch()
    {
        EldenInput input;
        input.set(A_HeavyModifier, true, true);
        input.set(A_Use, true, true);
        auto heavy = input.consume(A_HeavyAttack);
        check(heavy.mPressed && heavy.mHeld, "Shift + attack did not start charging");
        check(!input.consume(A_Use).mPressed, "heavy chord also began a light attack");
        input.set(A_HeavyModifier, false, true);
        heavy = input.consume(A_HeavyAttack);
        check(heavy.mHeld && !heavy.mReleased, "letting go of Shift interrupted charging");
        input.set(A_Use, false, true);
        heavy = input.consume(A_HeavyAttack);
        check(!heavy.mHeld && heavy.mReleased, "letting go of attack did not release charge");
        check(!input.consume(A_Use).mReleased, "heavy release leaked to light button");
    }

    void lightLatch()
    {
        EldenInput input;
        input.set(A_Use, true, true);
        check(input.consume(A_Use).mPressed, "ordinary attack did not start light attack");
        input.set(A_HeavyModifier, true, true);
        check(!input.consume(A_HeavyAttack).mPressed, "late Shift converted a light attack to heavy");
        input.set(A_Use, false, true);
        check(input.consume(A_Use).mReleased, "light attack did not release");
    }

    void gripChords()
    {
        for (int action : { A_Use, A_Block })
        {
            EldenInput input;
            input.set(A_Activate, true, true);
            check(!input.consume(A_Activate).mPressed, "grip modifier immediately interacted");
            input.set(action, true, true);
            const int grip = action == A_Use ? A_TwoHand : A_TwoHandLeft;
            check(input.consume(grip).mPressed, "grip chord selected wrong hand");
            check(!input.consume(action).mPressed, "grip chord also attacked/guarded");
            input.set(A_Activate, false, true);
            check(!input.consume(A_Activate).mPressed, "grip chord interacted on modifier release");
            check(!input.held(action), "modifier release began an unintended attack/guard");
            input.set(action, false, true);
            check(!input.consume(action).mReleased, "grip release leaked to attack/guard");
            input.set(action, true, true);
            check(input.consume(action).mPressed, "fresh press after grip was ignored");
        }
    }

    void interactions()
    {
        EldenInput input;
        input.set(A_Activate, true, false);
        check(!input.consume(A_Activate).mPressed, "interact fired before release");
        input.set(A_Activate, false, false);
        check(input.consume(A_Activate).mPressed, "ordinary interact release was lost");
        check(!input.consume(A_Activate).mPressed, "interaction fired more than once");

        input.set(A_Activate, true, true);
        input.clear(); // A menu, control switch, load or teleport cancels the chord.
        input.set(A_Activate, false, true);
        check(!input.consume(A_Activate).mPressed, "cleared input later interacted");
        input.set(A_Use, true, true);
        check(input.consume(A_Use).mPressed, "cleared modifier still captured an attack");
    }

    void spellsAndTriggers()
    {
        EldenInput input;
        input.set(A_Activate, true, false);
        input.set(A_HeavyModifier, true, false);
        input.set(A_Use, true, false);
        check(input.consume(A_Use).mPressed, "melee modifiers suppressed non-melee use");
        check(!input.consume(A_HeavyAttack).mPressed, "spell use was converted to a heavy attack");
        check(!input.consume(A_TwoHand).mPressed, "spell use changed weapon grip");
        input.set(A_Use, false, false);
        input.set(A_Activate, false, false);
        check(input.consume(A_Activate).mPressed, "non-melee use consumed ordinary interaction");

        input.set(A_HeavyAttack, true, true); // RT is a direct heavy button.
        check(input.consume(A_HeavyAttack).mHeld, "direct heavy trigger did not charge");
        input.set(A_HeavyAttack, false, true);
        check(input.consume(A_HeavyAttack).mReleased, "direct heavy trigger did not release");
    }

    void overlappingHeavySources()
    {
        EldenInput input;
        input.set(A_HeavyModifier, true, true);
        input.set(A_Use, true, true);
        input.consume(A_HeavyAttack);
        input.set(A_HeavyAttack, true, true);
        check(!input.consume(A_HeavyAttack).mPressed, "second heavy source restarted a charge");
        input.set(A_Use, false, true);
        const auto stillHeld = input.consume(A_HeavyAttack);
        check(stillHeld.mHeld && !stillHeld.mReleased, "one source released another source's charge");
        input.set(A_HeavyAttack, false, true);
        check(input.consume(A_HeavyAttack).mReleased, "final heavy source did not release");
    }

    void clearHeldAndQueuedButtons()
    {
        for (int action : { A_Use, A_HeavyAttack, A_Block, A_Dodge, A_Jump,
                 A_LockOn, A_TwoHand, A_TwoHandLeft })
        {
            EldenInput input;
            input.set(action, true, true);
            input.clear(); // Entering a menu or disabling player controls.
            const auto cleared = input.consume(action);
            check(!cleared.mHeld && !cleared.mPressed && !cleared.mReleased,
                "clearing input retained a held or queued combat button");
            input.set(action, false, true);
            check(!input.consume(action).mReleased, "old button release leaked through after clearing");
            input.set(action, true, true);
            check(input.consume(action).mPressed, "fresh combat press was ignored after clearing");
        }

        EldenInput input;
        input.set(A_HeavyModifier, true, true);
        input.set(A_Use, true, true);
        input.clear();
        input.set(A_HeavyModifier, false, true);
        input.set(A_Use, false, true);
        check(!input.consume(A_HeavyAttack).mReleased, "cleared charge released on leaving a menu");
        input.set(A_Use, true, true);
        check(input.consume(A_Use).mPressed, "fresh light attack still used a cleared heavy modifier");
    }

    void overlappingPhysicalBindings()
    {
        using Source = EldenInput::Source;
        for (int action : { A_Use, A_HeavyAttack, A_Dodge, A_Block })
        {
            EldenInput input;
            input.set(action, true, true, Source::Keyboard);
            check(input.consume(action).mPressed, "keyboard press was lost");
            input.set(action, true, true, Source::ControllerAxis);
            check(!input.consume(action).mPressed, "second physical binding retriggered action");
            input.set(action, false, true, Source::Keyboard);
            const auto held = input.consume(action);
            check(held.mHeld && !held.mReleased, "keyboard release dropped held controller action");
            input.set(action, false, true, Source::ControllerAxis);
            const auto released = input.consume(action);
            check(!released.mHeld && released.mReleased, "final physical release was lost");
        }

        EldenInput input;
        input.set(A_HeavyModifier, true, true, Source::Keyboard);
        input.set(A_HeavyModifier, true, true, Source::AlternateShift);
        input.set(A_HeavyModifier, false, true, Source::Keyboard);
        input.set(A_Use, true, true, Source::Mouse);
        check(input.consume(A_HeavyAttack).mPressed, "left Shift release cancelled held right Shift");
        check(!input.consume(A_Use).mPressed, "held right Shift produced a light attack");
        input.clear();
        input.set(A_Use, false, true, Source::Mouse);
        input.set(A_HeavyModifier, false, true, Source::AlternateShift);
        check(!input.consume(A_HeavyAttack).mReleased, "cleared physical bindings leaked a charge release");
    }

    void disconnectedController()
    {
        using Source = EldenInput::Source;
        EldenInput input;
        input.set(A_HeavyAttack, true, true, Source::ControllerAxis);
        input.set(A_Block, true, true, Source::ControllerButton);
        input.set(A_Block, true, true, Source::Mouse);
        input.set(A_Activate, true, true, Source::ControllerButton);
        input.release(Source::ControllerButton);
        input.release(Source::ControllerAxis);
        check(!input.held(A_HeavyAttack), "disconnected trigger remained held");
        check(input.held(A_Block), "disconnect cancelled a held mouse binding");
        check(!input.consume(A_Activate).mPressed, "controller disconnect triggered interaction");
        input.set(A_Block, false, true, Source::Mouse);
        check(input.consume(A_Block).mReleased, "remaining physical binding could not release");
    }

    void mouseWheelTaps()
    {
        using Source = EldenInput::Source;
        for (int action : { A_Use, A_HeavyAttack, A_Dodge, A_Activate })
        {
            EldenInput input;
            input.set(action, true, true, Source::MouseWheel);
            input.set(action, false, true, Source::MouseWheel);
            const auto tap = input.consume(action);
            check(tap.mPressed && tap.mReleased && !tap.mHeld, "mouse wheel binding lost its tap");
        }
        EldenInput input;
        input.set(A_HeavyModifier, true, true);
        input.set(A_Use, true, true, Source::MouseWheel);
        input.set(A_Use, false, true, Source::MouseWheel);
        check(input.consume(A_HeavyAttack).mPressed, "wheel attack ignored heavy modifier");
        check(!input.consume(A_Use).mPressed, "wheel heavy chord leaked a light attack");
        input.set(A_Use, true, true, Source::Mouse);
        input.consume(A_HeavyAttack);
        input.set(A_Use, true, true, Source::MouseWheel);
        input.set(A_Use, false, true, Source::MouseWheel);
        const auto charge = input.consume(A_HeavyAttack);
        check(charge.mHeld && !charge.mReleased, "wheel tap released the held mouse charge");
    }
}

int main()
{
    try
    {
        shortTaps();
        chargeLatch();
        lightLatch();
        gripChords();
        interactions();
        spellsAndTriggers();
        overlappingHeavySources();
        clearHeldAndQueuedButtons();
        overlappingPhysicalBindings();
        disconnectedController();
        mouseWheelTaps();
        std::cout << "Elden input tests passed (11 groups)\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
