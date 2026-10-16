#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_SPELLCASTING_H
#define OPENMW_COMPONENTS_ELDENCOMBAT_SPELLCASTING_H

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

namespace EldenCombat
{
    struct SpellAnimation
    {
        std::string_view mWindup;
        std::string_view mCast;
        float mReleaseFrame; // Event 64 in the cast continuation, at 30 fps.
    };

    // Each incantation has a preparation clip and a separate release/recovery
    // continuation. Match Morrowind's captured range, not the selected GUI spell.
    inline std::optional<SpellAnimation> spellAnimation(std::string_view range)
    {
        if (range == "self") // Urgent Heal.
            return SpellAnimation{ "a433_045000", "a433_045010", 30.f };
        if (range == "touch") // Catch Flame.
            return SpellAnimation{ "a506_045000", "a506_045010", 5.f };
        if (range == "target") // Flame Sling, uncharged release.
            return SpellAnimation{ "a417_045000", "a417_045011", 18.f };
        return std::nullopt;
    }

    struct SpellAnimationPose
    {
        std::string_view mClip;
        float mFrame;
    };

    // Native spellcast text keys retain ownership of casting and recovery.
    // Align the imported release gesture with that native release event even
    // when a replacement NIF changes the wind-up/recovery proportions.
    inline std::optional<float> spellAnimationFrame(float time, float start, float release, float stop,
        float releaseFrame, float endFrame)
    {
        if (!std::isfinite(time) || !std::isfinite(start) || !std::isfinite(release) || !std::isfinite(stop)
            || !std::isfinite(releaseFrame) || !std::isfinite(endFrame)
            || start < 0.f || release <= start || stop <= release || releaseFrame <= 0.f || endFrame <= releaseFrame)
            return std::nullopt;

        time = std::clamp(time, start, stop);
        if (time <= release)
            return releaseFrame * (time - start) / (release - start);
        return releaseFrame + (endFrame - releaseFrame) * (time - release) / (stop - release);
    }

    inline std::optional<SpellAnimationPose> spellAnimationPose(const SpellAnimation& animation,
        float time, float start, float release, float stop, float windupLength, float castLength)
    {
        if (!std::isfinite(windupLength) || !std::isfinite(castLength) || windupLength <= 0.f
            || !std::isfinite(animation.mReleaseFrame) || animation.mReleaseFrame <= 0.f
            || castLength <= animation.mReleaseFrame)
            return std::nullopt;

        const auto frame = spellAnimationFrame(time, start, release, stop,
            windupLength + animation.mReleaseFrame, windupLength + castLength);
        if (!frame)
            return std::nullopt;
        if (*frame < windupLength)
            return SpellAnimationPose{ animation.mWindup, *frame };
        return SpellAnimationPose{ animation.mCast, *frame - windupLength };
    }
}

#endif
