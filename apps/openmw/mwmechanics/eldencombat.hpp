#ifndef OPENMW_MWMECHANICS_ELDENCOMBAT_H
#define OPENMW_MWMECHANICS_ELDENCOMBAT_H

#include <components/eldencombat/combat.hpp>

#include "../mwworld/ptr.hpp"

namespace MWMechanics
{
    bool useEldenCombat(const MWWorld::Ptr& actor);
    bool useEldenMelee(const MWWorld::Ptr& actor);
    EldenCombat::Config eldenCombatConfig(const MWWorld::Ptr& actor);
    void storeEldenStamina(const MWWorld::Ptr& actor);
    MWWorld::Ptr eldenAttackWeapon(const MWWorld::Ptr& actor);
    void cycleEldenOffhand(const MWWorld::Ptr& actor);
    bool toggleEldenGrip(const MWWorld::Ptr& actor, bool left);
    // Called once when an accepted dodge starts, including a buffered dodge.
    void playEldenDodgeSound(const MWWorld::Ptr& actor, EldenCombat::Action action);
    void playEldenGuardCounterSound(const MWWorld::Ptr& actor);

    // True consumes a magic bolt before direct/area effects and plays the native miss cue.
    bool dodgeEldenMagicProjectile(const MWWorld::Ptr& victim);

    // Resolve defense before enchantments, damage callbacks, and reactive shields.
    // False means the blow did not connect; true may still reduce damage to zero for a block.
    bool resolveEldenHit(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, float& damage,
        bool ranged = false);
    float eldenDamageMultiplier(const MWWorld::Ptr& actor);
}

#endif
