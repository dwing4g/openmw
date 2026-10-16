#include "eldencombat.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/misc/rng.hpp>
#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"

#include "actorutil.hpp"
#include "creaturestats.hpp"
#include "weapontype.hpp"

namespace MWMechanics
{
    void playEldenDodgeSound(const MWWorld::Ptr& actor, EldenCombat::Action action)
    {
        // These are the source TAE/Wwise dodge cues. The file overload quietly
        // skips an absent optional library and respects the SFX volume/mute.
        VFS::Path::Normalized path;
        float volumeDb = -10.f;
        float pitchCents = 0.f;
        auto& prng = MWBase::Environment::get().getWorld()->getPrng();
        if (action == EldenCombat::Action::Roll || action == EldenCombat::Action::CrouchRoll)
        {
            path = VFS::Path::Normalized("sound/eldenring/dodge/roll.ogg");
            volumeDb -= Misc::Rng::rollProbability(prng);
        }
        else if (action == EldenCombat::Action::Backstep)
        {
            const int variant = Misc::Rng::rollDice(9, prng);
            path = VFS::Path::Normalized("sound/eldenring/dodge/backstep-" + std::to_string(variant) + ".ogg");
            volumeDb = variant == 3 ? -11.f : -15.f;
            pitchCents = 230.f + 200.f * Misc::Rng::rollProbability(prng) - 100.f;
        }
        else
            return;
        MWBase::Environment::get().getSoundManager()->playSound3D(
            actor, path, std::pow(10.f, volumeDb / 20.f), std::pow(2.f, pitchCents / 1200.f));
    }

    void playEldenGuardCounterSound(const MWWorld::Ptr& actor)
    {
        // Play_c999999984 starts at frame zero of the guard-counter TAE.
        MWBase::Environment::get().getSoundManager()->playSound3D(actor,
            VFS::Path::Normalized("sound/eldenring/dodge/guard-counter.ogg"), std::pow(10.f, -2.f / 20.f), 1.f);
    }

    bool useEldenCombat(const MWWorld::Ptr& actor)
    {
        return Settings::game().mEldenRingCombat && !actor.isEmpty() && actor.getClass().isActor();
    }

    bool useEldenMelee(const MWWorld::Ptr& actor)
    {
        if (!useEldenCombat(actor) || actor.getClass().getCreatureStats(actor).getDrawState() != DrawState::Weapon)
            return false;
        if (!actor.getClass().hasInventoryStore(actor))
            return true;
        const auto& input = actor.getClass().getCreatureStats(actor).getEldenCombatInput();
        if (input.mTwoHanded && input.mTwoHandedLeft)
        {
            auto& inventory = actor.getClass().getInventoryStore(actor);
            const auto left = inventory.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
            if (left != inventory.end())
            {
                if (left->getType() == ESM::Light::sRecordId || left->getType() == ESM::Armor::sRecordId)
                    return true;
                if (left->getType() == ESM::Weapon::sRecordId)
                    return getWeaponType(left->get<ESM::Weapon>()->mBase->mData.mType)->mWeaponClass
                        == ESM::WeaponType::Melee;
            }
        }
        ESM::RefId type;
        getActiveWeapon(actor, &type);
        return type == ESM::WeaponType::HandToHand
            || (isWeaponType(type) && getWeaponType(type)->mWeaponClass == ESM::WeaponType::Melee);
    }

    namespace
    {
        void playEldenBlockSound(const MWWorld::Ptr& actor)
        {
            const auto soundManager = MWBase::Environment::get().getSoundManager();
            // Keep the physical impact audible with or without the optional
            // imported library. Use the shield's native armor material.
            std::string_view impact = "Heavy Armor Hit";
            if (actor.getClass().hasInventoryStore(actor))
            {
                auto& inventory = actor.getClass().getInventoryStore(actor);
                const auto shield = inventory.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
                if (shield != inventory.end() && shield->getType() == ESM::Armor::sRecordId)
                {
                    const auto skill = shield->getClass().getEquipmentSkill(*shield);
                    if (skill == ESM::Skill::LightArmor)
                        impact = "Light Armor Hit";
                    else if (skill == ESM::Skill::MediumArmor)
                        impact = "Medium Armor Hit";
                }
            }
            soundManager->playSound3D(actor, ESM::RefId::stringRefId(impact), 1.f, 1.f);

            // The source GuardHit TAE also starts armor movement at contact.
            // Preserve its four random recordings, gain and pitch variation.
            auto& prng = MWBase::Environment::get().getWorld()->getPrng();
            const int variant = Misc::Rng::rollDice(4, prng);
            const float volumeDb = (variant == 2 ? -9.f : -7.f) - Misc::Rng::rollProbability(prng);
            const float pitchCents = (variant == 2 ? 100.f : -50.f)
                - 350.f + 450.f * Misc::Rng::rollProbability(prng);
            soundManager->playSound3D(actor,
                VFS::Path::Normalized("sound/eldenring/dodge/block-" + std::to_string(variant) + ".ogg"),
                std::pow(10.f, volumeDb / 20.f), std::pow(2.f, pitchCents / 1200.f));
        }

