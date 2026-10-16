#include <apps/openmw/mwmechanics/obstacledirection.hpp>

#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
    void check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void repeatedObstruction()
    {
        MWMechanics::ObstacleDirection direction;
        constexpr std::array<std::array<float, 2>, 7> expected{ {
            { 1.f, 1.f }, { 1.f, 0.f }, { 1.f, -1.f }, { 0.f, -1.f },
            { -1.f, -1.f }, { -1.f, 0.f }, { -1.f, 1.f },
        } };
        for (int retry = 0; retry < 21; ++retry)
        {
            check(direction.advance(15), "mobile actor has no evasive route");
            const auto& actual = direction.get();
            const auto& wanted = expected[retry % expected.size()];
            check(actual.mX == wanted[0] && actual.mY == wanted[1],
                "repeated obstruction did not advance to the next route");
        }
    }

    void forwardOnlyCreatures()
    {
        MWMechanics::ObstacleDirection direction;
        // Rats and other creatures need not have sideways/backward clips. They
        // must still try both forward diagonals to escape terrain on either side.
        for (int retry = 0; retry < 20; ++retry)
        {
            check(direction.advance(MWWorld::MovementDirectionFlag_Forward), "forward-only creature cannot evade");
            check(direction.get().mX == (retry % 2 == 0 ? 1.f : -1.f) && direction.get().mY == 1.f,
                "forward-only creature keeps pushing into the same side of an obstacle");
        }
    }

    void supportedDirections()
    {
        for (MWWorld::MovementDirectionFlags mask = 0; mask < 16; ++mask)
        {
            MWMechanics::ObstacleDirection direction;
            for (int retry = 0; retry < 21; ++retry)
            {
                check(direction.advance(mask) == (mask != 0), "no supported movement did not stop evasion");
                if (mask != 0)
                    check((direction.get().mRequiredAnimation & mask) != 0,
                        "obstacle evasion selected an unsupported creature animation");
            }
        }
        MWMechanics::ObstacleDirection direction;
        check(direction.advance(MWWorld::MovementDirectionFlag_Right), "right-only route unavailable");
        check(!direction.advance(0), "immobile actor has an evasive route");
        check(direction.advance(MWWorld::MovementDirectionFlag_Left), "changed animation capabilities ignored");
        check(direction.get().mX == -1.f && direction.get().mY == 0.f, "updated movement mask was ignored");
    }
}

int main()
{
    try
    {
        repeatedObstruction();
        forwardOnlyCreatures();
        supportedDirections();
        std::cout << "Navigation regression tests passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
