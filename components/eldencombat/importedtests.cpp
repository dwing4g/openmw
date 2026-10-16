#include "combat.hpp"
#include "animations.hpp"
#include "imported_timings.hpp"

#include <cmath>
#include <fstream>
#include <memory>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace EldenCombat;

    void check(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    void validate(const ActionDef& definition)
    {
        check(definition.mAnimation.size() == 11 && definition.mAnimation[0] == 'a'
                && definition.mAnimation[4] == '_', "missing canonical imported animation");
        check(std::isfinite(definition.mAnimationSpeed) && definition.mAnimationSpeed > 0.f,
            "invalid imported animation speed");
        check(std::isfinite(definition.mDuration) && definition.mDuration > 0.f, "invalid imported duration");
        check(definition.mHitCount <= MaximumHits, "too many imported hits");
        check(std::isfinite(definition.mStartCost) && definition.mStartCost >= 0.f, "invalid action cost");
        for (std::size_t index = 0; index < definition.mHitCount; ++index)
        {
            const auto& hit = definition.mHits[index];
            check(std::isfinite(hit.mFrom) && std::isfinite(hit.mTo) && hit.mFrom >= 0.f
                    && hit.mTo >= hit.mFrom && hit.mTo <= definition.mDuration + 0.001f,
                "imported hit window lies outside its action");
            check(std::isfinite(hit.mStaminaCost) && hit.mStaminaCost >= 0.f, "invalid imported stamina cost");
            check(std::isfinite(hit.mDamageMultiplier) && hit.mDamageMultiplier >= 0.f, "invalid motion value");
        }
        for (const auto& sample : definition.mRootMotion)
            for (const float coordinate : sample)
                check(std::isfinite(coordinate), "non-finite imported root motion");
        const auto endpoint = Controller::rootMotionAt(definition, definition.mDuration);
        for (const float coordinate : endpoint)
            check(std::isfinite(coordinate), "non-finite sampled root motion");
    }

    void lightAttack(const Config& config, bool twoHanded, const ActionDef& imported)
    {
        Controller controller;
        controller.configure(config);
        Input input;
        input.mTwoHanded = twoHanded;
        input.mLight = { true, true, false };
        controller.update(input, TickSeconds);
        check(controller.action() == Action::Light1, "imported light did not start");
        check(controller.definition().mDuration == imported.mDuration, "controller selected fallback timing");
        check(controller.definition().mHitCount == imported.mHitCount, "controller lost imported multi-hit data");
        input.mLight = {};
        std::array<bool, MaximumHits> seen{};
        // Cover even slow native weapons without relying on fallback durations.
        const int ticks = static_cast<int>(std::ceil(imported.mDuration * 2.f)) + 4;
        for (int tick = 0; tick < ticks; ++tick)
        {
            while (const auto hit = controller.activeHit())
            {
                check(hit->mHitIndex < imported.mHitCount, "invalid active hit index");
                check(!seen[hit->mHitIndex], "an imported hit was emitted twice");
                seen[hit->mHitIndex] = true;
                check(hit->mDamageMultiplier == imported.mHits[hit->mHitIndex].mDamageMultiplier,
                    "active hit lost its imported damage");
                controller.markHit();
            }
            controller.update(input, TickSeconds);
            check(std::isfinite(controller.stamina()) && controller.stamina() >= 0.f,
                "invalid stamina during an imported attack");
        }
        for (std::size_t index = 0; index < imported.mHitCount; ++index)
            check(seen[index], "an imported hit window was never emitted");
        check(controller.action() == Action::Idle, "imported attack did not recover");
    }

    void availableOpeners(const Config& config, bool twoHanded)
    {
        auto expected = [&](std::initializer_list<Action> actions) {
            for (const Action action : actions)
                if (importedActionDefinition(action, config, twoHanded))
                    return action;
            return Action::Idle;
        };
        auto selected = [&](const Controller& controller, Action action) {
            check(controller.action() == action, "attack did not select the first available imported opener");
            if (action != Action::Idle)
            {
                const auto definition = importedActionDefinition(action, config, twoHanded);
                check(definition && controller.definition().mDuration == definition->mDuration,
                    "an available imported opener was replaced by estimated timing");
            }
        };

        Controller crouching;
        crouching.configure(config);
        Input input;
        input.mTwoHanded = twoHanded;
        input.mCrouching = true;
        input.mLight = { true, true, false };
        crouching.update(input, TickSeconds);
        selected(crouching, expected({ Action::CrouchAttack, Action::RollAttack, Action::Light1 }));

        Controller running;
        running.configure(config);
        input = {};
        input.mTwoHanded = twoHanded;
        input.mMoveY = 1.f;
        input.mSprint = true;
        running.update(input, TickSeconds);
        input.mLight = { true, true, false };
        running.update(input, TickSeconds);
        selected(running, expected({ Action::RunLight, Action::Light1 }));

        Controller rolling;
        rolling.configure(config);
        input = {};
        input.mTwoHanded = twoHanded;
        input.mMoveY = 1.f;
        input.mDodge = { true, true, false };
        rolling.update(input, TickSeconds);
        input.mDodge = { false, false, true };
        rolling.update(input, TickSeconds);
        input.mDodge = {};
        input.mMoveY = 0.f;
        const float listen = rolling.definition().mInputFrom;
        while (rolling.frame() < listen)
            rolling.update(input, TickSeconds);
        input.mLight = { true, true, false };
        rolling.update(input, TickSeconds);
        input.clearEdges();
        for (int tick = 0; rolling.action() == Action::Roll && tick < 400; ++tick)
            rolling.update(input, TickSeconds);
        selected(rolling, expected({ Action::RollAttack, Action::Light1 }));

        if (!twoHanded)
        {
            Config paired = config;
            paired.mHasShield = false;
            paired.mHasLeftWeapon = true;
            paired.mPairedWeapons = true;
            for (const bool sprinting : { false, true })
            {
                Controller left;
                left.configure(paired);
                input = {};
                input.mSprint = sprinting;
                input.mMoveY = sprinting ? 1.f : 0.f;
                left.update(input, TickSeconds);
                input.mGuard = { true, true, false };
                left.update(input, TickSeconds);
                const bool hasPair = importedActionDefinition(Action::PairedLight1, config, false).has_value();
                selected(left, hasPair ? (sprinting ? expected({ Action::PairedRun, Action::PairedLight1 })
                                                   : Action::PairedLight1)
                                      : expected({ Action::LeftLight1 }));
                if (!hasPair && left.action() != Action::Idle)
                    check(left.attack().mLeftHand && !left.attack().mPaired,
                        "unsupported pairing did not use the left weapon's own chain");
            }
        }
    }
}

