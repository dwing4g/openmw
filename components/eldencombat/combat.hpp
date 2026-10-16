#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_COMBAT_H
#define OPENMW_COMPONENTS_ELDENCOMBAT_COMBAT_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Adapted from ELDEN-RING-Combat-Rewrite, Copyright (c) 2026 Funny-Bones and contributors.
// See LICENSE-MIT.txt for the upstream license and README.txt for the available source data.
namespace EldenCombat
{
    constexpr float AnimationFps = 30.f;
    constexpr float TickSeconds = 1.f / 60.f;
    // The source's paid/landed bit masks accommodate 32 hits per action.
    constexpr std::size_t MaximumHits = 32;

    struct Button
    {
        bool mHeld = false;
        bool mPressed = false;
        bool mReleased = false;
    };

    struct Input
    {
        Button mDodge;
        Button mLight;
        Button mHeavy;
        Button mGuard;
        Button mJump;
        Button mCritical;
        // Actor-local horizontal direction: positive X right, positive Y forward.
        float mMoveX = 0.f;
        float mMoveY = 0.f;
        // Optional validated AI direction, captured with a dodge release so later
        // navigation changes cannot redirect a buffered dodge. Zero requests backstep.
        std::optional<std::array<float, 2>> mDodgeDirection;
        bool mSprint = false;
        bool mInCombat = false;
        bool mGrounded = true;
        bool mCrouching = false;
        bool mLockedOn = false;
        bool mTwoHanded = false;
        // Two-handing the left weapon routes primary attacks to that hand.
        bool mTwoHandedLeft = false;
        // Set by the world adapter after checking the target's vulnerability and reach.
        bool mCriticalAvailable = false;
        // Tactical AiCombat owns these buttons instead of the legacy attack-button adapter.
        bool mAIControlled = false;

        void clearEdges();
    };

    enum class Load
    {
        Light,
        Medium,
        Heavy,
    };

    enum class WeaponClass
    {
        Dagger,
        StraightSword,
        Greatsword,
        ColossalSword,
        ThrustingSword,
        Katana,
        Hammer,
        Axe,
        Spear,
        Halberd,
        HeavyThrustingSword,
        CurvedSword,
        CurvedGreatsword,
        Twinblade,
        GreatHammer,
        Flail,
        Greataxe,
        GreatSpear,
        Reaper,
        Whip,
        Fist,
        Claw,
        ColossalWeapon,
        Torch,
        Shield,
    };

    enum class Action
    {
        Idle,
        Light1,
        Light2,
        Light3,
        Light4,
        Light5,
        Light6,
        Heavy1Charge,
        Heavy1,
        Heavy2Charge,
        Heavy2,
        RunLight,
        RunHeavy,
        RollAttack,
        BackstepAttack,
        CrouchAttack,
        JumpLight,
        JumpHeavy,
        GuardCounter,
        LeftLight1,
        LeftLight2,
        LeftLight3,
        LeftLight4,
        LeftLight5,
        LeftLight6,
        PairedLight1,
        PairedLight2,
        PairedLight3,
        PairedLight4,
        PairedLight5,
        PairedLight6,
        PairedRun,
        PairedRoll,
        PairedBackstep,
        PairedJump,
        Roll,
        CrouchRoll,
        Backstep,
        Jump,
        GuardHit,
        GuardBreak,
        HurtSmall,
        HurtMiddle,
        HurtLarge,
        Knockdown,
        StanceBreak,
        Critical,
        Dead,
    };

    struct Config
    {
        Load mLoad = Load::Medium;
        WeaponClass mWeaponClass = WeaponClass::StraightSword;
        WeaponClass mLeftWeaponClass = WeaponClass::Fist;
        bool mHasShield = true;
        bool mHasLeftWeapon = false;
        bool mPairedWeapons = false;
        bool mAllowStanceBreak = true;
        float mMaximumStamina = 100.f;
        float mMaximumStance = 60.f;
        // Estimates from the upstream simulation, in the normalized 100-point stamina scale.
        float mStaminaRegen = 45.f;
        float mGuardRegenMultiplier = 0.5f;
        float mSprintDrain = 11.f;
        float mGuardStaminaTaken = 0.55f;
        float mSpeedMultiplier = 1.f;
        // Opt-in physical-hit protection; the world adapter enables this for the player only.
        float mPostHitProtectionSeconds = 0.f;
    };

