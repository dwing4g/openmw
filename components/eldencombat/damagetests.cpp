#include "combat.hpp"
#include "weapondamage.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void check(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    void near(float actual, float expected, const char* message)
    {
        check(std::abs(actual - expected) < 0.0001f, message);
    }

    void weaponStatsAndSkill()
    {
        using namespace EldenCombat;
        // A thrusting weapon must not alternate into its weak slash/chop range.
        const WeaponDamageRange chop{ 1, 2 }, slash{ 1, 3 }, thrust{ 1, 20 };
        near(skillScaledWeaponDamage(chop, slash, thrust, 0), 10.5f, "untrained damage is the native midpoint");
        near(skillScaledWeaponDamage(chop, slash, thrust, 50), 15.25f, "weapon skill must increase damage");
        near(skillScaledWeaponDamage(chop, slash, thrust, 100), 20.f, "mastery reaches native maximum");
        near(skillScaledWeaponDamage(chop, slash, thrust, -50), 10.5f, "drained skill clamps at zero");
        near(skillScaledWeaponDamage(chop, slash, thrust, 150), 20.f, "fortified skill clamps at mastery");
        near(skillScaledWeaponDamage(chop, slash, thrust, std::numeric_limits<float>::quiet_NaN()),
            10.5f, "nonfinite skill cannot poison damage");
        near(skillScaledWeaponDamage({ 0, 0 }, { 0, 0 }, { 0, 0 }, 100), 0.f,
            "a weapon without physical damage stays harmless");
        near(skillScaledWeaponDamage({ 5, 5 }, { 0, 0 }, { 0, 0 }, 0), 5.f,
            "a fixed native damage range stays fixed");

        // Native best-attack selection ranks average damage, not just maximum.
        const WeaponDamageRange highMinimum{ 10, 12 }, highMaximum{ 1, 20 }, zero{ 0, 0 };
        check(&bestWeaponDamageRange(highMinimum, highMaximum, zero) == &highMinimum,
            "selection must match the native average-damage rule");
        const WeaponDamageRange a{ 1, 9 }, b{ 2, 8 }, c{ 3, 7 };
        check(&bestWeaponDamageRange(a, b, c) == &b, "native three-way ties prefer slash");
        check(&bestWeaponDamageRange(a, zero, c) == &c, "native two-way ties prefer thrust");

        // Each hand passes its own range and equipment skill. No primary-hand
        // state or global damage coefficient can leak into the offhand result.
        const float right = skillScaledWeaponDamage({ 1, 40 }, zero, zero, 100);
        const float left = skillScaledWeaponDamage({ 1, 8 }, zero, zero, 20);
        near(right, 40.f, "right-hand stats lost");
        near(left, 5.2f, "offhand must use its own weapon and skill");
    }

    void sourceHitScaling()
    {
        using namespace EldenCombat;
        const float base = skillScaledWeaponDamage({ 1, 20 }, { 0, 0 }, { 0, 0 }, 50);
        Config config;
        config.mWeaponClass = WeaponClass::StraightSword;
        const auto light = Controller::actionDefinition(Action::Light1, config);
        const auto heavy = Controller::actionDefinition(Action::Heavy1, config);
        const auto charged = Controller::actionDefinition(Action::Heavy1Charge, config);
        const float lightDamage = base * light.mHits[0].mDamageMultiplier;
        const float heavyDamage = base * heavy.mHits[0].mDamageMultiplier;
        const float chargedDamage = base * charged.mHits[0].mDamageMultiplier;
        check(lightDamage < heavyDamage && heavyDamage < chargedDamage,
            "source hit multipliers must retain light/heavy/full-charge progression");
        near(lightDamage, 15.25f, "light attack was discounted by synthetic vanilla charge");
        near(heavyDamage, 19.0625f, "heavy hit multiplier applied incorrectly");
        near(chargedDamage, 24.4f, "charged hit multiplier applied incorrectly");

        // Split hits must retain their individual source fractions. Raising a
        // small hit to full weapon damage would overbuff multi-hit armaments.
        config.mWeaponClass = WeaponClass::Twinblade;
#ifdef OPENMW_ELDENCOMBAT_IMPORTED_TIMINGS
        const auto split = Controller::actionDefinition(Action::PairedLight1, config);
#else
        const auto split = Controller::actionDefinition(Action::Light1, config);
#endif
        check(split.mHitCount > 1, "multi-hit fixture has no split hits");
        float total = 0.f;
        for (std::size_t i = 0; i != split.mHitCount; ++i)
        {
            check(split.mHits[i].mDamageMultiplier < 1.f, "split-hit source weighting was lost");
            total += base * split.mHits[i].mDamageMultiplier;
        }
        check(total > 0.f && total < lightDamage * split.mHitCount,
            "multi-hit weighting should preserve the total instead of awarding full damage per contact");
    }

    void rangedDrawAndAmmunition()
    {
        using namespace EldenCombat;
        const WeaponDamageRange bow{ 1, 20 }, arrow{ 1, 4 }, fixedArrow{ 2, 2 };
        near(skillScaledDamageRange(bow, 50, 1.f), 15.25f, "full draw must retain weapon skill");
        near(skillScaledDamageRange(bow, 50, 0.5f), 8.125f, "partial draw must remain weaker independently");
        near(skillScaledDamageRange(bow, 50, 0.f), 1.f, "uncharged range must retain the native minimum");
        near(skillScaledDamageRange(bow, 100, 1.f), 20.f, "mastery full draw reaches native maximum");
        near(skillScaledDamageRange(bow, 50, 1.f) + skillScaledDamageRange(arrow, 50, 1.f),
            18.5f, "arrow stats must contribute independently of the bow range");
        near(skillScaledDamageRange(fixedArrow, 0, 0.f), 2.f, "fixed-damage ammunition must stay fixed");
        check(skillScaledDamageRange(bow, 100, 0.5f) > skillScaledDamageRange(bow, 0, 0.5f),
            "weapon skill must still matter on partial ranged attacks");
    }
}

int main()
{
    try
    {
        weaponStatsAndSkill();
        sourceHitScaling();
        rangedDrawAndAmmunition();
        std::cout << "Elden weapon damage tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
