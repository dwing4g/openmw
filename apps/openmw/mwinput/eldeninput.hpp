#ifndef OPENMW_MWINPUT_ELDENINPUT_H
#define OPENMW_MWINPUT_ELDENINPUT_H

#include <array>
#include <cstdint>

#include <components/eldencombat/combat.hpp>

#include "actions.hpp"

namespace MWInput
{
    // Route physical button transitions before the legacy bindings are updated.
    // This preserves short taps and remembers the chord that began an attack
    // until its attack button is released, even if its modifier is released first.
    class EldenInput
    {
    public:
        // OpenMW supports one active controller. Keep its buttons/axes separate
        // from keyboard/mouse bindings, including the two Shift keys.
        enum class Source
        {
            Keyboard,
            AlternateShift,
            Mouse,
            MouseWheel,
            ControllerButton,
            ControllerAxis,
        };

        EldenInput() { clear(); }

        void clear()
        {
            mButtons = {};
            mSources = {};
            mTargets.fill(A_Last);
            mActivatePending = false;
        }

        void set(int action, bool down, bool melee, Source source = Source::Keyboard)
        {
            auto& sources = mSources.at(action);
            const bool wasHeld = sources != 0;
            const auto mask = std::uint32_t{ 1 } << static_cast<unsigned int>(source);
            if (down)
                sources |= mask;
            else
                sources &= ~mask;
            // Releasing one physical binding must not release another binding
            // of the same action (e.g. G and the right trigger held together).
            if ((sources != 0) == wasHeld)
                return;

            // Interact is also the grip modifier. Perform an ordinary interaction
            // on release, unless an attack/guard chord consumed this press.
            if (action == A_Activate)
            {
                if (down)
                    mActivatePending = true;
                else if (mActivatePending)
                {
                    mButtons[A_Activate].mPressed = true;
                    mButtons[A_Activate].mReleased = true;
                    mActivatePending = false;
                }
                return;
            }

            int target = action;
            if (down)
            {
                if (melee && (action == A_Use || action == A_Block) && mSources[A_Activate])
                {
                    target = action == A_Use ? A_TwoHand : A_TwoHandLeft;
                    mActivatePending = false;
                }
                else if (melee && action == A_Use && mSources[A_HeavyModifier])
                    target = A_HeavyAttack;
                mTargets.at(action) = target;
            }
            else
            {
                target = mTargets.at(action);
                mTargets.at(action) = A_Last;
            }
            if (target == A_Last)
                return;

            bool held = false;
            for (int sourceAction = 0; sourceAction < A_Last; ++sourceAction)
                held |= mSources[sourceAction] && mTargets[sourceAction] == target;
            auto& button = mButtons.at(target);
            button.mPressed |= held && !button.mHeld;
            button.mReleased |= !held && button.mHeld;
            button.mHeld = held;
        }

        EldenCombat::Button consume(int action)
        {
            auto& button = mButtons.at(action);
            const EldenCombat::Button result = button;
            button.mPressed = false;
            button.mReleased = false;
            return result;
        }

        bool held(int action) const { return mButtons.at(action).mHeld; }
        bool sourceHeld(int action) const { return mSources.at(action) != 0; }

        void release(Source source)
        {
            // Losing a device is not an intentional interact-button release.
            if (mSources[A_Activate] == (std::uint32_t{ 1 } << static_cast<unsigned int>(source)))
                mActivatePending = false;
            for (int action = 0; action < A_Last; ++action)
                set(action, false, false, source);
        }

    private:
        std::array<EldenCombat::Button, A_Last> mButtons{};
        std::array<std::uint32_t, A_Last> mSources{};
        std::array<int, A_Last> mTargets{};
        bool mActivatePending = false;
    };
}

#endif
