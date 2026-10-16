#include "aitactics.hpp"

#include <algorithm>
#include <cmath>

namespace EldenCombat
{
    namespace
    {
        float nonnegativeAiTime(float value)
        {
            return std::isfinite(value) ? std::max(0.f, value) : 0.f;
        }

        bool releasedCharge(Action before, Action after)
        {
            return (before == Action::Heavy1Charge && after == Action::Heavy1)
                || (before == Action::Heavy2Charge && after == Action::Heavy2);
        }

        float cancelFrame(const ActionDef& definition, AiAttackButton button)
        {
            switch (button)
            {
                case AiAttackButton::Light:
                    return definition.mCancelLight;
                case AiAttackButton::Heavy:
                    return definition.mCancelHeavy;
                case AiAttackButton::Left:
                    return definition.mCancelLeft;
            }
            return definition.mDuration;
        }
    }

    void AiTactics::reset()
    {
        *this = AiTactics();
    }

    void AiTactics::update(const Controller& controller, float seconds, float attackDelay)
    {
        const Action action = controller.action();
        const bool attacking = Controller::isAttack(action);
        const bool wasAttacking = Controller::isAttack(mAction);
        if (wasAttacking && !attacking)
            mAttackCooldown = nonnegativeAiTime(attackDelay);
        else if (action == Action::Idle || action == Action::Jump)
            mAttackCooldown = std::max(0.f, mAttackCooldown - nonnegativeAiTime(seconds));

        if (!attacking)
        {
            mComboCount = 0;
            mComboRequested = false;
        }
        else if (controller.sequence() != mSequence || action != mAction)
        {
            // Releasing a charged heavy swaps its controller action, but remains
            // the same attack for the combo budget.
            if (!(controller.sequence() == mSequence + 1 && releasedCharge(mAction, action)))
                mComboCount = wasAttacking ? mComboCount + 1 : 1;
            mComboRequested = false;
        }
        mAction = action;
        mSequence = controller.sequence();
    }

    bool AiTactics::attackReady() const
    {
        return !Controller::isAttack(mAction) && mAttackCooldown <= 0.f;
    }

    bool AiTactics::canCombo(const Controller& controller, AiAttackButton button, int maximumCombo) const
    {
        if (!Controller::isAttack(controller.action()) || controller.action() == Action::Critical
            || mComboCount <= 0 || mComboCount >= maximumCombo
            || (mComboRequested && mComboRequestSequence == controller.sequence()))
            return false;

        const ActionDef& definition = controller.definition();
        const float cancel = cancelFrame(definition, button);
        if (!std::isfinite(cancel) || cancel > definition.mDuration)
            return false;
        // A modest lead avoids missing cancels that also permit movement on the
        // same frame, including when the engine advances several ticks at once.
        constexpr float ComboBufferFrames = 8.f;
        return controller.frame() >= std::max(definition.mInputFrom, cancel - ComboBufferFrames);
    }

    void AiTactics::markComboRequested(const Controller& controller)
    {
        mComboRequested = true;
        mComboRequestSequence = controller.sequence();
    }

    void AiTactics::observeThreat(const Controller& opponent, float seconds)
    {
        const Action action = opponent.action();
        if (!Controller::isAttack(action))
        {
            mThreatSeconds = 0.f;
            mReactionConsumed = false;
        }
        else if ((opponent.sequence() == mThreatSequence && action == mThreatAction)
            || (opponent.sequence() == mThreatSequence + 1 && releasedCharge(mThreatAction, action)))
            mThreatSeconds += nonnegativeAiTime(seconds);
        else
        {
            mThreatSeconds = 0.f;
            mReactionConsumed = false;
        }
        mThreatAction = action;
        mThreatSequence = opponent.sequence();
    }

    bool AiTactics::consumeReaction(float reactionSeconds)
    {
        if (!Controller::isAttack(mThreatAction) || mReactionConsumed
            || mThreatSeconds < nonnegativeAiTime(reactionSeconds))
            return false;
        mReactionConsumed = true;
        return true;
    }
}