int main(int argc, char** argv)
{
    try
    {
        std::unique_ptr<EldenCombat::AnimationBank> animations;
        if (argc > 1)
        {
            std::ifstream stream(argv[1], std::ios::binary);
            check(static_cast<bool>(stream), "cannot open local animation bank");
            animations = std::make_unique<EldenCombat::AnimationBank>(stream);
        }
        std::size_t definitions = 0;
        std::size_t movesets = 0;
        for (int weapon = static_cast<int>(EldenCombat::WeaponClass::Dagger);
             weapon <= static_cast<int>(EldenCombat::WeaponClass::Shield); ++weapon)
        {
            for (const bool twoHanded : { false, true })
            {
                if (animations)
                    for (unsigned int id : { 0u, 100u })
                        check(animations->find(EldenCombat::stanceAnimation(
                            static_cast<EldenCombat::WeaponClass>(weapon), twoHanded, id)) != nullptr,
                            "baked bank is missing an imported weapon stance");
                for (const float speed : { 0.75f, 1.f, 1.3f })
                {
                    EldenCombat::Config config;
                    config.mWeaponClass = static_cast<EldenCombat::WeaponClass>(weapon);
                    config.mLeftWeaponClass = config.mWeaponClass;
                    config.mSpeedMultiplier = speed;
                    for (int action = static_cast<int>(EldenCombat::Action::Light1);
                         action < static_cast<int>(EldenCombat::Action::Dead); ++action)
                    {
                        const auto imported = EldenCombat::importedActionDefinition(
                            static_cast<EldenCombat::Action>(action), config, twoHanded);
                        if (!imported)
                            continue;
                        validate(*imported);
                        if (animations)
                            check(animations->find(imported->mAnimation) != nullptr,
                                "baked bank is missing an imported action clip");
                        for (const auto& landing : { imported->mLanding, imported->mLandingShort })
                        {
                            if (!landing)
                                continue;
                            validate(*landing);
                            check(!landing->mLanding && !landing->mLandingShort,
                                "landing phase still contains airborne transition metadata");
                            check(landing->mAnimationSpeed == speed, "landing lost weapon speed scaling");
                            if (animations)
                            {
                                const auto* clip = animations->find(landing->mAnimation);
                                check(clip != nullptr, "baked bank is missing a jump-attack landing clip");
                                check(landing->mDuration * speed <= clip->mFrames - 1 + .001f,
                                    "landing recovery outlasts its clip and freezes the final pose");
                            }
                        }
                        check(imported->mAnimationSpeed ==
                            (EldenCombat::Controller::isAttack(static_cast<EldenCombat::Action>(action)) ? speed : 1.f),
                            "animation speed does not match the action clock");
                        const auto selected = EldenCombat::Controller::actionDefinition(
                            static_cast<EldenCombat::Action>(action), config, twoHanded);
                        check(selected.mDuration == imported->mDuration && selected.mHitCount == imported->mHitCount,
                            "the engine did not select its imported definition");
                        ++definitions;
                    }
                    if (const auto light = EldenCombat::importedActionDefinition(
                            EldenCombat::Action::Light1, config, twoHanded))
                    {
                        lightAttack(config, twoHanded, *light);
                        ++movesets;
                    }
                    if (speed == 1.f)
                        availableOpeners(config, twoHanded);
                }
            }
        }
        for (const auto load : { EldenCombat::Load::Light, EldenCombat::Load::Medium, EldenCombat::Load::Heavy })
        {
            EldenCombat::Config config;
            config.mLoad = load;
            const auto roll = EldenCombat::importedActionDefinition(EldenCombat::Action::Roll, config, false);
            check(roll.has_value(), "missing game-derived equip-load roll");
            validate(*roll);
            if (animations)
                check(animations->find(roll->mAnimation) != nullptr, "baked bank is missing an equip-load roll");
        }
        check(definitions > 0 && movesets > 0, "no game-derived weapon movesets were tested");
        EldenCombat::Controller jumping;
        EldenCombat::Input jumpInput;
        jumpInput.mJump = { true, true, false };
        const auto jumpUpdate = jumping.update(jumpInput, 1.f);
        check(jumpUpdate.mAction != EldenCombat::Action::Jump,
            "long-update jump fixture must advance past the imported jump action");
        check(jumpUpdate.mJumpStarted, "jump launch event was lost when its action finished within one update");
        std::cout << "Imported data passed: " << definitions << " action variants, " << movesets
                  << " light attacks and all three equip-load rolls.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Imported data test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