    struct Hit
    {
        float mFrom = 0.f;
        float mTo = 0.f;
        float mDamageMultiplier = 1.f;
        float mStaminaCost = 12.f;
        float mStanceDamage = 10.f;
        float mGuardDamage = 1.f;
        float mHitStop = 0.05f;
        bool mLeftHand = false;
    };

    struct ActionDef
    {
        // Canonical baked ERAN clip name. Empty when using estimated timings.
        std::string mAnimation;
        // Convert this action's clock back to the original clip's 30-fps frames.
        // Imported weapon timings are divided by the actor's weapon speed.
        float mAnimationSpeed = 1.f;
        // All timings use 30-fps animation frames, as in the upstream state machine.
        float mDuration = 0.f;
        float mInputFrom = 0.f;
        float mDodgeInputFrom = 0.f;
        float mCancelLight = 0.f;
        float mCancelHeavy = 0.f;
        float mCancelDodge = 0.f;
        float mCancelJump = 0.f;
        float mCancelGuard = 0.f;
        float mCancelMove = 0.f;
        float mCancelLeft = 0.f;
        float mInvincibleFrom = 0.f;
        float mInvincibleTo = 0.f;
        float mChargeFrom = 0.f;
        float mChargeTo = 0.f;
        float mStartCost = 0.f;
        float mDistance = 0.f;
        float mStrength = 0.5f;
        std::array<Hit, MaximumHits> mHits{};
        std::size_t mHitCount = 0;
        // Cumulative [left, up, forward] metres, sampled once per original 30-fps frame.
        std::vector<std::array<float, 3>> mRootMotion;
        float mRootMotionSpeed = 1.f;
        // Imported jump swings use separate clips and clocks after touching down.
        // The short recovery is used when the airborne hit window has finished.
        std::shared_ptr<const ActionDef> mLanding;
        std::shared_ptr<const ActionDef> mLandingShort;
    };

    // Return the extracted weapon stance's idle, guard or locomotion clip name.
    // Without local imported data this selects the base category (a000).
    std::string stanceAnimation(WeaponClass weapon, bool twoHanded, unsigned int animationId);

    struct Attack
    {
        Action mAction = Action::Idle;
        std::uint64_t mSequence = 0;
        std::size_t mHitIndex = 0;
        float mStrength = 0.5f;
        float mDamageMultiplier = 1.f;
        float mStanceDamage = 10.f;
        float mGuardDamage = 1.f;
        float mHitStop = 0.05f;
        bool mTwoHanded = false;
        bool mLeftHand = false;
        bool mPaired = false;
    };

    enum class HurtLevel
    {
        Small,
        Middle,
        Large,
        Knockdown,
    };

    struct Incoming
    {
        float mDamage = 0.f;
        float mStaminaDamage = 0.f;
        float mStanceDamage = 0.f;
        // Dot product of facing and normalized direction from victim to attacker.
        float mFacingDot = 1.f;
        float mAirborneClearance = 0.f;
        bool mLow = false;
        bool mUnblockable = false;
        bool mCritical = false;
        HurtLevel mLevel = HurtLevel::Middle;
    };

    enum class HitResult
    {
        Ignored,
        Dodged,
        Jumped,
        Blocked,
        GuardBroken,
        Hit,
        StanceBroken,
    };

    struct Update
    {
        Action mAction = Action::Idle;
        float mFrame = 0.f;
        float mDuration = 0.f;
        bool mActionChanged = false;
        bool mAttackStarted = false;
        // Retained even when a long update advances past the jump action.
        bool mJumpStarted = false;
        // Speed before the accepted jump clears sprint state, retained for physics launch.
        float mJumpMovementMultiplier = 1.f;
        bool mMovementLocked = false;
        // True only when hit-stop paused the entire update. A partial freeze
        // retains its advanced time and displacement for the world adapter.
        bool mFrozen = false;
        float mAdvancedSeconds = 0.f;
        // Dodge direction remains constant for the action; distances are in metres.
        float mMoveX = 0.f;
        float mMoveY = 0.f;
        float mDisplacementX = 0.f;
        float mDisplacementY = 0.f;
        float mMovementMultiplier = 1.f;
        float mStaminaSpent = 0.f;
        float mStaminaRegenerated = 0.f;
    };

    class Controller
    {
    public:
        Controller();

