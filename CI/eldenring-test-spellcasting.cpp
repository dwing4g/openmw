// Check the actual pose clock policy without graphics or proprietary assets.
#include <components/eldencombat/spellcasting.hpp>
#include <components/eldencombat/animations.hpp>

#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
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

    void close(std::optional<float> value, float expected, const char* message)
    {
        check(value && std::abs(*value - expected) < 1e-3f, message);
    }

    void releaseAlignment()
    {
        // Include native and modded spell tracks at different absolute offsets,
        // especially releases well before/after the source clip's midpoint.
        for (float start : { 0.f, 2.5f, 20.f })
        {
            for (float windup : { .1f, .75f, 2.f })
            {
                for (float recovery : { .1f, .5f, 3.f })
                {
                    const float release = start + windup;
                    const float stop = release + recovery;
                    constexpr float sourceRelease = 24.f;
                    constexpr float sourceEnd = 70.f;
                    const auto sample = [&](float time) {
                        return EldenCombat::spellAnimationFrame(time, start, release, stop, sourceRelease, sourceEnd);
                    };
                    close(sample(start), 0.f, "native start does not sample source start");
                    close(sample(release), sourceRelease, "native spell release misses imported release gesture");
                    close(sample(stop), sourceEnd, "native recovery end misses source end");
                    close(sample(start - 10.f), 0.f, "pre-start sample does not clamp");
                    close(sample(stop + 10.f), sourceEnd, "long update does not clamp at recovery end");
                    close(sample((start + release) / 2.f), sourceRelease / 2.f, "wind-up phase is not linear");
                    close(sample((release + stop) / 2.f), (sourceRelease + sourceEnd) / 2.f,
                        "recovery phase is not linear");

                    // The renderer predicts currentTime + duration * speedMult
                    // before native runAnimation advances the same amount. A
                    // release crossed within a frame must already be visible.
                    for (float fps : { 15.f, 30.f, 60.f, 144.f })
                    {
                        for (float speed : { .5f, 1.f, 2.f })
                        {
                            float previous = 0.f;
                            const float step = speed / fps;
                            for (float current = start; current < stop; current += step)
                            {
                                const float next = current + step;
                                const auto frame = sample(next);
                                check(frame && *frame >= previous && *frame <= sourceEnd,
                                    "spell pose reverses or overshoots during native playback");
                                check(next < release ? *frame < sourceRelease : *frame >= sourceRelease,
                                    "rendered gesture and native release lie on different sides of event");
                                previous = *frame;
                            }
                            close(sample(release), sourceRelease, "paused spell moved off release frame");
                        }
                    }
                }
            }
        }
    }

    void invalidTracks()
    {
        using Args = std::array<float, 6>;
        const auto sample = [](const Args& args) {
            return EldenCombat::spellAnimationFrame(args[0], args[1], args[2], args[3], args[4], args[5]);
        };
        const Args valid{ 1.f, 0.f, 1.f, 2.f, 24.f, 70.f };
        for (std::size_t index = 0; index < valid.size(); ++index)
        {
            for (float invalid : { std::numeric_limits<float>::infinity(),
                     -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
            {
                Args args = valid;
                args[index] = invalid;
                check(!sample(args), "non-finite timing did not request native fallback");
            }
        }
        for (const Args& args : {
                 Args{ 1.f, -1.f, 1.f, 2.f, 24.f, 70.f }, // Missing start key.
                 Args{ 1.f, 0.f, -1.f, 2.f, 24.f, 70.f }, // Missing release key.
                 Args{ 1.f, 0.f, 1.f, -1.f, 24.f, 70.f }, // Missing stop key.
                 Args{ 1.f, 1.f, 1.f, 2.f, 24.f, 70.f }, // Zero wind-up.
                 Args{ 1.f, 0.f, 1.f, 1.f, 24.f, 70.f }, // Zero recovery.
                 Args{ 1.f, 2.f, 1.f, 3.f, 24.f, 70.f }, // Reversed keys.
                 Args{ 1.f, 0.f, 1.f, 2.f, 24.f, 0.f }, // Missing source clip.
                 Args{ 1.f, 0.f, 1.f, 2.f, 0.f, 70.f }, // Missing source release.
                 Args{ 1.f, 0.f, 1.f, 2.f, 70.f, 70.f }, // Truncated source.
             })
            check(!sample(args), "invalid spell track did not request native fallback");
    }

    void compositeClips(const EldenCombat::AnimationBank* bank)
    {
        check(!EldenCombat::spellAnimation(""), "empty casting range selects an incantation");
        check(!EldenCombat::spellAnimation("shoot"), "ranged attack selects an incantation");
        for (std::string_view range : { "self", "touch", "target" })
        {
            const auto animation = EldenCombat::spellAnimation(range);
            check(animation.has_value(), "native spell range has no imported animation");
            float windupLength = 40.f;
            float castLength = 80.f;
            if (bank)
            {
                const auto* windup = bank->find(animation->mWindup);
                const auto* cast = bank->find(animation->mCast);
                check(windup && cast, "animation bank is missing a required incantation phase");
                windupLength = static_cast<float>(windup->mFrames - 1);
                castLength = static_cast<float>(cast->mFrames - 1);
                check(cast->mBlendFrames == 0.f, "cast continuation adds a blend that delays the release gesture");
            }
            check(windupLength > 0.f && castLength > animation->mReleaseFrame,
                "incantation release lies outside the actual clip");
            constexpr float start = 2.f;
            constexpr float release = 2.75f;
            constexpr float stop = 3.5f;
            const auto pose = [&](float time) {
                return EldenCombat::spellAnimationPose(*animation, time, start, release, stop, windupLength, castLength);
            };
            auto sampled = pose(start);
            check(sampled && sampled->mClip == animation->mWindup && sampled->mFrame == 0.f,
                "incantation did not start with its preparation clip");
            const float seam = start + (release - start) * windupLength / (windupLength + animation->mReleaseFrame);
            sampled = pose(seam - .001f);
            check(sampled && sampled->mClip == animation->mWindup,
                "preparation clip switched before its endpoint");
            sampled = pose(seam + .001f);
            check(sampled && sampled->mClip == animation->mCast && sampled->mFrame < 1.f,
                "continuation did not resume at its own first frame");
            sampled = pose(release);
            check(sampled && sampled->mClip == animation->mCast,
                "native release is still sampling preparation clip");
            close(sampled->mFrame, animation->mReleaseFrame, "native release misses continuation's TAE event");
            sampled = pose(stop);
            check(sampled && sampled->mClip == animation->mCast,
                "native stop reverted to preparation clip");
            close(sampled->mFrame, castLength, "native stop misses continuation endpoint");
            check(!EldenCombat::spellAnimationPose(*animation, start, start, release, stop, 0.f, castLength),
                "missing preparation clip did not retain native animation");
            check(!EldenCombat::spellAnimationPose(*animation, start, start, release, stop, windupLength, 0.f),
                "missing continuation clip did not retain native animation");
            check(!EldenCombat::spellAnimationPose(*animation, start, start, release, stop,
                      windupLength, animation->mReleaseFrame),
                "truncated continuation clip did not retain native animation");
            if (bank)
            {
                std::vector<float> sample;
                for (float time = start; time <= stop + .01f; time += 1.f / 120.f)
                {
                    sampled = pose(time);
                    check(sampled.has_value(), "real incantation produced no pose");
                    const auto* clip = bank->find(sampled->mClip);
                    bank->sample(*clip, sampled->mFrame, false, sample);
                    check(sample.size() == bank->stride(), "incantation pose size differs from skeleton");
                    for (float value : sample)
                        check(std::isfinite(value), "incantation pose contains invalid joint data");
                }
                std::cout << range << ": " << animation->mWindup << " + " << animation->mCast
                          << ", release " << animation->mReleaseFrame << "/" << castLength << '\n';
            }
        }
    }
}

int main(int argc, char** argv)
{
    try
    {
        releaseAlignment();
        invalidTracks();
        compositeClips(nullptr);
        if (argc > 1)
        {
            std::ifstream stream(argv[1], std::ios::binary);
            if (!stream)
                throw std::runtime_error("cannot open requested animation bank");
            const EldenCombat::AnimationBank bank(stream);
            compositeClips(&bank);
        }
        std::cout << checks << " spellcasting checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