        EldenCombat::WeaponClass weaponClass(const MWWorld::Ptr& weapon)
        {
            using Class = EldenCombat::WeaponClass;
            if (!weapon.isEmpty() && weapon.getType() == ESM::Armor::sRecordId)
                return Class::Shield;
            if (weapon.isEmpty() || weapon.getType() != ESM::Weapon::sRecordId)
                return Class::Fist;
            const ESM::RefId type = weapon.get<ESM::Weapon>()->mBase->mData.mType;
            if (type == ESM::WeaponType::ShortBladeOneHand)
                return Class::Dagger;
            if (type == ESM::WeaponType::LongBladeTwoHand)
                return Class::Greatsword;
            if (type == ESM::WeaponType::BluntOneHand)
                return Class::Hammer;
            if (type == ESM::WeaponType::BluntTwoClose || type == ESM::WeaponType::BluntTwoWide)
                return Class::GreatHammer;
            if (type == ESM::WeaponType::SpearTwoWide)
                return Class::Spear;
            if (type == ESM::WeaponType::AxeOneHand)
                return Class::Axe;
            if (type == ESM::WeaponType::AxeTwoHand)
                return Class::Greataxe;
            return Class::StraightSword;
        }
    }

    EldenCombat::Config eldenCombatConfig(const MWWorld::Ptr& actor)
    {
        EldenCombat::Config config;
        // Six animation frames stop simultaneous crowd contacts from restarting
        // the player's reaction; NPCs retain their existing hit behavior.
        if (actor == getPlayer())
            config.mPostHitProtectionSeconds = 6.f / EldenCombat::AnimationFps;
        const auto& cls = actor.getClass();
        config.mHasShield = false;
        config.mMaximumStamina = std::max(0.f, cls.getCreatureStats(actor).getFatigue().getModified());
        config.mMaximumStance = std::max(30.f, cls.getCreatureStats(actor)
            .getAttribute(ESM::Attribute::Endurance).getModified());
        config.mWeaponClass = EldenCombat::WeaponClass::Fist;

        // Equipment load uses worn items, excluding ammunition and unequipped loot.
        float equipmentWeight = 0.f;
        if (cls.hasInventoryStore(actor))
        {
            auto& inv = cls.getInventoryStore(actor);
            for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
            {
                const auto item = inv.getSlot(slot);
                if (item != inv.end() && slot != MWWorld::InventoryStore::Slot_Ammunition)
                    equipmentWeight += item->getClass().getWeight(*item);
            }
            const auto right = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
            if (right != inv.end() && right->getType() == ESM::Weapon::sRecordId)
            {
                config.mWeaponClass = weaponClass(*right);
                config.mSpeedMultiplier = std::clamp(right->get<ESM::Weapon>()->mBase->mData.mSpeed, 0.25f, 3.f);
            }
            const auto left = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
            config.mHasShield = left != inv.end() && left->getType() == ESM::Armor::sRecordId;
            config.mHasLeftWeapon = config.mHasShield
                || (left != inv.end() && left->getType() == ESM::Weapon::sRecordId);
            if (config.mHasLeftWeapon)
            {
                config.mLeftWeaponClass = weaponClass(*left);
                config.mPairedWeapons = config.mWeaponClass == config.mLeftWeaponClass;
            }
            else if (left != inv.end() && left->getType() == ESM::Light::sRecordId)
            {
                config.mHasLeftWeapon = true;
                config.mLeftWeaponClass = EldenCombat::WeaponClass::Torch;
            }
            const auto& input = cls.getCreatureStats(actor).getEldenCombatInput();
            if (input.mTwoHanded && input.mTwoHandedLeft && config.mHasLeftWeapon)
            {
                config.mWeaponClass = config.mLeftWeaponClass;
                config.mPairedWeapons = false;
                config.mSpeedMultiplier = left->getType() == ESM::Weapon::sRecordId
                    ? std::clamp(left->get<ESM::Weapon>()->mBase->mData.mSpeed, 0.25f, 3.f) : 1.f;
            }
        }
        const float load = equipmentWeight / std::max(1.f, cls.getCapacity(actor));
        config.mLoad = load < 0.3f ? EldenCombat::Load::Light
            : load < 0.7f ? EldenCombat::Load::Medium : EldenCombat::Load::Heavy;
        return config;
    }