        void configure(const Config& config);
        const Config& config() const { return mConfig; }
        // Fatigue remains owned by OpenMW. Synchronize before update and copy stamina back afterwards.
        void setVitals(float currentStamina, float maximumStamina);
        float stamina() const { return mStamina; }
        float maximumStamina() const { return mConfig.mMaximumStamina; }
        float stance() const { return mStance; }

        Update update(const Input& input, float seconds);
        HitResult receiveHit(const Incoming& hit);
        Attack attack() const;
        std::optional<Attack> activeHit() const;
        // A hit remains available throughout its window until world contact consumes it.
        void markHit();
        void applyHitStop(float seconds);
        float hitStopRemaining() const { return mHitStopSeconds; }
        // Called only after an unblocked physical hit; zero damage cannot grant protection.
        void protectAfterHit(float damage);
        float postHitProtectionRemaining() const { return mPostHitProtectionRemaining; }
        // Inactive world adapters still advance this timer; update() handles active actors.
        void advanceHitProtection(float seconds);
        void setDead(bool dead);
        void reset(bool preserveHitProtection = false);
        void clearRequests();
        // Release input ownership (menus/control changes) without refreshing
        // stamina, stance, vulnerability or an ongoing defensive reaction.
        void cancelInputActions();

        Action action() const { return mAction; }
        float frame() const { return mFrame; }
        const ActionDef& definition() const { return mDef; }
        bool invincible() const;
        bool guarding() const { return mGuarding; }
        bool guardUp() const;
        bool guardCounterReady() const { return mGuardCounterFrames > 0.f; }
        bool sprinting() const { return mSprinting; }
        bool criticalReady() const { return mCriticalSeconds > 0.f; }
        bool consumeCritical();
        std::uint64_t sequence() const { return mSequence; }

        static bool isAttack(Action action);
        static ActionDef actionDefinition(Action action, const Config& config, bool twoHanded = false);
        static std::array<float, 3> rootMotionAt(const ActionDef& definition, float frame);

    private:
        enum class Request
        {
            Light,
            Heavy,
            Left,
            Dodge,
            Jump,
            Critical,
        };

        struct BufferedRequest
        {
            Request mRequest;
            std::optional<std::array<float, 2>> mDodgeDirection;
        };

        void tick(const Input& input, float seconds, Update& result);
        void readButtons(const Input& input, float frames);
        void start(Action action, const Input& input, Update* result = nullptr);
        void startRequest(const BufferedRequest& request, const Input& input, Update& result);
        Action nextLight() const;
        Action nextHeavy() const;
        Action nextLeft() const;
        bool pairedWeaponsActive() const;
        bool leftButtonAttacks(const Input& input) const;
        float cancelFrame(Request request) const;
        void spend(float normalizedCost, Update* result = nullptr);
        Attack makeAttack(std::size_t hitIndex) const;
        void collectHits(float previousFrame);
        void landAttack(Update& result);

        Config mConfig;
        ActionDef mDef;
        Action mAction = Action::Idle;
        float mFrame = 0.f;
        float mStamina = 100.f;
        float mStance = 60.f;
        float mGuardFrames = 0.f;
        float mGuardCounterFrames = 0.f;
        float mDodgeHoldFrames = 0.f;
        // Brief terrain contact gaps must not alternate sprint and run poses.
        // This only continues an existing sprint; all action gates use mGrounded.
        float mSprintGroundGraceRemaining = 0.f;
        float mHitStopSeconds = 0.f;
        float mPostHitProtectionRemaining = 0.f;
        float mCriticalSeconds = 0.f;
        float mSinceHitSeconds = 0.f;
        float mDodgeX = 0.f;
        float mDodgeY = 0.f;
        bool mDodgeArmed = false;
        bool mSprintSpent = false;
        bool mSprinting = false;
        bool mGuarding = false;
        bool mTwoHanded = false;
        bool mTwoHandedLeft = false;
        bool mGrounded = true;
        // A jump or jump attack may remain grounded while its accepted physics
        // pulse waits for a fixed step. Land it only after observing flight.
        bool mJumpWasAirborne = false;
        std::uint32_t mPaidHits = 0;
        std::uint32_t mLandedHits = 0;
        std::uint64_t mSequence = 0;
        // Crossed windows remain available until the next update, even at low frame rates.
        std::array<Attack, MaximumHits * 2> mAvailableHits{};
        std::size_t mAvailableHitCount = 0;
        std::optional<BufferedRequest> mBuffer;
    };
}

#endif
