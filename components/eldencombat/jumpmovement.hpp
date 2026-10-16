#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_JUMPMOVEMENT_H
#define OPENMW_COMPONENTS_ELDENCOMBAT_JUMPMOVEMENT_H

#include "combat.hpp"

#include <algorithm>
#include <array>

namespace EldenCombat
{
    // A render update can accept a jump before the fixed physics step runs.
    // Retain its launch speed while OpenMW's native jump pulse is pending.
    class JumpMovement
    {
    public:
        bool pressed(bool inputPressed, bool grounded, bool nativeJumpPending) const
        {
            // Physics can retain the accepted pulse across several render
            // updates. It is not another button press to put in the cancel buffer.
            return inputPressed || (grounded && nativeJumpPending && !mPending);
        }

        void reset(float& nativeJump)
        {
            *this = {};
            if (nativeJump > 0.f)
                nativeJump = 0.f;
        }

        void update(const Update& combat, bool enabled, bool grounded, bool nativeJumpPending)
        {
            if (!enabled || !grounded || !nativeJumpPending)
            {
                mPending = false;
                mHaveLaunch = false;
            }
            else if (combat.mJumpStarted)
            {
                mPending = true;
                mMultiplier = combat.mJumpMovementMultiplier;
                mHaveLaunch = false;
            }
        }

        bool pending() const { return mPending; }
        float multiplier(const Update& combat) const { return mPending ? mMultiplier : combat.mMovementMultiplier; }
        bool movementLocked(const Update& combat) const { return combat.mMovementLocked && !mPending; }

        std::array<float, 3> launch(float x, float y, float jumpSpeed)
        {
            // Keep native moving/standing jump height, while preserving the
            // horizontal running velocity that physics stores as inertia.
            if (!mHaveLaunch)
            {
                mLaunch = { x, y, jumpSpeed * ((x != 0.f || y != 0.f) ? .707f : 1.f) };
                mHaveLaunch = true;
            }
            return mLaunch;
        }

    private:
        bool mPending = false;
        float mMultiplier = 1.f;
        bool mHaveLaunch = false;
        std::array<float, 3> mLaunch{};
    };

    template <class Velocity>
    void limitMovement(Velocity& velocity, const Update& combat, bool launchedJump, bool frozen,
        bool animationControlled, float duration)
    {
        // A committed jump is an impulse. Scaling or zeroing it here would
        // replace its momentum for the entire flight, not just this frame.
        if (launchedJump)
            return;
        if (combat.mMovementLocked || frozen)
            velocity[0] = velocity[1] = 0.f;
        else if (duration > 0.f && !animationControlled)
        {
            const float fraction = std::clamp(combat.mAdvancedSeconds / duration, 0.f, 1.f);
            velocity[0] *= fraction;
            velocity[1] *= fraction;
        }
    }
}

#endif