    void storeEldenStamina(const MWWorld::Ptr& actor)
    {
        auto& stats = actor.getClass().getCreatureStats(actor);
        auto fatigue = stats.getFatigue();
        if (actor == getPlayer() && MWBase::Environment::get().getWorld()->getGodModeState())
            return;
        fatigue.setCurrent(stats.getEldenCombat().stamina());
        stats.setFatigue(fatigue);
    }

    MWWorld::Ptr eldenAttackWeapon(const MWWorld::Ptr& actor)
    {
        if (!actor.getClass().hasInventoryStore(actor))
            return {};
        auto& inventory = actor.getClass().getInventoryStore(actor);
        const auto& stats = actor.getClass().getCreatureStats(actor);
        const auto hit = stats.getEldenCombat().activeHit();
        const auto& input = stats.getEldenCombatInput();
        const bool left = useEldenMelee(actor)
            && (hit ? hit->mLeftHand : (stats.getEldenCombat().attack().mLeftHand
                || (input.mTwoHanded && input.mTwoHandedLeft)));
        const auto item = inventory.getSlot(left ? MWWorld::InventoryStore::Slot_CarriedLeft
                                                : MWWorld::InventoryStore::Slot_CarriedRight);
        return item != inventory.end() && item->getType() == ESM::Weapon::sRecordId ? *item : MWWorld::Ptr{};
    }

    void cycleEldenOffhand(const MWWorld::Ptr& actor)
    {
        if (!useEldenMelee(actor) || !actor.getClass().hasInventoryStore(actor))
            return;
        if (actor.getClass().getCreatureStats(actor).getEldenCombat().action() != EldenCombat::Action::Idle)
            return;
        auto& inventory = actor.getClass().getInventoryStore(actor);
        const auto right = inventory.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
        const auto current = inventory.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
        std::vector<MWWorld::ContainerStoreIterator> candidates;
        for (auto item = inventory.begin(); item != inventory.end(); ++item)
        {
            if (item == right || (item->getClass().hasItemHealth(*item) && item->getClass().getItemHealth(*item) <= 0))
                continue;
            const auto slots = item->getClass().getEquipmentSlots(*item).first;
            if (std::find(slots.begin(), slots.end(), MWWorld::InventoryStore::Slot_CarriedLeft) != slots.end())
                candidates.push_back(item);
        }
        auto next = candidates.begin();
        if (current != inventory.end())
        {
            next = std::find(candidates.begin(), candidates.end(), current);
            if (next != candidates.end())
                ++next;
        }
        if (next == candidates.end())
            inventory.unequipSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
        else
            inventory.equip(MWWorld::InventoryStore::Slot_CarriedLeft, *next);
        auto& stats = actor.getClass().getCreatureStats(actor);
        if (EldenCombat::Controller::isAttack(stats.getEldenCombat().action()) || stats.getEldenCombat().guarding())
        {
            stats.getEldenCombat().reset(true);
            stats.getEldenCombat().setVitals(stats.getFatigue().getCurrent(), stats.getFatigue().getModified());
        }
        stats.getEldenCombatInput().mTwoHanded = false;
        stats.getEldenCombatInput().mTwoHandedLeft = false;
    }

    bool toggleEldenGrip(const MWWorld::Ptr& actor, bool left)
    {
        if (!useEldenCombat(actor) || !actor.getClass().hasInventoryStore(actor))
            return false;
        auto& stats = actor.getClass().getCreatureStats(actor);
        if (stats.getDrawState() != DrawState::Weapon || stats.getEldenCombat().action() != EldenCombat::Action::Idle)
            return false;
        auto& inventory = actor.getClass().getInventoryStore(actor);
        const auto item = inventory.getSlot(left ? MWWorld::InventoryStore::Slot_CarriedLeft
                                                : MWWorld::InventoryStore::Slot_CarriedRight);
        if (item == inventory.end())
            return false;
        if (item->getType() == ESM::Weapon::sRecordId)
        {
            if (getWeaponType(item->get<ESM::Weapon>()->mBase->mData.mType)->mWeaponClass != ESM::WeaponType::Melee)
                return false;
        }
        else if (!left || (item->getType() != ESM::Light::sRecordId && item->getType() != ESM::Armor::sRecordId))
            return false;
        auto& input = stats.getEldenCombatInput();
        const bool release = input.mTwoHanded && input.mTwoHandedLeft == left;
        input.mTwoHanded = !release;
        input.mTwoHandedLeft = !release && left;
        input.mLight = input.mHeavy = input.mGuard = input.mCritical = {};
        stats.getEldenCombat().reset(true);
        stats.getEldenCombat().setVitals(stats.getFatigue().getCurrent(), stats.getFatigue().getModified());
        return true;
    }

