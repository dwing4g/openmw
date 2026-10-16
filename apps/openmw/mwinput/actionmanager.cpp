#include "actionmanager.hpp"

#include <algorithm>
#include <MyGUI_InputManager.h>

#include <SDL_keyboard.h>

#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/globals.hpp"
#include "../mwworld/player.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/drawstate.hpp"
#include "../mwmechanics/eldencombat.hpp"
#include "../mwmechanics/movement.hpp"
#include "../mwmechanics/npcstats.hpp"

#include "../mwrender/camera.hpp"

#include "actions.hpp"
#include "bindingsmanager.hpp"

namespace MWInput
{

    ActionManager::ActionManager(BindingsManager* bindingsManager, std::function<void()> takeScreenshot)
        : mBindingsManager(bindingsManager)
        , mTakeScreenshot(std::move(takeScreenshot))
        , mTimeIdle(0.f)
    {
    }

    void ActionManager::update(float dt)
    {
        updateCombatInput();
        if (mBindingsManager->actionIsActive(A_MoveForward) || mBindingsManager->actionIsActive(A_MoveBackward)
            || mBindingsManager->actionIsActive(A_MoveLeft) || mBindingsManager->actionIsActive(A_MoveRight)
            || mBindingsManager->actionIsActive(A_Jump) || mBindingsManager->actionIsActive(A_Sneak)
            || mBindingsManager->actionIsActive(A_TogglePOV) || mBindingsManager->actionIsActive(A_ZoomIn)
            || mBindingsManager->actionIsActive(A_ZoomOut))
        {
            resetIdleTime();
        }
        else
            mTimeIdle += dt;
    }

    void ActionManager::clearCombatInput()
    {
        mBindingsManager->clearActionButtons();
        const auto& environment = MWBase::Environment::get();
        // Loading screens pump input before the world has a player or camera.
        // Always clear physical buttons, but only touch actors once a game exists.
        if (environment.getStateManager()->getState() == MWBase::StateManager::State_NoGame)
            return;
        const auto world = environment.getWorld();
        const MWWorld::Ptr player = world->getPlayerPtr();
        // World teardown resets the player before input is cleared. Cancelling
        // input must not lazily recreate its stats and inventory in that state.
        if (!player.isEmpty() && player.getRefData().getCustomData())
        {
            auto& stats = player.getClass().getCreatureStats(player);
            stats.getEldenCombat().cancelInputActions();
            auto& input = stats.getEldenCombatInput();
            const bool twoHanded = input.mTwoHanded;
            const bool twoHandedLeft = input.mTwoHandedLeft;
            input = {};
            input.mTwoHanded = twoHanded;
            input.mTwoHandedLeft = twoHandedLeft;
        }
        if (auto* camera = world->getCamera())
            camera->clearCombatLockOn();
    }

    void ActionManager::updateCombatInput()
    {
        const auto& environment = MWBase::Environment::get();
        if (environment.getStateManager()->getState() != MWBase::StateManager::State_Running)
        {
            mBindingsManager->clearActionButtons();
            return;
        }
        const auto manager = environment.getInputManager();
        const MWWorld::Ptr player = environment.getWorld()->getPlayerPtr();
        if (!Settings::game().mEldenRingCombat || player.isEmpty() || manager->controlsDisabled()
            || !manager->getControlSwitch("playercontrols") || environment.getWindowManager()->isGuiMode()
            || manager->getCursorMode() != MWBase::CursorMode::Locked)
        {
            clearCombatInput();
            return;
        }

        auto& stats = player.getClass().getCreatureStats(player);
        if (stats.isDead() || stats.isParalyzed())
        {
            clearCombatInput();
            return;
        }
        if (mBindingsManager->consumeActionButton(A_Activate).mPressed)
        {
            executeAction(A_Activate);
            return; // Activation may teleport the player or open a menu.
        }
        auto& input = stats.getEldenCombatInput();
        auto gather = [&](EldenCombat::Button& destination, int action, bool enabled) {
            const EldenCombat::Button button = mBindingsManager->consumeActionButton(action);
            destination.mHeld = enabled && button.mHeld;
            destination.mPressed |= enabled && button.mPressed;
            destination.mReleased |= button.mReleased;
            if (!enabled)
                destination.mPressed = false;
        };
        const bool fighting = manager->getControlSwitch("playerfighting");
        input.mInCombat = stats.getDrawState() == MWMechanics::DrawState::Weapon && fighting;
        gather(input.mLight, A_Use, input.mInCombat);
        gather(input.mHeavy, A_HeavyAttack, input.mInCombat);
        gather(input.mGuard, A_Block, input.mInCombat);
        gather(input.mCritical, A_CriticalAttack, input.mInCombat);
        gather(input.mDodge, A_Dodge, fighting);
        gather(input.mJump, A_Jump, manager->getControlSwitch("playerjumping"));
        input.mMoveX = std::clamp(mBindingsManager->getActionValue(A_MoveRight)
                - mBindingsManager->getActionValue(A_MoveLeft)
                + mBindingsManager->getActionValue(A_MoveLeftRight) * 2.f - 1.f,
            -1.f, 1.f);
        input.mMoveY = std::clamp(mBindingsManager->getActionValue(A_MoveForward)
                - mBindingsManager->getActionValue(A_MoveBackward)
                - (mBindingsManager->getActionValue(A_MoveForwardBackward) * 2.f - 1.f),
            -1.f, 1.f);
        // Sprint belongs to the held dodge button. Alt is the walking modifier.
        input.mSprint = false;
        input.mCrouching = isSneaking();
        input.mGrounded = environment.getWorld()->isOnGround(player);
        if (mBindingsManager->consumeActionButton(A_TwoHand).mPressed && input.mInCombat)
            MWMechanics::toggleEldenGrip(player, false);
        if (mBindingsManager->consumeActionButton(A_TwoHandLeft).mPressed && input.mInCombat)
            MWMechanics::toggleEldenGrip(player, true);
        if (auto* camera = environment.getWorld()->getCamera())
        {
            if (stats.wasTeleported())
                camera->clearCombatLockOn();
            if (mBindingsManager->consumeActionButton(A_LockOn).mPressed)
                camera->toggleCombatLockOn();
            const MWWorld::Ptr target = camera->getCombatLockOnTarget();
            input.mLockedOn = !target.isEmpty();
        }
    }

