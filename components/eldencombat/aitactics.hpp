#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_AITACTICS_H
#define OPENMW_COMPONENTS_ELDENCOMBAT_AITACTICS_H

#include "combat.hpp"

namespace EldenCombat
{
    enum class AiAttackButton
    {
        Light,
        Heavy,
        Left,
    };

    // Timing and action history for NPC tactics. World queries, movement safety,
    // stamina thresholds and randomized choices remain in the engine adapter.
    class AiTactics
    {
    public:
        void reset();

        // Observe every frame, before choosing inputs. The delay begins when the
        // previous attack chain actually finishes, including an interruption.
        // Rolls and hit reactions do not consume the pause between attack chains.
        void update(const Controller& controller, float seconds, float attackDelay);
        bool attackReady() const;
        float attackCooldown() const { return mAttackCooldown; }
        int comboCount() const { return mComboCount; }

        // Queue once per swing, close enough to the selected cancel window that
        // slow weapons do not submit follow-ups far ahead of their recovery.
        bool canCombo(const Controller& controller, AiAttackButton button, int maximumCombo) const;
        void markComboRequested(const Controller& controller);

        // Observe the opponent's actual action, even outside immediate hit range.
        // Losing and regaining range must not reroll a reaction to the same swing.
        void observeThreat(const Controller& opponent, float seconds);
        bool consumeReaction(float reactionSeconds);

    private:
        Action mAction = Action::Idle;
        std::uint64_t mSequence = 0;
        int mComboCount = 0;
        float mAttackCooldown = 0.f;
        bool mComboRequested = false;
        std::uint64_t mComboRequestSequence = 0;

        Action mThreatAction = Action::Idle;
        std::uint64_t mThreatSequence = 0;
        float mThreatSeconds = 0.f;
        bool mReactionConsumed = false;
    };
}

#endif