    float eldenDamageMultiplier(const MWWorld::Ptr& actor)
    {
        if (!useEldenMelee(actor) || !actor.getClass().getCreatureStats(actor).isEldenCombatActive())
            return 1.f;
        const auto hit = actor.getClass().getCreatureStats(actor).getEldenCombat().activeHit();
        return hit ? hit->mDamageMultiplier : 1.f;
    }

    bool dodgeEldenMagicProjectile(const MWWorld::Ptr& victim)
    {
        if (!useEldenCombat(victim))
            return false;
        const auto& stats = victim.getClass().getCreatureStats(victim);
        const auto& combat = stats.getEldenCombat();
        if (!stats.isEldenCombatActive() || stats.isDead() || combat.action() == EldenCombat::Action::Dead
            || !combat.invincible())
            return false;

        // Only action invincibility applies to magic. Physical hit protection,
        // guard, stance damage and hit reactions remain in resolveEldenHit.
        MWBase::Environment::get().getSoundManager()->playSound3D(victim, ESM::RefId::stringRefId("miss"), 1.f, 1.f);
        return true;
    }

    bool resolveEldenHit(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, float& damage, bool ranged)
    {
        if (!useEldenCombat(victim))
            return true;
        auto& stats = victim.getClass().getCreatureStats(victim);
        auto& combat = stats.getEldenCombat();
        // Both melee and projectiles enter here before their physical contact
        // effects. Do not spend critical vulnerability or restart hit-stop when
        // another attacker contacts the player during the protection window.
        if (combat.postHitProtectionRemaining() > 0.f)
            return false;
        combat.configure(eldenCombatConfig(victim));
        if (!stats.isEldenCombatActive())
        {
            // Swimming/paralysis/scripted controls disable the action controller,
            // but should not erase or freeze this short physical-hit protection.
            combat.protectAfterHit(damage);
            return true;
        }
        combat.setVitals(stats.getFatigue().getCurrent(), stats.getFatigue().getModified());

        EldenCombat::Attack attack;
        if (!ranged && useEldenMelee(attacker))
        {
            const auto hit = attacker.getClass().getCreatureStats(attacker).getEldenCombat().activeHit();
            if (hit)
                attack = *hit;
        }
        EldenCombat::Incoming incoming;
        incoming.mDamage = damage;
        incoming.mStaminaDamage = std::max(5.f, damage * attack.mGuardDamage);
        incoming.mStanceDamage = ranged ? 5.f : attack.mStanceDamage;
        incoming.mCritical = attack.mAction == EldenCombat::Action::Critical;
        incoming.mLevel = attack.mStanceDamage >= 30.f ? EldenCombat::HurtLevel::Large
            : attack.mStanceDamage >= 15.f ? EldenCombat::HurtLevel::Middle : EldenCombat::HurtLevel::Small;
        if (!attacker.isEmpty())
        {
            osg::Vec3f direction = attacker.getRefData().getPosition().asVec3()
                - victim.getRefData().getPosition().asVec3();
            direction.z() = 0.f;
            direction.normalize();
            const float yaw = victim.getRefData().getPosition().rot[2];
            incoming.mFacingDot = osg::Vec3f(std::sin(yaw), std::cos(yaw), 0.f) * direction;
        }
        if (incoming.mCritical && !combat.consumeCritical())
            return false;

        const auto result = combat.receiveHit(incoming);
        storeEldenStamina(victim);
        switch (result)
        {
            case EldenCombat::HitResult::Ignored:
            case EldenCombat::HitResult::Dodged:
            case EldenCombat::HitResult::Jumped:
                return false;
            case EldenCombat::HitResult::Blocked:
                damage = 0.f;
                stats.setBlock(true);
                playEldenBlockSound(victim);
                if (victim == getPlayer() && combat.config().mHasShield)
                    victim.getClass().skillUsageSucceeded(victim, ESM::Skill::Block, ESM::Skill::Block_Success);
                break;
            case EldenCombat::HitResult::GuardBroken:
                damage = 0.f;
                playEldenBlockSound(victim);
                stats.setBlock(false);
                stats.setKnockedDown(true);
                break;
            case EldenCombat::HitResult::StanceBroken:
                stats.setBlock(false);
                stats.setKnockedDown(true);
                break;
            case EldenCombat::HitResult::Hit:
                break;
        }
        if (!ranged && !attacker.isEmpty() && useEldenCombat(attacker))
        {
            attacker.getClass().getCreatureStats(attacker).getEldenCombat().applyHitStop(attack.mHitStop);
            combat.applyHitStop(attack.mHitStop);
        }
        return true;
    }
}