    void ActionManager::resetIdleTime()
    {
        mTimeIdle = 0.f;
    }

    void ActionManager::executeAction(int action)
    {
        MWBase::Environment::get().getLuaManager()->inputEvent({ MWBase::LuaManager::InputEvent::Action, action });
        const auto inputManager = MWBase::Environment::get().getInputManager();
        const auto windowManager = MWBase::Environment::get().getWindowManager();
        // trigger action activated
        switch (action)
        {
            case A_GameMenu:
                toggleMainMenu();
                break;
            case A_Screenshot:
                screenshot();
                break;
            case A_Console:
                toggleConsole();
                break;
            case A_Activate:
                inputManager->resetIdleTime();
                activate();
                break;
            case A_CycleOffhand:
                if (Settings::game().mEldenRingCombat && !inputManager->controlsDisabled()
                    && inputManager->getControlSwitch("playercontrols")
                    && inputManager->getControlSwitch("playerfighting") && !windowManager->isGuiMode()
                    && inputManager->getCursorMode() == MWBase::CursorMode::Locked
                    && MWBase::Environment::get().getStateManager()->getState() == MWBase::StateManager::State_Running
                    && checkAllowedToUseItems())
                {
                    const MWWorld::Ptr player = MWMechanics::getPlayer();
                    const auto& stats = player.getClass().getCreatureStats(player);
                    if (stats.getDrawState() == MWMechanics::DrawState::Weapon && !stats.isDead()
                        && !stats.isParalyzed())
                        MWMechanics::cycleEldenOffhand(player);
                }
                break;
            case A_MoveLeft:
            case A_MoveRight:
            case A_MoveForward:
            case A_MoveBackward:
                handleGuiArrowKey(action);
                break;
            case A_Rest:
                rest();
                break;
            case A_QuickKey1:
                quickKey(1);
                break;
            case A_QuickKey2:
                quickKey(2);
                break;
            case A_QuickKey3:
                quickKey(3);
                break;
            case A_QuickKey4:
                quickKey(4);
                break;
            case A_QuickKey5:
                quickKey(5);
                break;
            case A_QuickKey6:
                quickKey(6);
                break;
            case A_QuickKey7:
                quickKey(7);
                break;
            case A_QuickKey8:
                quickKey(8);
                break;
            case A_QuickKey9:
                quickKey(9);
                break;
            case A_QuickKey10:
                quickKey(10);
                break;
            case A_ToggleHUD:
                windowManager->setHudVisibility(!windowManager->isHudVisible());
                break;
            case A_ToggleDebug:
                windowManager->toggleDebugWindow();
                break;
            case A_TogglePostProcessorHUD:
                windowManager->togglePostProcessorHud();
                break;
            case A_QuickSave:
                quickSave();
                break;
            case A_QuickLoad:
                quickLoad();
                break;
            case A_CycleSpellLeft:
                if (checkAllowedToUseItems() && windowManager->isAllowed(MWGui::GW_Magic))
                    MWBase::Environment::get().getWindowManager()->cycleSpell(false);
                break;
            case A_CycleSpellRight:
                if (checkAllowedToUseItems() && windowManager->isAllowed(MWGui::GW_Magic))
                    MWBase::Environment::get().getWindowManager()->cycleSpell(true);
                break;
            case A_CycleWeaponLeft:
                if (checkAllowedToUseItems() && windowManager->isAllowed(MWGui::GW_Inventory))
                    MWBase::Environment::get().getWindowManager()->cycleWeapon(false);
                break;
            case A_CycleWeaponRight:
                if (checkAllowedToUseItems() && windowManager->isAllowed(MWGui::GW_Inventory))
                    MWBase::Environment::get().getWindowManager()->cycleWeapon(true);
                break;
            case A_Inventory:
            case A_Journal:
            case A_QuickKeysMenu:
                // Handled in Lua
                break;
        }
    }

