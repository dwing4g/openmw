#ifndef OPENMW_MECHANICS_OBSTACLEDIRECTION_H
#define OPENMW_MECHANICS_OBSTACLEDIRECTION_H

#include "../mwworld/movementdirection.hpp"

#include <array>
#include <cstddef>

namespace MWMechanics
{
    // Retain the last attempted route, so repeated collisions try the other
    // supported directions instead of pressing into the same obstacle forever.
    class ObstacleDirection
    {
    public:
        struct Direction
        {
            float mX;
            float mY;
            MWWorld::MovementDirectionFlag mRequiredAnimation;
        };

        bool advance(MWWorld::MovementDirectionFlags supported)
        {
            for (std::size_t attempt = 0; attempt < sDirections.size(); ++attempt)
            {
                mIndex = (mIndex + 1) % sDirections.size();
                if ((sDirections[mIndex].mRequiredAnimation & supported) != 0)
                    return true;
            }
            return false;
        }

        const Direction& get() const { return sDirections[mIndex]; }

    private:
        static constexpr std::array<Direction, 7> sDirections{ {
            { 1.f, 1.f, MWWorld::MovementDirectionFlag_Forward },
            { 1.f, 0.f, MWWorld::MovementDirectionFlag_Right },
            { 1.f, -1.f, MWWorld::MovementDirectionFlag_Back },
            { 0.f, -1.f, MWWorld::MovementDirectionFlag_Back },
            { -1.f, -1.f, MWWorld::MovementDirectionFlag_Back },
            { -1.f, 0.f, MWWorld::MovementDirectionFlag_Left },
            { -1.f, 1.f, MWWorld::MovementDirectionFlag_Forward },
        } };
        std::size_t mIndex = sDirections.size() - 1;
    };
}

#endif
