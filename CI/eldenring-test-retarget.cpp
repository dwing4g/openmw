// Checks production heading blends, limb planes, and feet without a graphics context.
// Optionally pass player_anims.bin to verify locomotion and combat regression samples.
#include <apps/openmw/mwrender/eldenanatomy.hpp>
#include <components/eldencombat/animations.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
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

    constexpr float pi = 3.14159265358979323846f;

    void sameHeading(float actual, float expected, const char* message)
    {
        check(std::isfinite(actual) && std::abs(std::remainder(actual - expected, 2.f * pi)) < 1e-5f,
            message);
        check(actual > -pi && actual <= pi, "shown heading was not normalized");
    }

    void headingReversals()
    {
        for (float direction : { -1.f, 1.f })
        {
            MWRender::EldenPose::HeadingBlend heading;
            const float from = direction * 0.6f;
            heading.start(from);
            heading.update(from, 1.f);
            const float turn = direction * (pi - 0.2f);
            const float target = std::remainder(from + turn, 2.f * pi);
            heading.start(target);
            sameHeading(heading.from(), from, "turn did not capture the displayed heading");
            for (int step = 0; step <= 100; ++step)
            {
                const float blend = step / 100.f;
                sameHeading(heading.update(target, blend), from + turn * blend,
                    "heading reversed its shortest turn during the fade");
            }
        }

        for (float target : { pi, -pi, std::atan2(0.f, -1.f), std::atan2(-0.f, -1.f) })
        {
            MWRender::EldenPose::HeadingBlend heading;
            heading.start(target);
            sameHeading(heading.update(target, 0.5f), pi * 0.5f,
                "exact reversal selected an inconsistent turn direction");
            sameHeading(heading.update(target, 1.f), pi,
                "exact reversal failed to reach its target");
        }
        for (float zero : { 0.f, -0.f })
        {
            MWRender::EldenPose::HeadingBlend heading;
            heading.start(zero);
            check(heading.update(zero, 0.5f) == 0.f && !std::signbit(heading.shown()),
                "signed zero changed the canonical forward heading");
        }
    }

    void headingBoundaryAndInterruption()
    {
        for (float direction : { -1.f, 1.f })
        {
            MWRender::EldenPose::HeadingBlend heading;
            heading.start(direction * (pi - 0.1f));
            for (int step = 0; step <= 100; ++step)
            {
                const float blend = step / 100.f;
                // The movement target passes through the atan2 discontinuity
                // while the character is still turning toward it.
                const float target = direction * (pi - 0.1f + 0.2f * blend);
                sameHeading(heading.update(std::remainder(target, 2.f * pi), blend), target * blend,
                    "crossing +/-pi reversed an ongoing heading blend");
            }
        }

        MWRender::EldenPose::HeadingBlend heading;
        heading.start(2.4f);
        heading.update(2.4f, 0.4f);
        const float interrupted = heading.shown();
        heading.start(-2.4f);
        sameHeading(heading.from(), interrupted, "interrupted turn lost the displayed heading");
        sameHeading(heading.update(-2.4f, 0.f), interrupted, "interrupted turn snapped at blend zero");
        sameHeading(heading.update(-2.4f, 0.5f), interrupted + (-2.4f + 2.f * pi - interrupted) * 0.5f,
            "interrupted turn failed to take the shortest path from its visible pose");
        heading.reset();
        check(heading.shown() == 0.f && heading.from() == 0.f, "heading reset retained an old turn");
        heading.start(-1.f);
        sameHeading(heading.update(-1.f, 0.5f), -0.5f, "native pose entry did not begin at zero heading");
    }

    std::array<float, 4> headingAtRate(int fps)
    {
        MWRender::EldenPose::HeadingBlend heading;
        heading.start(3.f);
        std::array<float, 4> checkpoints;
        for (int frame = 1; frame <= fps * 2; ++frame)
        {
            const float time = static_cast<float>(frame) / fps;
            if (frame <= fps)
                heading.update(std::remainder(3.f + 0.4f * time, 2.f * pi), time / 1.5f);
            else
                heading.update(-2.1f + 0.1f * (time - 1.f), time - 1.f);
            if (frame % (fps / 2) == 0)
                checkpoints[frame / (fps / 2) - 1] = heading.shown();
            if (frame == fps)
                heading.start(-2.1f);
        }
        return checkpoints;
    }

    void headingFrameRates()
    {
        const auto baseline = headingAtRate(30);
        for (int fps : { 60, 144 })
        {
            const auto other = headingAtRate(fps);
            for (std::size_t index = 0; index < baseline.size(); ++index)
                sameHeading(other[index], baseline[index], "heading blend depends on frame rate");
        }
    }

    void handReach()
    {
        const auto near = [](const osg::Vec3f& a, const osg::Vec3f& b) {
            return (a - b).length() < 2e-4f;
        };
        for (float scale : { .4f, 1.f, 2.5f })
            for (float lowerLength : { 12.f, 18.f, 24.f })
            {
                const osg::Vec3f root(4, -2, 80);
                const osg::Vec3f joint = root + osg::Vec3f(0, 0, -18.f * scale);
                const osg::Vec3f end = joint + osg::Vec3f(0, .8f, .6f) * (lowerLength * scale);
                for (const osg::Vec3f offset : { osg::Vec3f(4, 12, -12), osg::Vec3f(-3, 8, -10),
                         osg::Vec3f(0, 1, 0), osg::Vec3f(0, 80, 0), osg::Vec3f(0, 0, 0) })
                {
                    const osg::Vec3f target = root + offset * scale;
                    const auto solved = MWRender::EldenPose::solveTwoBone(root, joint, end, target);
                    check(std::abs((solved.mJoint - root).length() - 18.f * scale) < 2e-4f,
                        "grip correction stretched the native upper arm");
                    check(std::abs((solved.mEnd - solved.mJoint).length() - lowerLength * scale) < 2e-4f,
                        "grip correction stretched the native forearm");
                    const float requested = (target - root).length();
                    const float minimum = std::abs(18.f - lowerLength) * scale;
                    const float maximum = (18.f + lowerLength) * scale;
                    if (requested >= minimum && requested <= maximum)
                        check(near(solved.mEnd, target), "reachable grip did not close onto the handle");
                    else
                        check(std::abs((solved.mEnd - root).length()
                                  - std::clamp(requested, minimum, maximum)) < 2e-4f,
                            "unreachable grip did not clamp to native arm reach");

                    // A second evaluation of a held pose must not accumulate a
                    // twist or drift, including a fully extended/folded arm.
                    const auto held = MWRender::EldenPose::solveTwoBone(root, solved.mJoint, solved.mEnd, target);
                    check(near(held.mEnd, solved.mEnd) && near(held.mJoint, solved.mJoint),
                        "holding a grip changed the solved arm pose");
                }
                const auto same = MWRender::EldenPose::solveTwoBone(root, joint, end, end);
                check(near(same.mJoint, joint) && near(same.mEnd, end),
                    "an already closed grip changed its elbow bend");
            }

        // Both target directions lie on opposite sides of the old elbow.
        // Projecting the old upper arm directly onto each target's plane
        // reverses the bend; transporting its original plane stays continuous.
        const osg::Vec3f root, joint(0, 1, 0), end(1, 0, 0);
        const auto before = MWRender::EldenPose::solveTwoBone(root, joint, end, { -.001f, 1, 0 });
        const auto after = MWRender::EldenPose::solveTwoBone(root, joint, end, { .001f, 1, 0 });
        check((before.mJoint - after.mJoint).length() < .01f,
            "grip target crossing the elbow direction flipped its bend side");
        for (const auto arm : { std::array<osg::Vec3f, 3>{ root, root, end },
                 std::array<osg::Vec3f, 3>{ root, joint, joint } })
        {
            const auto result = MWRender::EldenPose::solveTwoBone(arm[0], arm[1], arm[2], { 2, 2, 2 });
            check(result.mJoint == arm[1] && result.mEnd == arm[2],
                "missing native arm segment generated a grip correction");
        }
    }

    osg::Vec3f plane(const osg::Vec3f& upper, const osg::Vec3f& lower,
        const osg::Vec3f& primary, const osg::Vec3f& hipRight)
    {
        const auto secondary = MWRender::EldenPose::kneeSecondary(upper, lower, primary, hipRight);
        check(std::isfinite(secondary.x()) && std::isfinite(secondary.y()) && std::isfinite(secondary.z()),
            "bend reconstruction produced a non-finite orientation");
        check(secondary.length2() > 1e-10f, "bend reconstruction lost its secondary axis");
        return MWRender::EldenPose::unit(primary ^ secondary, osg::Vec3f(1, 0, 0));
    }

    void raisedKnee()
    {
        const osg::Vec3f right(1, 0, 0);
        const osg::Vec3f lower(0, 0, -1);
        for (int step = -100; step <= 100; ++step)
        {
            // A flexed knee's bend plane stays fixed as the thigh passes
            // horizontal. The old primary x -forward reference changed sign.
            const osg::Vec3f upper(0, 1, step * 0.01f);
            for (const auto primary : { upper, lower })
                check(plane(upper, lower, primary, right) * -right > 0.999f,
                    "raising a flexed knee reversed its anatomical bend plane");
        }
        for (float noise : { -0.001f, 0.f, 0.001f })
        {
            const osg::Vec3f upper(0, 0, -1);
            const osg::Vec3f nearlyStraight(noise, noise, -1);
            for (const auto primary : { upper, nearlyStraight })
                check(plane(upper, nearlyStraight, primary, right) * -right > 0.99f,
                    "nearly straight knee amplified sample noise into a twist");
        }
    }

    void swingingElbows()
    {
        for (int fps : { 30, 60, 144 })
            for (bool forearm : { false, true })
            {
                MWRender::EldenPose::LimbBendPlane arm;
                osg::Vec3f previous;
                const osg::Vec3f upper(0, 0, -1), chest(0, 1, 0);
                for (int step = 0; step <= fps * 2; ++step)
                {
                    const float angle = step * pi / fps;
                    const osg::Vec3f lower(std::cos(angle), std::sin(angle), -.4f);
                    const osg::Vec3f primary = forearm ? lower : upper;
                    const osg::Vec3f secondary
                        = arm.secondary(upper, lower, primary, primary ^ chest, 1.f / fps);
                    const osg::Vec3f normal = MWRender::EldenPose::unit(primary ^ secondary, { 1, 0, 0 });
                    if (step > 0)
                        check(previous * normal > .97f, "weapon swing flipped its elbow plane against the chest");
                    previous = normal;
                    check(arm.secondary(upper, lower, primary, primary ^ chest, 0.f) == secondary,
                        "hit-stop changed the elbow plane");
                    check(arm.secondary(upper, lower, primary, primary ^ chest, 1.f / fps) == secondary,
                        "holding the same source frame accumulated an elbow twist");
                    check(std::abs(normal * primary) < 1e-5f, "elbow plane is not perpendicular to its segment");
                }
                arm.reset();
                const osg::Vec3f lower(1, 0, -.4f), primary = forearm ? lower : upper;
                check((arm.secondary(upper, lower, primary, primary ^ chest, 0.f)
                    - MWRender::EldenPose::bendSecondary(upper, lower, primary, primary ^ chest)).length() < 1e-5f,
                    "new clip retained a previous elbow plane");
            }

        MWRender::EldenPose::LimbBendPlane arm;
        const osg::Vec3f upper(0, 0, -1), reference(1, 0, 0);
        const osg::Vec3f before = arm.secondary(upper, upper, upper, reference, 1.f / 60.f);
        for (int step = 0; step != 100; ++step)
        {
            const float noise = step % 2 == 0 ? .00001f : -.00001f;
            const auto secondary = arm.secondary(upper, { noise, noise, -1 }, upper, reference, 1.f / 60.f);
            check(secondary * before > .999f, "straight elbow amplified sample noise into a twist");
        }
    }

    void verticalToes()
    {
        const osg::Vec3f right(1, 0, 0);
        for (int fps : { 30, 60, 144 })
        {
            MWRender::EldenPose::LimbBendPlane foot;
            for (int step = 0; step <= 2 * fps; ++step)
            {
                const float pitch = -.685f + step * pi / fps;
                const osg::Vec3f toe(0, std::cos(pitch), std::sin(pitch));
                const auto secondary = foot.secondary(toe, right ^ toe, toe, right, 1.f / fps);
                const auto lateral = MWRender::EldenPose::unit(toe ^ secondary, right);
                check(lateral * right > .999f, "foot flipped its sole while the toe passed vertical");
                check(foot.secondary(toe, right ^ toe, toe, right, 0.f) == secondary,
                    "hit-stop changed the foot orientation");
            }
        }
    }

    void parallelLimbFrames()
    {
        for (const osg::Vec3f axis : { osg::Vec3f(1, 0, 0), osg::Vec3f(0, 1, 0), osg::Vec3f(0, 0, 1) })
            for (float sign : { -1.f, 1.f })
            {
                const osg::Vec3f primary = axis * sign;
                MWRender::EldenPose::LimbBendPlane limb;
                const auto secondary = limb.secondary(primary, primary, primary, axis, 1.f / 60.f);
                check(secondary.length2() > .99f && std::abs(secondary * primary) < 1e-5f,
                    "straight limb aligned with its reference lost the secondary axis");
                const auto foot = MWRender::EldenPose::footSecondary(primary, axis);
                check(foot.length2() > .99f && std::abs(foot * primary) < 1e-5f,
                    "sideways toe aligned with the hip axis lost its foot frame");
                for (float noise : { .00001f, -.00001f, 0.f })
                {
                    const osg::Vec3f next = primary + secondary * noise;
                    const auto updated = limb.secondary(next, next, next, axis, 1.f / 60.f);
                    check(updated.length2() > .99f && updated * secondary > .99f,
                        "nearly parallel limb/reference amplified noise into a twist");
                }
            }
    }

    osg::Quat rotate(float angle, osg::Vec3f axis)
    {
        axis.normalize();
        const float sine = std::sin(angle * 0.5f);
        return osg::Quat(axis.x() * sine, axis.y() * sine, axis.z() * sine, std::cos(angle * 0.5f));
    }

    void sameRotation(const osg::Quat& actual, const osg::Quat& expected, const char* message)
    {
        for (const osg::Vec3f axis : { osg::Vec3f(1, 0, 0), osg::Vec3f(0, 1, 0), osg::Vec3f(0, 0, 1) })
            check((actual * axis - expected * axis).length() < 1e-5f, message);
    }

    void neutralFeet()
    {
        // Anatomical frame: +X points toward toes, +Y up, +Z right.
        const osg::Quat forward(0.5, 0.5, 0.5, 0.5);
        const osg::Quat nativeRest = rotate(0.4f, {0, 0, 1}) * rotate(-0.2f, {0, 1, 0});
        // Both absent native toes (a synthesized horizontal direction) and a
        // replacement rig with a real downward ankle/toe line must stay flat
        // in their own mesh bind pose when the source stands neutrally.
        for (float nativePitch : { 0.f, -0.35f })
            for (float sourceHeading : { -0.7f, 0.f, 0.9f })
                for (float nativeHeading : { -0.3f, 0.f, 0.5f })
                {
                    const osg::Quat neutral = forward * rotate(-0.685f, {1, 0, 0})
                        * rotate(sourceHeading, {0, 0, 1});
                    const osg::Quat nativeFrame = forward * rotate(nativePitch, {1, 0, 0})
                        * rotate(nativeHeading, {0, 0, 1});
                    const osg::Quat correction = MWRender::EldenPose::footCorrection(
                        nativeRest, nativeFrame * osg::Vec3f(1, 0, 0), neutral);
                    const osg::Quat expectedRest
                        = nativeRest * rotate(sourceHeading - nativeHeading, {0, 0, 1});
                    sameRotation(correction * neutral, expectedRest,
                        "neutral source foot changed native sole pitch/roll or lost toe-out");
                    for (float pitch : { -0.5f, 0.3f })
                    {
                        // Heel lift/toe-off is a change relative to neutral,
                        // including when the actor faces a different heading.
                        const osg::Quat delta = rotate(-sourceHeading, {0, 0, 1})
                            * rotate(pitch, {1, 0, 0}) * rotate(sourceHeading, {0, 0, 1});
                        sameRotation(correction * (neutral * delta), expectedRest * delta,
                            "foot calibration removed animated heel lift/toe-off");
                    }
                    const osg::Quat roll = rotate(2.4f, {0.3f, 1.f, 0.2f});
                    sameRotation(correction * (neutral * roll), expectedRest * roll,
                        "foot calibration failed to follow a whole-body roll");
                }
    }

    void realLocomotion(const char* path)
    {
        std::ifstream stream(path, std::ios::binary);
        check(static_cast<bool>(stream), "cannot open supplied animation bank");
        const EldenCombat::AnimationBank bank(stream);
        constexpr std::array<unsigned, 17> movements = {
            20000, 20001, 20002, 20003, 20100, 20101, 20102, 20103, 20200,
            320000, 320001, 320002, 320003, 320100, 320101, 320102, 320103,
        };
        std::vector<float> sample;
        const auto joint = [&](std::string_view name) {
            const auto offset = bank.jointOffset(name);
            check(offset.has_value(), "locomotion bank lacks a required leg joint");
            return osg::Vec3f(-sample[*offset], sample[*offset + 2], sample[*offset + 1]);
        };
        unsigned clips = 0;
        float minimumDot = 1.f;
        for (unsigned stance = 0; stance != 1000; ++stance)
            for (unsigned movement : movements)
            {
                char name[16];
                std::snprintf(name, sizeof(name), "a%03u_%06u", stance, movement);
                const auto* clip = bank.find(name);
                if (!clip)
                    continue;
                ++clips;
                std::array<osg::Vec3f, 4> previous;
                // Include half frames and the loop seam, exercising the same
                // sample interpolation and plane reconstruction as the renderer.
                for (std::size_t step = 0; step <= clip->mFrames * 2; ++step)
                {
                    bank.sample(*clip, step * 0.5f, true, sample);
                    const auto right = MWRender::EldenPose::unit(
                        joint("R_Thigh") - joint("L_Thigh"), osg::Vec3f(1, 0, 0));
                    std::size_t index = 0;
                    for (const char side : { 'L', 'R' })
                    {
                        const std::string prefix = std::string(1, side) + '_';
                        const auto upper = joint(prefix + "Calf") - joint(prefix + "Thigh");
                        const auto lower = joint(prefix + "Foot") - joint(prefix + "Calf");
                        for (const auto primary : { upper, lower })
                        {
                            const auto normal = plane(upper, lower, primary, right);
                            if (step > 0)
                            {
                                const float delta = normal * previous[index];
                                minimumDot = std::min(minimumDot, delta);
                                if (delta < 0.7f)
                                    throw std::runtime_error(std::string(name)
                                        + " knee plane jumped at half-frame " + std::to_string(step));
                                ++checks;
                            }
                            previous[index++] = normal;
                        }
                    }
                }
            }
        check(clips >= movements.size(), "supplied bank lacks the base locomotion set");
        std::cout << "Checked " << clips << " locomotion clips; maximum knee-plane step "
                  << std::acos(std::clamp(minimumDot, -1.f, 1.f)) * 180.f / 3.14159265359f << " degrees\n";
    }

    void realCombatArms(const char* path)
    {
        std::ifstream stream(path, std::ios::binary);
        check(static_cast<bool>(stream), "cannot open supplied animation bank");
        const EldenCombat::AnimationBank bank(stream);
        std::vector<float> sample;
        const auto joint = [&](std::string_view name) {
            const auto offset = bank.jointOffset(name);
            check(offset.has_value(), "combat bank lacks a required arm joint");
            return osg::Vec3f(-sample[*offset], sample[*offset + 2], sample[*offset + 1]);
        };
        float minimumDot = 1.f;
        for (const char* name : { "a010_000000", "a025_033230", "a037_032400", "a031_030010" })
        {
            const auto* clip = bank.find(name);
            check(clip != nullptr, "supplied bank lacks a combat regression clip");
            check(clip->mFrames > 1, "combat regression clip has no animation frames");
            const bool loop = std::string_view(name) == "a010_000000";
            const std::size_t cycleSteps = (clip->mFrames - 1) * 2;
            std::array<MWRender::EldenPose::LimbBendPlane, 4> planes;
            std::array<osg::Vec3f, 4> previous;
            std::vector<std::array<osg::Vec3f, 4>> previousCycle(loop ? cycleSteps : 0);
            for (std::size_t step = 0; step <= cycleSteps * (loop ? 3 : 1); ++step)
            {
                bank.sample(*clip, step * .5f, loop, sample);
                const auto up = MWRender::EldenPose::unit(joint("Neck") - joint("Pelvis"), { 0, 0, 1 });
                const auto right = MWRender::EldenPose::unit(joint("R_UpperArm") - joint("L_UpperArm"), { 1, 0, 0 });
                const auto chest = MWRender::EldenPose::unit(up ^ right, { 0, 1, 0 });
                std::size_t index = 0;
                for (const char side : { 'L', 'R' })
                {
                    const std::string prefix = std::string(1, side) + '_';
                    const auto upper = joint(prefix + "Forearm") - joint(prefix + "UpperArm");
                    const auto lower = joint(prefix + "Hand") - joint(prefix + "Forearm");
                    for (const auto primary : { upper, lower })
                    {
                        const auto secondary
                            = planes[index].secondary(upper, lower, primary, primary ^ chest, 1.f / 60.f);
                        const auto normal = MWRender::EldenPose::unit(primary ^ secondary, { 1, 0, 0 });
                        if (step > 0)
                        {
                            const float delta = normal * previous[index];
                            minimumDot = std::min(minimumDot, delta);
                            if (delta < .7f)
                                throw std::runtime_error(std::string(name) + " elbow plane jumped at half-frame "
                                    + std::to_string(step));
                            ++checks;
                        }
                        if (loop)
                        {
                            auto& earlier = previousCycle[step % cycleSteps][index];
                            if (step >= cycleSteps)
                                check(earlier * normal > .99f, "repeated idle loops accumulated an elbow twist");
                            earlier = normal;
                        }
                        previous[index++] = normal;
                    }
                }
            }
        }
        std::cout << "Checked 4 combat clips; maximum elbow-plane step "
                  << std::acos(std::clamp(minimumDot, -1.f, 1.f)) * 180.f / pi << " degrees\n";
    }

    using FrameAxes = std::array<osg::Vec3f, 3>;

    FrameAxes frameAxes(osg::Vec3f primary, osg::Vec3f secondary)
    {
        primary = MWRender::EldenPose::unit(primary, { 0, 0, 1 });
        secondary -= primary * (secondary * primary);
        secondary = MWRender::EldenPose::unit(secondary, { 1, 0, 0 });
        return { primary, secondary, primary ^ secondary };
    }

    float frameAngle(const FrameAxes& a, const FrameAxes& b)
    {
        const float cosine = (a[0] * b[0] + a[1] * b[1] + a[2] * b[2] - 1.f) * .5f;
        return std::acos(std::clamp(cosine, -1.f, 1.f)) * 180.f / pi;
    }

    void realGreatswordLegs(const char* path)
    {
        std::ifstream stream(path, std::ios::binary);
        check(static_cast<bool>(stream), "cannot open supplied animation bank");
        const EldenCombat::AnimationBank bank(stream);
        std::vector<float> sample;
        const auto joint = [&](std::string_view name) {
            const auto offset = bank.jointOffset(name);
            check(offset.has_value(), "greatsword bank lacks a required leg joint");
            return osg::Vec3f(-sample[*offset], sample[*offset + 2], sample[*offset + 1]);
        };
        for (int fps : { 30, 60, 144 })
        {
            float maximumKnee = 0.f, maximumFoot = 0.f;
            for (const char* name : { "a012_000000", "a012_020000", "a012_020001", "a012_020002", "a012_020003",
                     "a012_020100", "a012_020101", "a012_020102", "a012_020103", "a022_032020", "a022_032030" })
            {
                const auto* clip = bank.find(name);
                check(clip && clip->mFrames > 1, "supplied bank lacks a greatsword regression clip");
                const bool loop = std::string_view(name).starts_with("a012_");
                const float length = static_cast<float>(clip->mFrames - 1);
                const std::size_t cycleSteps = static_cast<std::size_t>(std::ceil(length * fps / 30.f));
                std::array<MWRender::EldenPose::LimbBendPlane, 6> planes;
                std::array<FrameAxes, 6> previous;
                std::vector<std::array<FrameAxes, 6>> previousCycle(loop ? cycleSteps : 0);
                for (std::size_t step = 0; step <= cycleSteps * (loop ? 3 : 1); ++step)
                {
                    const float frame = std::min(static_cast<float>(step % cycleSteps) * 30.f / fps, length);
                    bank.sample(*clip, !loop && step == cycleSteps ? length : frame, loop, sample);
                    const auto right = MWRender::EldenPose::unit(joint("R_Thigh") - joint("L_Thigh"), { 1, 0, 0 });
                    std::size_t index = 0;
                    for (const char side : { 'L', 'R' })
                    {
                        const std::string prefix = std::string(1, side) + '_';
                        const auto upper = joint(prefix + "Calf") - joint(prefix + "Thigh");
                        const auto lower = joint(prefix + "Foot") - joint(prefix + "Calf");
                        const auto toe = joint(prefix + "Toe0") - joint(prefix + "Foot");
                        for (const auto primary : { upper, lower, toe })
                        {
                            const bool foot = index % 3 == 2;
                            const auto secondary = foot
                                ? planes[index].secondary(toe, right ^ toe, toe, right, 1.f / fps)
                                : planes[index].secondary(upper, lower, primary, -right, 1.f / fps);
                            const auto axes = frameAxes(primary, secondary);
                            if (step > 0)
                            {
                                const float angle = frameAngle(previous[index], axes);
                                auto& maximum = foot ? maximumFoot : maximumKnee;
                                maximum = std::max(maximum, angle);
                                if (angle > 85.f * 60.f / fps)
                                    throw std::runtime_error(std::string(name) + (foot ? " foot" : " knee")
                                        + " jumped " + std::to_string(angle) + " degrees at frame "
                                        + std::to_string(frame) + " (" + std::to_string(fps) + " fps)");
                                ++checks;
                            }
                            if (loop)
                            {
                                auto& earlier = previousCycle[step % cycleSteps][index];
                                if (step >= cycleSteps && frameAngle(earlier, axes) >= 10.f)
                                    throw std::runtime_error(std::string(name) + " repeated gait changed segment "
                                        + std::to_string(index) + " by " + std::to_string(frameAngle(earlier, axes))
                                        + " degrees at frame " + std::to_string(frame) + " ("
                                        + std::to_string(fps) + " fps)");
                                if (step >= cycleSteps)
                                    ++checks;
                                earlier = axes;
                            }
                            previous[index++] = axes;
                        }
                    }
                }
            }
            std::cout << "Greatsword at " << fps << " fps: maximum knee/foot steps "
                      << maximumKnee << '/' << maximumFoot << " degrees\n";
        }
    }
}

int main(int argc, char** argv)
{
    try
    {
        headingReversals();
        headingBoundaryAndInterruption();
        headingFrameRates();
        handReach();
        raisedKnee();
        swingingElbows();
        verticalToes();
        parallelLimbFrames();
        neutralFeet();
        if (argc > 1)
        {
            realLocomotion(argv[1]);
            realCombatArms(argv[1]);
            realGreatswordLegs(argv[1]);
        }
        std::cout << "Elden limb retarget validation passed: " << checks << " assertions\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
