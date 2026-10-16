#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_WEAPONDAMAGE_H
#define OPENMW_COMPONENTS_ELDENCOMBAT_WEAPONDAMAGE_H

#include <algorithm>
#include <array>
#include <cmath>

namespace EldenCombat
{
    using WeaponDamageRange = std::array<unsigned char, 2>;

    // Match the native "best attack" selection: highest mean damage, with
    // slash winning a three-way tie, then thrust, slash, and chop.
    inline const WeaponDamageRange& bestWeaponDamageRange(const WeaponDamageRange& chop,
        const WeaponDamageRange& slash, const WeaponDamageRange& thrust)
    {
        const int chopMean = chop[0] + chop[1];
        const int slashMean = slash[0] + slash[1];
        const int thrustMean = thrust[0] + thrust[1];
        if (slashMean == chopMean && slashMean == thrustMean)
            return slash;
        if (thrustMean >= chopMean && thrustMean >= slashMean)
            return thrust;
        return slashMean >= chopMean ? slash : chop;
    }

    inline float skillScaledDamageRange(const WeaponDamageRange& range, float modifiedSkill, float drawCharge = 1.f)
    {
        const float skill = std::isfinite(modifiedSkill) ? std::clamp(modifiedSkill, 0.f, 100.f) : 0.f;
        const float charge = std::isfinite(drawCharge) ? std::clamp(drawCharge, 0.f, 1.f) : 0.f;
        const float minimum = std::min(range[0], range[1]);
        const float maximum = std::max(range[0], range[1]);
        return minimum + (maximum - minimum) * (0.5f + skill / 200.f) * charge;
    }

    // Imported attacks are committed swings. Their per-hit motion values
    // already distinguish light, heavy, charged, and split multi-hit damage;
    // applying their synthetic mStrength as vanilla charge halves light hits
    // a second time. Skill instead interpolates the best native damage range
    // from its midpoint at skill 0 to its maximum at skill 100. Native Strength,
    // condition, defenses, and each source hit multiplier are applied later.
    inline float skillScaledWeaponDamage(const WeaponDamageRange& chop, const WeaponDamageRange& slash,
        const WeaponDamageRange& thrust, float modifiedSkill)
    {
        return skillScaledDamageRange(bestWeaponDamageRange(chop, slash, thrust), modifiedSkill);
    }
}

#endif