    bool ActionManager::checkAllowedToUseItems() const
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.getClass().getNpcStats(player).isWerewolf())
        {
            // Cannot use items or spells while in werewolf form
            MWBase::Environment::get().getWindowManager()->messageBox("#{sWerewolfRefusal}");
            return false;
        }
        return true;
    }

    void ActionManager::screenshot()
    {
        mTakeScreenshot();
    }

    void ActionManager::toggleMainMenu()
    {
        if (MyGUI::InputManager::getInstance().isModalAny())
        {
            MWBase::Environment::get().getWindowManager()->exitCurrentModal();
            return;
        }

        if (MWBase::Environment::get().getWindowManager()->isConsoleMode())
        {
            MWBase::Environment::get().getWindowManager()->toggleConsole();
            return;
        }

        if (MWBase::Environment::get().getWindowManager()->isPostProcessorHudVisible())
        {
            MWBase::Environment::get().getWindowManager()->togglePostProcessorHud();
            return;
        }

        if (!MWBase::Environment::get().getWindowManager()->isGuiMode()) // No open GUIs, open up the MainMenu
        {
            MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_MainMenu);
        }
        else // Close current GUI
        {
            MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode();
        }
    }

    void ActionManager::quickLoad()
    {
        if (!MyGUI::InputManager::getInstance().isModalAny())
            MWBase::Environment::get().getStateManager()->quickLoad();
    }

    void ActionManager::quickSave()
    {
        if (!MyGUI::InputManager::getInstance().isModalAny())
            MWBase::Environment::get().getStateManager()->quickSave();
    }

    void ActionManager::rest()
    {
        if (!MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols"))
            return;

        if (!MWBase::Environment::get().getWindowManager()->getRestEnabled()
            || MWBase::Environment::get().getWindowManager()->isGuiMode())
            return;

        MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Rest); // Open rest GUI
    }

    void ActionManager::toggleConsole()
    {
        if (MyGUI::InputManager::getInstance().isModalAny())
            return;

        MWBase::Environment::get().getWindowManager()->toggleConsole();
    }

    void ActionManager::quickKey(int index)
    {
        if (!MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols")
            || !MWBase::Environment::get().getInputManager()->getControlSwitch("playerfighting")
            || !MWBase::Environment::get().getInputManager()->getControlSwitch("playermagic"))
            return;
        if (!checkAllowedToUseItems())
            return;

        if (MWBase::Environment::get().getWorld()->getGlobalFloat(MWWorld::Globals::sCharGenState) != -1)
            return;

        if (!MWBase::Environment::get().getWindowManager()->isGuiMode())
            MWBase::Environment::get().getWindowManager()->activateQuickKey(index);
    }

    void ActionManager::activate()
    {
        if (MWBase::Environment::get().getWindowManager()->isGuiMode())
        {
            bool joystickUsed = MWBase::Environment::get().getInputManager()->joystickLastUsed();
            if (!SDL_IsTextInputActive() && !mBindingsManager->isLeftOrRightButton(A_Activate, joystickUsed))
                MWBase::Environment::get().getWindowManager()->injectKeyPress(MyGUI::KeyCode::Return, 0, false);
        }
        else if (MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols"))
        {
            MWWorld::Player& player = MWBase::Environment::get().getWorld()->getPlayer();
            player.activate();
        }
    }

    bool ActionManager::isSneaking() const
    {
        const MWBase::Environment& env = MWBase::Environment::get();
        return env.getMechanicsManager()->isSneaking(env.getWorld()->getPlayer().getPlayer());
    }

    void ActionManager::handleGuiArrowKey(int action)
    {
        bool joystickUsed = MWBase::Environment::get().getInputManager()->joystickLastUsed();
        // This is currently keyboard-specific code
        // TODO: see if GUI controls can be refactored into a single function
        if (joystickUsed)
            return;

        if (SDL_IsTextInputActive())
            return;

        if (mBindingsManager->isLeftOrRightButton(action, joystickUsed))
            return;

        MyGUI::KeyCode key;
        switch (action)
        {
            case A_MoveLeft:
                key = MyGUI::KeyCode::ArrowLeft;
                break;
            case A_MoveRight:
                key = MyGUI::KeyCode::ArrowRight;
                break;
            case A_MoveForward:
                key = MyGUI::KeyCode::ArrowUp;
                break;
            case A_MoveBackward:
            default:
                key = MyGUI::KeyCode::ArrowDown;
                break;
        }

        MWBase::Environment::get().getWindowManager()->injectKeyPress(key, 0, false);
    }
}
