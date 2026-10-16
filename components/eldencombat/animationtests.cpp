#include "animations.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace
{
    void check(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    void u32(std::string& output, std::uint32_t value)
    {
        for (unsigned int shift = 0; shift < 32; shift += 8)
            output.push_back(static_cast<char>((value >> shift) & 0xff));
    }

    void f32(std::string& output, float value)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        u32(output, bits);
    }

    void name(std::string& output, const std::string& value)
    {
        output.push_back(static_cast<char>(value.size()));
        output += value;
    }

    std::string fixture(unsigned int frames = 3, unsigned int clips = 1)
    {
        std::string output = "ERAN";
        u32(output, 2);
        u32(output, 1);
        u32(output, clips);
        name(output, "Pelvis");
        name(output, "Head");
        name(output, "Head");
        for (unsigned int clip = 0; clip < clips; ++clip)
        {
            name(output, "a000_000000");
            f32(output, 3.5f);
            u32(output, frames);
            for (unsigned int frame = 0; frame < frames; ++frame)
                for (unsigned int offset = 0; offset < 15; ++offset)
                    f32(output, static_cast<float>(frame * 30 + offset));
        }
        return output;
    }

    void rejects(const std::string& data)
    {
        std::istringstream stream(data);
        try
        {
            const EldenCombat::AnimationBank bank(stream);
        }
        catch (const std::runtime_error&)
        {
            return;
        }
        throw std::runtime_error("accepted an invalid animation bank");
    }

    void sampling()
    {
        std::istringstream stream(fixture());
        const EldenCombat::AnimationBank bank(stream);
        const auto* clip = bank.find("a000_000000");
        check(clip && clip->mFrames == 3 && clip->mBlendFrames == 3.5f, "incorrect clip metadata");
        check(bank.size() == 1 && bank.stride() == 15, "incorrect bank layout");
        check(bank.jointOffset("Head") == 3 && bank.axesOffset("Head") == 6, "incorrect joint offsets");
        check(!bank.find("missing") && !bank.jointOffset("missing") && !bank.axesOffset("Pelvis"),
            "missing lookup did not fail");
        std::vector<float> pose;
        bank.sample(*clip, 0.5f, false, pose);
        check(pose.size() == 15 && pose.front() == 15.f && pose.back() == 29.f, "interpolation differs");
        bank.sample(*clip, -1.f, false, pose);
        check(pose.front() == 0.f, "negative nonloop frame did not clamp");
        bank.sample(*clip, 90.f, false, pose);
        check(pose.front() == 60.f, "nonloop endpoint did not clamp");
        bank.sample(*clip, 2.f, true, pose);
        check(pose.front() == 0.f, "loop duplicate endpoint did not wrap");
        bank.sample(*clip, -0.5f, true, pose);
        check(pose.front() == 45.f, "negative loop frame did not wrap");
        try
        {
            bank.sample(*clip, std::numeric_limits<float>::quiet_NaN(), false, pose);
            throw std::runtime_error("accepted non-finite sample time");
        }
        catch (const std::invalid_argument&)
        {
        }
        std::istringstream singleStream(fixture(1));
        const EldenCombat::AnimationBank single(singleStream);
        single.sample(*single.find("a000_000000"), 500.f, true, pose);
        check(pose.front() == 0.f, "single-frame loop did not hold its pose");
    }

    void malformed()
    {
        const std::string good = fixture();
        // Every cut is checked, including cuts in counts, names and sample floats.
        for (std::size_t length = 0; length < good.size(); ++length)
            rejects(good.substr(0, length));
        rejects("FAIL" + good.substr(4));
        rejects(good + 'x');
        rejects(fixture(0));
        rejects(fixture(3, 2)); // Duplicate clip identifier.
        std::string bad = good;
        bad.replace(4, 4, std::string(4, '\xff'));
        rejects(bad);
        bad = good;
        bad.replace(12, 4, std::string(4, '\0'));
        rejects(bad);
        bad = good;
        bad[16] = 0; // Empty joint name.
        rejects(bad);
        bad = good;
        bad.replace(29, 4, "Nope"); // Oriented name absent from joint names.
        rejects(bad);
        bad = good;
        std::string nan;
        f32(nan, std::numeric_limits<float>::quiet_NaN());
        bad.replace(bad.size() - 4, 4, nan);
        rejects(bad);
        bad = good;
        const std::size_t clipNameEnd = 16 + 7 + 5 + 5 + 12;
        std::string negative;
        f32(negative, -1.f);
        bad.replace(clipNameEnd, 4, negative);
        rejects(bad);
        bad = good;
        bad.replace(clipNameEnd + 4, 4, std::string(4, '\xff'));
        rejects(bad);
    }

    void locomotionFacing()
    {
        struct Movement
        {
            float mX;
            float mY;
            unsigned mDirection;
        };
        // Actor-local travel while the actor/camera stay aimed at a target.
        // Include intermediate vectors produced by movement smoothing.
        constexpr std::array<Movement, 10> movements{ {
            { 0.f, 1.f, 0 }, { 0.f, -1.f, 1 }, { -1.f, 0.f, 2 }, { 1.f, 0.f, 3 },
            { 1.f, 1.f, 0 }, { -1.f, 1.f, 0 }, { 1.f, -1.f, 1 }, { -1.f, -1.f, 1 },
            { 0.4f, 0.9f, 0 }, { -0.8f, 0.2f, 2 },
        } };
        for (const auto& movement : movements)
        {
            for (float magnitude : { 0.25f, 1.f, 250.f })
            {
                const float x = movement.mX * magnitude;
                const float y = movement.mY * magnitude;
                const float length = std::hypot(x, y);
                const auto sprint = EldenCombat::locomotionAnimation(x, y, false, true, true, false);
                check(sprint.mId == 20200 && sprint.mNativeSpeed == 6.035f,
                    "sprint must use the forward sprint loop in every direction");
                // Rotate that clip's +Y forward vector about +Z. It must point
                // along travel, including sideways and away from the target.
                check(std::abs(-std::sin(sprint.mYaw) - x / length) < 1e-5f
                        && std::abs(std::cos(sprint.mYaw) - y / length) < 1e-5f,
                    "sprint pose faces the target instead of the direction of travel");
                const auto released = EldenCombat::locomotionAnimation(x, y, false, true, false, false);
                check(released.mId == 20100 + movement.mDirection && released.mYaw == 0.f,
                    "releasing sprint must restore target-facing directional running");
                const auto walk = EldenCombat::locomotionAnimation(x, y, false, false, false, false);
                check(walk.mId == 20000 + movement.mDirection && walk.mYaw == 0.f && walk.mNativeSpeed == 1.5f,
                    "walking must retain directional loops and target-facing pose");
                for (bool running : { false, true })
                {
                    const auto crouch = EldenCombat::locomotionAnimation(x, y, true, running, true, false);
                    check(crouch.mId == (running ? 320100 : 320000) + movement.mDirection && crouch.mYaw == 0.f,
                        "sprint held while crouching must not rotate directional crouch loops");

                    const auto free = EldenCombat::locomotionAnimation(x, y, false, running, false, true);
                    check(free.mId == (running ? 20100 : 20000)
                            && free.mNativeSpeed == (running ? 4.012f : 1.5f),
                        "turn-to-movement walk/run must use the forward clip and its playback speed");
                    check(std::abs(-std::sin(free.mYaw) - x / length) < 1e-5f
                            && std::abs(std::cos(free.mYaw) - y / length) < 1e-5f,
                        "turn-to-movement walk/run does not face travel");
                    check(free.mYaw == sprint.mYaw,
                        "releasing sprint with turn-to-movement must retain the same facing");
                    for (bool sprintHeld : { false, true })
                    {
                        const auto freeCrouch = EldenCombat::locomotionAnimation(x, y, true, running, sprintHeld, true);
                        check(freeCrouch.mId == (running ? 320100 : 320000)
                                && freeCrouch.mNativeSpeed == (running ? 2.982f : 1.370f)
                                && freeCrouch.mYaw == free.mYaw,
                            "turn-to-movement crouch must face travel without selecting a sprint clip");
                    }
                }
                const auto freeSprint = EldenCombat::locomotionAnimation(x, y, false, true, true, true);
                check(freeSprint.mId == sprint.mId && freeSprint.mNativeSpeed == sprint.mNativeSpeed
                        && freeSprint.mYaw == sprint.mYaw,
                    "sprint must face travel regardless of turn-to-movement setting");
            }
        }
    }
}

int main(int argc, char** argv)
{
    try
    {
        sampling();
        malformed();
        locomotionFacing();
        std::cout << "Elden animation bank, sampling and locomotion-facing tests passed\n";
        if (argc > 1)
        {
            std::ifstream stream(argv[1], std::ios::binary);
            check(static_cast<bool>(stream), "cannot open supplied animation bank");
            const EldenCombat::AnimationBank bank(stream);
            check(bank.jointNames().size() == 38 && bank.orientedNames().size() == 3,
                "supplied bank differs from the recreation skeleton");
            std::vector<float> pose;
            for (const auto* name : { "a000_000000", "a000_027000", "a000_020000", "a000_017002" })
            {
                const auto* clip = bank.find(name);
                check(clip != nullptr, "supplied bank is missing an essential animation");
                bank.sample(*clip, 0.5f, false, pose);
                for (float value : pose)
                    check(std::isfinite(value), "supplied bank produced a non-finite pose");
            }
            std::cout << "Loaded local bank: " << bank.size() << " clips, " << bank.jointNames().size()
                      << " joints, " << bank.orientedNames().size() << " oriented joints\n";
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
