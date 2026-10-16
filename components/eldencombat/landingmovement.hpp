#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_LANDINGMOVEMENT_H
#define OPENMW_COMPONENTS_ELDENCOMBAT_LANDINGMOVEMENT_H

#include "locomotion.hpp"

namespace EldenCombat
{
    // Landing hands the collision-resolved world-space velocity back to ground
    // controls over a short, finite interval. The retained velocity replaces
    // requested movement; adding it would give every landing a speed boost.
    class LandingMovement
    {
    public:
        void reset()
        {
            mVelocity = {};
            mRemaining = 0.f;
        }

        bool active() const { return mRemaining > 0.f; }

        void land(PlanarMovement collisionVelocity, float maximumHorizontalSpeed)
        {
            mVelocity = bounded(collisionVelocity, maximumHorizontalSpeed);
            mRemaining = nonzero(mVelocity) ? 0.20f : 0.f;
        }

        PlanarMovement apply(PlanarMovement requested, float seconds)
        {
            if (!finite(requested))
                requested = {};
            if (!active())
                return requested;
            if (!std::isfinite(seconds) || seconds <= 0.f)
                return mVelocity;
            // Absorb only floating-point residue at the endpoint, so a fixed
            // number of physics steps reaches the same stop at every rate.
            if (seconds + 1e-6f >= mRemaining)
            {
                reset();
                return requested;
            }
            const float fraction = seconds / mRemaining;
            mVelocity.mX += (requested.mX - mVelocity.mX) * fraction;
            mVelocity.mY += (requested.mY - mVelocity.mY) * fraction;
            mRemaining -= seconds;
            return mVelocity;
        }

        void collide(PlanarMovement collisionVelocity, float maximumHorizontalSpeed)
        {
            if (!active())
                return;
            // Retain wall/sliding corrections without starting another handoff
            // or turning a downward slope projection into horizontal speed.
            mVelocity = bounded(collisionVelocity, maximumHorizontalSpeed);
            if (!nonzero(mVelocity))
                reset();
        }

    private:
        static bool finite(PlanarMovement velocity)
        {
            return std::isfinite(velocity.mX) && std::isfinite(velocity.mY);
        }

        static bool nonzero(PlanarMovement velocity)
        {
            return velocity.mX != 0.f || velocity.mY != 0.f;
        }

        static PlanarMovement bounded(PlanarMovement velocity, float maximumSpeed)
        {
            if (!finite(velocity) || !std::isfinite(maximumSpeed) || maximumSpeed <= 0.f)
                return {};
            const float speed = std::hypot(velocity.mX, velocity.mY);
            if (speed > maximumSpeed)
            {
                velocity.mX *= maximumSpeed / speed;
                velocity.mY *= maximumSpeed / speed;
            }
            return velocity;
        }

        PlanarMovement mVelocity;
        float mRemaining = 0.f;
    };
}

#endif
