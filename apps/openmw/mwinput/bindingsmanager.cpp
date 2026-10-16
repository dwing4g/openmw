#include "bindingsmanager.hpp"

#include <filesystem>

#include <MyGUI_EditBox.h>

#include <oics/ICSChannelListener.h>
#include <oics/ICSInputControlSystem.h>

#include <components/debug/debuglog.hpp>
#include <components/files/conversion.hpp>
#include <components/sdlutil/sdlmappings.hpp>
#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwmechanics/eldencombat.hpp"

#include "actions.hpp"

namespace MWInput
{
    static const int sFakeDeviceId = 1; // As we only support one controller at a time, use a fake deviceID so we don't
                                        // lose bindings when switching controllers

    void clearAllKeyBindings(ICS::InputControlSystem* inputBinder, ICS::Control* control)
    {
        // right now we don't really need multiple bindings for the same action, so remove all others first
        if (inputBinder->getKeyBinding(control, ICS::Control::INCREASE) != SDL_SCANCODE_UNKNOWN)
            inputBinder->removeKeyBinding(inputBinder->getKeyBinding(control, ICS::Control::INCREASE));
        if (inputBinder->getMouseButtonBinding(control, ICS::Control::INCREASE) != ICS_MAX_DEVICE_BUTTONS)
            inputBinder->removeMouseButtonBinding(inputBinder->getMouseButtonBinding(control, ICS::Control::INCREASE));
        if (inputBinder->getMouseWheelBinding(control, ICS::Control::INCREASE)
            != ICS::InputControlSystem::MouseWheelClick::UNASSIGNED)
            inputBinder->removeMouseWheelBinding(inputBinder->getMouseWheelBinding(control, ICS::Control::INCREASE));
    }

    void clearAllControllerBindings(ICS::InputControlSystem* inputBinder, ICS::Control* control)
    {
        // right now we don't really need multiple bindings for the same action, so remove all others first
        if (inputBinder->getJoystickAxisBinding(control, sFakeDeviceId, ICS::Control::INCREASE) != SDL_SCANCODE_UNKNOWN)
            inputBinder->removeJoystickAxisBinding(
                sFakeDeviceId, inputBinder->getJoystickAxisBinding(control, sFakeDeviceId, ICS::Control::INCREASE));
        if (inputBinder->getJoystickButtonBinding(control, sFakeDeviceId, ICS::Control::INCREASE)
            != ICS_MAX_DEVICE_BUTTONS)
            inputBinder->removeJoystickButtonBinding(
                sFakeDeviceId, inputBinder->getJoystickButtonBinding(control, sFakeDeviceId, ICS::Control::INCREASE));
    }

    class InputControlSystem : public ICS::InputControlSystem
    {
    public:
        InputControlSystem(const std::filesystem::path& bindingsFile)
            : ICS::InputControlSystem(Files::pathToUnicodeString(bindingsFile), true, nullptr, nullptr, A_Last)
        {
        }
    };

    class BindingsListener : public ICS::ChannelListener, public ICS::DetectingBindingListener
    {
    public:
        BindingsListener(ICS::InputControlSystem* inputBinder, BindingsManager* bindingsManager)
            : mInputBinder(inputBinder)
            , mBindingsManager(bindingsManager)
            , mDetectingKeyboard(false)
        {
        }

        virtual ~BindingsListener() = default;

        void channelChanged(ICS::Channel* channel, float currentValue, float previousValue) override
        {
            int action = channel->getNumber();
            mBindingsManager->actionValueChanged(action, currentValue, previousValue);
        }

        void keyBindingDetected(ICS::InputControlSystem* ics, ICS::Control* control, SDL_Scancode key,
            ICS::Control::ControlChangingDirection direction) override
        {
            // Disallow binding escape key
            if (key == SDL_SCANCODE_ESCAPE)
            {
                // Stop binding if esc pressed
                mInputBinder->cancelDetectingBindingState();
                MWBase::Environment::get().getWindowManager()->notifyInputActionBound();
                return;
            }

            // Disallow binding reserved keys
            if (key == SDL_SCANCODE_F3 || key == SDL_SCANCODE_F4 || key == SDL_SCANCODE_F10)
                return;

#ifndef __APPLE__
            // Disallow binding Windows/Meta keys
            if (key == SDL_SCANCODE_LGUI || key == SDL_SCANCODE_RGUI)
                return;
#endif

            if (!mDetectingKeyboard)
                return;

            clearAllKeyBindings(mInputBinder, control);
            control->setInitialValue(0.0f);
            ICS::DetectingBindingListener::keyBindingDetected(ics, control, key, direction);
            MWBase::Environment::get().getWindowManager()->notifyInputActionBound();
        }

        void mouseAxisBindingDetected(ICS::InputControlSystem* /*ics*/, ICS::Control* /*control*/,
            ICS::InputControlSystem::NamedAxis /*axis*/, ICS::Control::ControlChangingDirection /*direction*/) override
        {
            // we don't want mouse movement bindings
            return;
        }

        void mouseButtonBindingDetected(ICS::InputControlSystem* ics, ICS::Control* control, unsigned int button,
            ICS::Control::ControlChangingDirection direction) override
        {
            if (!mDetectingKeyboard)
                return;
            clearAllKeyBindings(mInputBinder, control);
            control->setInitialValue(0.0f);
            ICS::DetectingBindingListener::mouseButtonBindingDetected(ics, control, button, direction);
            MWBase::Environment::get().getWindowManager()->notifyInputActionBound();
        }

        void mouseWheelBindingDetected(ICS::InputControlSystem* ics, ICS::Control* control,
            ICS::InputControlSystem::MouseWheelClick click, ICS::Control::ControlChangingDirection direction) override
        {
            if (!mDetectingKeyboard)
                return;
            clearAllKeyBindings(mInputBinder, control);
            control->setInitialValue(0.0f);
            ICS::DetectingBindingListener::mouseWheelBindingDetected(ics, control, click, direction);
            MWBase::Environment::get().getWindowManager()->notifyInputActionBound();
        }

        void joystickAxisBindingDetected(ICS::InputControlSystem* ics, int deviceID, ICS::Control* control, int axis,
            ICS::Control::ControlChangingDirection direction) override
        {
            // only allow binding to the trigers
            if (axis != SDL_CONTROLLER_AXIS_TRIGGERLEFT && axis != SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
                return;
            if (mDetectingKeyboard)
                return;

            clearAllControllerBindings(mInputBinder, control);
            control->setValue(0.5f); // axis bindings must start at 0.5
            control->setInitialValue(0.5f);
            ICS::DetectingBindingListener::joystickAxisBindingDetected(ics, deviceID, control, axis, direction);
            MWBase::Environment::get().getWindowManager()->notifyInputActionBound();
        }

        void joystickButtonBindingDetected(ICS::InputControlSystem* ics, int deviceID, ICS::Control* control,
            unsigned int button, ICS::Control::ControlChangingDirection direction) override
        {
            if (mDetectingKeyboard)
                return;
            clearAllControllerBindings(mInputBinder, control);
            control->setInitialValue(0.0f);
            ICS::DetectingBindingListener::joystickButtonBindingDetected(ics, deviceID, control, button, direction);
            MWBase::Environment::get().getWindowManager()->notifyInputActionBound();
        }

        void setDetectingKeyboard(bool detecting)
        {
            mDetectingKeyboard = detecting;
        }

    private:
        ICS::InputControlSystem* mInputBinder;
        BindingsManager* mBindingsManager;
        bool mDetectingKeyboard;
    };

    BindingsManager::BindingsManager(const std::filesystem::path& userFile, bool userFileExists)
        : mUserFile(userFile)
        , mDragDrop(false)
    {
        const auto file = userFileExists ? userFile : std::filesystem::path();
        mInputBinder = std::make_unique<InputControlSystem>(file);
        mListener = std::make_unique<BindingsListener>(mInputBinder.get(), this);
        mInputBinder->setDetectingBindingListener(mListener.get());

        loadKeyDefaults();
        loadControllerDefaults();

        for (int i = 0; i < A_Last; ++i)
        {
            mInputBinder->getChannel(i)->addListener(mListener.get());
        }
    }

    void BindingsManager::setDragDrop(bool dragDrop)
    {
        mDragDrop = dragDrop;
    }

    BindingsManager::~BindingsManager()
    {
        saveBindings();
    }

    void BindingsManager::update(float dt)
    {
        // update values of channels (as a result of pressed keys)
        mInputBinder->update(dt);
        mCapturedActions = {};
    }

    bool BindingsManager::isLeftOrRightButton(int action, bool joystick) const
    {
        int mouseBinding
            = mInputBinder->getMouseButtonBinding(mInputBinder->getControl(action), ICS::Control::INCREASE);
        if (mouseBinding != ICS_MAX_DEVICE_BUTTONS)
            return true;
        int buttonBinding = mInputBinder->getJoystickButtonBinding(
            mInputBinder->getControl(action), sFakeDeviceId, ICS::Control::INCREASE);
        if (joystick && (buttonBinding == 0 || buttonBinding == 1))
            return true;
        return false;
    }

    void BindingsManager::setPlayerControlsEnabled(bool enabled)
    {
        int playerChannels[] = { A_AutoMove, A_AlwaysRun, A_ToggleWeapon, A_ToggleSpell, A_Rest, A_QuickKey1,
            A_QuickKey2, A_QuickKey3, A_QuickKey4, A_QuickKey5, A_QuickKey6, A_QuickKey7, A_QuickKey8, A_QuickKey9,
            A_QuickKey10, A_Use, A_Journal, A_HeavyAttack, A_Dodge, A_Block, A_LockOn, A_CriticalAttack,
            A_TwoHand, A_CycleOffhand, A_HeavyModifier, A_TwoHandLeft };

        for (int pc : playerChannels)
        {
            mInputBinder->getChannel(pc)->setEnabled(enabled);
        }
    }

    void BindingsManager::setJoystickDeadZone(float deadZone)
    {
        mInputBinder->setJoystickDeadZone(deadZone);
    }

    float BindingsManager::getActionValue(int id) const
    {
        // Lua's native attack controls must see the same light button as the
        // combat controller, so a heavy/grip chord cannot also start a light hit.
        if ((id == A_Use || id >= A_HeavyAttack) && id < A_Last && eldenInputEnabled())
            return mEldenInput.held(id) ? 1.f : 0.f;
        return mInputBinder->getChannel(id)->getValue();
    }

    bool BindingsManager::actionIsActive(int id) const
    {
        return getActionValue(id) == 1.0;
    }

    EldenCombat::Button BindingsManager::consumeActionButton(int id)
    {
        return mEldenInput.consume(id);
    }

    void BindingsManager::clearActionButtons()
    {
        mEldenInput.clear();
    }

    void BindingsManager::loadKeyDefaults(bool force)
    {
        // using hardcoded key defaults is inevitable, if we want the configuration files to stay valid
        // across different versions of OpenMW (in the case where another input action is added)
        std::map<int, SDL_Scancode> defaultKeyBindings;

        // Gets the Keyvalue from the Scancode; gives the button in the same place reguardless of keyboard format
        defaultKeyBindings[A_Activate] = SDL_SCANCODE_SPACE;
        defaultKeyBindings[A_MoveBackward] = SDL_SCANCODE_S;
        defaultKeyBindings[A_MoveForward] = SDL_SCANCODE_W;
        defaultKeyBindings[A_MoveLeft] = SDL_SCANCODE_A;
        defaultKeyBindings[A_MoveRight] = SDL_SCANCODE_D;
        defaultKeyBindings[A_ToggleWeapon] = SDL_SCANCODE_F;
        defaultKeyBindings[A_ToggleSpell] = SDL_SCANCODE_R;
        defaultKeyBindings[A_CycleSpellLeft] = SDL_SCANCODE_MINUS;
        defaultKeyBindings[A_CycleSpellRight] = SDL_SCANCODE_EQUALS;
        defaultKeyBindings[A_CycleWeaponLeft] = SDL_SCANCODE_LEFTBRACKET;
        defaultKeyBindings[A_CycleWeaponRight] = SDL_SCANCODE_RIGHTBRACKET;

        defaultKeyBindings[A_QuickKeysMenu] = SDL_SCANCODE_F1;
        defaultKeyBindings[A_Console] = SDL_SCANCODE_GRAVE;
        defaultKeyBindings[A_Run] = SDL_SCANCODE_LSHIFT;
        defaultKeyBindings[A_Sneak] = SDL_SCANCODE_LCTRL;
        defaultKeyBindings[A_AutoMove] = SDL_SCANCODE_Q;
        defaultKeyBindings[A_Jump] = SDL_SCANCODE_E;
        defaultKeyBindings[A_Journal] = SDL_SCANCODE_J;
        defaultKeyBindings[A_Rest] = SDL_SCANCODE_T;
        defaultKeyBindings[A_GameMenu] = SDL_SCANCODE_ESCAPE;
        defaultKeyBindings[A_TogglePOV] = SDL_SCANCODE_TAB;
        defaultKeyBindings[A_QuickKey1] = SDL_SCANCODE_1;
        defaultKeyBindings[A_QuickKey2] = SDL_SCANCODE_2;
        defaultKeyBindings[A_QuickKey3] = SDL_SCANCODE_3;
        defaultKeyBindings[A_QuickKey4] = SDL_SCANCODE_4;
        defaultKeyBindings[A_QuickKey5] = SDL_SCANCODE_5;
        defaultKeyBindings[A_QuickKey6] = SDL_SCANCODE_6;
        defaultKeyBindings[A_QuickKey7] = SDL_SCANCODE_7;
        defaultKeyBindings[A_QuickKey8] = SDL_SCANCODE_8;
        defaultKeyBindings[A_QuickKey9] = SDL_SCANCODE_9;
        defaultKeyBindings[A_QuickKey10] = SDL_SCANCODE_0;
        defaultKeyBindings[A_Screenshot] = SDL_SCANCODE_F12;
        defaultKeyBindings[A_ToggleHUD] = SDL_SCANCODE_F11;
        defaultKeyBindings[A_ToggleDebug] = SDL_SCANCODE_F10;
        defaultKeyBindings[A_AlwaysRun] = SDL_SCANCODE_CAPSLOCK;
        defaultKeyBindings[A_QuickSave] = SDL_SCANCODE_F5;
        defaultKeyBindings[A_QuickLoad] = SDL_SCANCODE_F9;
        defaultKeyBindings[A_TogglePostProcessorHUD] = SDL_SCANCODE_F2;
        defaultKeyBindings[A_HeavyAttack] = SDL_SCANCODE_G;
        defaultKeyBindings[A_Dodge] = SDL_SCANCODE_V;
        defaultKeyBindings[A_Block] = SDL_SCANCODE_C;
        defaultKeyBindings[A_CriticalAttack] = SDL_SCANCODE_B;
        defaultKeyBindings[A_TwoHand] = SDL_SCANCODE_H;
        defaultKeyBindings[A_CycleOffhand] = SDL_SCANCODE_N;

        if (Settings::game().mEldenRingCombat)
        {
            defaultKeyBindings[A_Activate] = SDL_SCANCODE_E;
            defaultKeyBindings[A_Dodge] = SDL_SCANCODE_SPACE;
            defaultKeyBindings[A_Jump] = SDL_SCANCODE_F;
            defaultKeyBindings[A_Sneak] = SDL_SCANCODE_X;
            defaultKeyBindings[A_Run] = SDL_SCANCODE_LALT;
            defaultKeyBindings[A_HeavyModifier] = SDL_SCANCODE_LSHIFT;
            defaultKeyBindings[A_LockOn] = SDL_SCANCODE_Q;
            defaultKeyBindings[A_CycleWeaponRight] = SDL_SCANCODE_RIGHT;
            defaultKeyBindings[A_CycleOffhand] = SDL_SCANCODE_LEFT;
            defaultKeyBindings[A_Inventory] = SDL_SCANCODE_TAB;
            defaultKeyBindings[A_TogglePOV] = SDL_SCANCODE_V;
            defaultKeyBindings[A_ToggleWeapon] = SDL_SCANCODE_R;
            defaultKeyBindings[A_ToggleSpell] = SDL_SCANCODE_C;
            defaultKeyBindings[A_AutoMove] = SDL_SCANCODE_Z;
            defaultKeyBindings.erase(A_Block);
        }

        std::map<int, int> defaultMouseButtonBindings;
        defaultMouseButtonBindings[A_Inventory] = SDL_BUTTON_RIGHT;
        defaultMouseButtonBindings[A_Use] = SDL_BUTTON_LEFT;
        defaultMouseButtonBindings[A_LockOn] = SDL_BUTTON_MIDDLE;
        if (Settings::game().mEldenRingCombat)
        {
            defaultMouseButtonBindings.erase(A_Inventory);
            defaultMouseButtonBindings[A_Block] = SDL_BUTTON_RIGHT;
        }

        std::map<int, ICS::InputControlSystem::MouseWheelClick> defaultMouseWheelBindings;
        defaultMouseWheelBindings[A_ZoomIn] = ICS::InputControlSystem::MouseWheelClick::UP;
        defaultMouseWheelBindings[A_ZoomOut] = ICS::InputControlSystem::MouseWheelClick::DOWN;

        for (int i = 0; i < A_Last; ++i)
        {
            ICS::Control* control;
            bool controlExists = mInputBinder->getChannel(i)->getControlsCount() != 0;
            if (!controlExists)
            {
                control = new ICS::Control(std::to_string(i), false, true, 0, ICS::ICS_MAX, ICS::ICS_MAX);
                mInputBinder->addControl(control);
                control->attachChannel(mInputBinder->getChannel(i), ICS::Channel::DIRECT);
            }
            else
            {
                control = mInputBinder->getChannel(i)->getAttachedControls().front().control;
            }

            if (!controlExists || force
                || (mInputBinder->getKeyBinding(control, ICS::Control::INCREASE) == SDL_SCANCODE_UNKNOWN
                    && mInputBinder->getMouseButtonBinding(control, ICS::Control::INCREASE) == ICS_MAX_DEVICE_BUTTONS
                    && mInputBinder->getMouseWheelBinding(control, ICS::Control::INCREASE)
                        == ICS::InputControlSystem::MouseWheelClick::UNASSIGNED))
            {
                clearAllKeyBindings(mInputBinder.get(), control);

                if (defaultKeyBindings.find(i) != defaultKeyBindings.end()
                    && (force || !mInputBinder->isKeyBound(defaultKeyBindings[i])))
                {
                    control->setInitialValue(0.0f);
                    mInputBinder->addKeyBinding(control, defaultKeyBindings[i], ICS::Control::INCREASE);
                }
                if (defaultMouseButtonBindings.find(i) != defaultMouseButtonBindings.end()
                    && (force || !mInputBinder->isMouseButtonBound(defaultMouseButtonBindings[i])))
                {
                    control->setInitialValue(0.0f);
                    mInputBinder->addMouseButtonBinding(control, defaultMouseButtonBindings[i], ICS::Control::INCREASE);
                }
                else if (defaultMouseWheelBindings.find(i) != defaultMouseWheelBindings.end()
                    && (force || !mInputBinder->isMouseWheelBound(defaultMouseWheelBindings[i])))
                {
                    control->setInitialValue(0.f);
                    mInputBinder->addMouseWheelBinding(control, defaultMouseWheelBindings[i], ICS::Control::INCREASE);
                }

                if (i == A_LookLeftRight && !mInputBinder->isKeyBound(SDL_SCANCODE_KP_4)
                    && !mInputBinder->isKeyBound(SDL_SCANCODE_KP_6))
                {
                    mInputBinder->addKeyBinding(control, SDL_SCANCODE_KP_6, ICS::Control::INCREASE);
                    mInputBinder->addKeyBinding(control, SDL_SCANCODE_KP_4, ICS::Control::DECREASE);
                }
                if (i == A_LookUpDown && !mInputBinder->isKeyBound(SDL_SCANCODE_KP_8)
                    && !mInputBinder->isKeyBound(SDL_SCANCODE_KP_2))
                {
                    mInputBinder->addKeyBinding(control, SDL_SCANCODE_KP_2, ICS::Control::INCREASE);
                    mInputBinder->addKeyBinding(control, SDL_SCANCODE_KP_8, ICS::Control::DECREASE);
                }
            }
        }
    }

    void BindingsManager::loadControllerDefaults(bool force)
    {
        // using hardcoded key defaults is inevitable, if we want the configuration files to stay valid
        // across different versions of OpenMW (in the case where another input action is added)
        std::map<int, int> defaultButtonBindings;

        defaultButtonBindings[A_Activate] = SDL_CONTROLLER_BUTTON_A;
        defaultButtonBindings[A_ToggleWeapon] = SDL_CONTROLLER_BUTTON_X;
        defaultButtonBindings[A_ToggleSpell] = SDL_CONTROLLER_BUTTON_Y;
        // defaultButtonBindings[A_QuickButtonsMenu] = SDL_GetButtonFromScancode(SDL_SCANCODE_F1); // Need to implement,
        // should be ToggleSpell(5) AND Wait(9)
        defaultButtonBindings[A_Sneak] = SDL_CONTROLLER_BUTTON_LEFTSTICK;
        defaultButtonBindings[A_Journal] = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
        defaultButtonBindings[A_Rest] = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
        defaultButtonBindings[A_TogglePOV] = SDL_CONTROLLER_BUTTON_RIGHTSTICK;
        defaultButtonBindings[A_Inventory] = SDL_CONTROLLER_BUTTON_B;
        defaultButtonBindings[A_GameMenu] = SDL_CONTROLLER_BUTTON_START;
        defaultButtonBindings[A_QuickSave] = SDL_CONTROLLER_BUTTON_GUIDE;
        // These slots are free in the existing Morrowind controller layout.
        // All combat actions remain configurable in the Controls menu.
        defaultButtonBindings[A_HeavyAttack] = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
        defaultButtonBindings[A_Dodge] = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
        defaultButtonBindings[A_Block] = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
        defaultButtonBindings[A_LockOn] = SDL_CONTROLLER_BUTTON_BACK;
        defaultButtonBindings[A_CriticalAttack] = SDL_CONTROLLER_BUTTON_DPAD_UP;

        std::map<int, int> defaultAxisBindings;
        defaultAxisBindings[A_MoveForwardBackward] = SDL_CONTROLLER_AXIS_LEFTY;
        defaultAxisBindings[A_MoveLeftRight] = SDL_CONTROLLER_AXIS_LEFTX;
        defaultAxisBindings[A_LookUpDown] = SDL_CONTROLLER_AXIS_RIGHTY;
        defaultAxisBindings[A_LookLeftRight] = SDL_CONTROLLER_AXIS_RIGHTX;
        defaultAxisBindings[A_Use] = SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
        defaultAxisBindings[A_Jump] = SDL_CONTROLLER_AXIS_TRIGGERLEFT;

        if (Settings::game().mEldenRingCombat)
        {
            defaultButtonBindings[A_Activate] = SDL_CONTROLLER_BUTTON_Y;
            defaultButtonBindings[A_Use] = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
            defaultButtonBindings[A_Block] = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
            defaultButtonBindings[A_Dodge] = SDL_CONTROLLER_BUTTON_B;
            defaultButtonBindings[A_Jump] = SDL_CONTROLLER_BUTTON_A;
            defaultButtonBindings[A_LockOn] = SDL_CONTROLLER_BUTTON_RIGHTSTICK;
            defaultButtonBindings[A_CycleWeaponRight] = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
            defaultButtonBindings[A_CycleOffhand] = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
            defaultButtonBindings[A_Inventory] = SDL_CONTROLLER_BUTTON_BACK;
            defaultButtonBindings[A_ToggleSpell] = SDL_CONTROLLER_BUTTON_DPAD_UP;
            defaultButtonBindings[A_Journal] = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
            defaultButtonBindings.erase(A_Rest);
            defaultButtonBindings.erase(A_TogglePOV);
            defaultButtonBindings.erase(A_HeavyAttack);
            defaultButtonBindings.erase(A_CriticalAttack);
            defaultAxisBindings.erase(A_Use);
            defaultAxisBindings.erase(A_Jump);
            defaultAxisBindings[A_HeavyAttack] = SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
        }

        for (int i = 0; i < A_Last; i++)
        {
            ICS::Control* control;
            bool controlExists = mInputBinder->getChannel(i)->getControlsCount() != 0;
            if (!controlExists)
            {
                float initial;
                if (defaultAxisBindings.find(i) == defaultAxisBindings.end())
                    initial = 0.0f;
                else
                    initial = 0.5f;
                control = new ICS::Control(std::to_string(i), false, true, initial, ICS::ICS_MAX, ICS::ICS_MAX);
                mInputBinder->addControl(control);
                control->attachChannel(mInputBinder->getChannel(i), ICS::Channel::DIRECT);
            }
            else
            {
                control = mInputBinder->getChannel(i)->getAttachedControls().front().control;
            }

            if (!controlExists || force
                || (mInputBinder->getJoystickAxisBinding(control, sFakeDeviceId, ICS::Control::INCREASE)
                        == ICS::InputControlSystem::UNASSIGNED
                    && mInputBinder->getJoystickButtonBinding(control, sFakeDeviceId, ICS::Control::INCREASE)
                        == ICS_MAX_DEVICE_BUTTONS))
            {
                clearAllControllerBindings(mInputBinder.get(), control);

                if (defaultButtonBindings.find(i) != defaultButtonBindings.end()
                    && (force || !mInputBinder->isJoystickButtonBound(sFakeDeviceId, defaultButtonBindings[i])))
                {
                    control->setInitialValue(0.0f);
                    mInputBinder->addJoystickButtonBinding(
                        control, sFakeDeviceId, defaultButtonBindings[i], ICS::Control::INCREASE);
                }
                else if (defaultAxisBindings.find(i) != defaultAxisBindings.end()
                    && (force || !mInputBinder->isJoystickAxisBound(sFakeDeviceId, defaultAxisBindings[i])))
                {
                    control->setValue(0.5f);
                    control->setInitialValue(0.5f);
                    mInputBinder->addJoystickAxisBinding(
                        control, sFakeDeviceId, defaultAxisBindings[i], ICS::Control::INCREASE);
                }
            }
        }
    }

    std::string_view BindingsManager::getActionDescription(int action)
    {
        switch (action)
        {
            case A_Screenshot:
                return "#{OMWEngine:Screenshot}";
            case A_ZoomIn:
                return "#{OMWEngine:CameraZoomIn}";
            case A_ZoomOut:
                return "#{OMWEngine:CameraZoomOut}";
            case A_ToggleHUD:
                return "#{OMWEngine:ToggleHUD}";
            case A_Use:
                return "#{sUse}";
            case A_Activate:
                return Settings::game().mEldenRingCombat ? "Interact / grip modifier" : "#{sActivate}";
            case A_MoveBackward:
                return "#{sBack}";
            case A_MoveForward:
                return "#{sForward}";
            case A_MoveLeft:
                return "#{sLeft}";
            case A_MoveRight:
                return "#{sRight}";
            case A_ToggleWeapon:
                return "#{sReady_Weapon}";
            case A_ToggleSpell:
                return "#{sReady_Magic}";
            case A_CycleSpellLeft:
                return "#{sPrevSpell}";
            case A_CycleSpellRight:
                return "#{sNextSpell}";
            case A_CycleWeaponLeft:
                return "#{sPrevWeapon}";
            case A_CycleWeaponRight:
                return "#{sNextWeapon}";
            case A_Console:
                return "#{OMWEngine:ConsoleWindow}";
            case A_Run:
                return Settings::game().mEldenRingCombat ? "Walk (hold)" : "#{sRun}";
            case A_Sneak:
                return "#{sCrouch_Sneak}";
            case A_AutoMove:
                return "#{sAuto_Run}";
            case A_Jump:
                return "#{sJump}";
            case A_Journal:
                return "#{sJournal}";
            case A_Rest:
                return "#{sRestKey}";
            case A_Inventory:
                return "#{sInventory}";
            case A_TogglePOV:
                return "#{sTogglePOVCmd}";
            case A_QuickKeysMenu:
                return "#{sQuickMenu}";
            case A_QuickKey1:
                return "#{sQuick1Cmd}";
            case A_QuickKey2:
                return "#{sQuick2Cmd}";
            case A_QuickKey3:
                return "#{sQuick3Cmd}";
            case A_QuickKey4:
                return "#{sQuick4Cmd}";
            case A_QuickKey5:
                return "#{sQuick5Cmd}";
            case A_QuickKey6:
                return "#{sQuick6Cmd}";
            case A_QuickKey7:
                return "#{sQuick7Cmd}";
            case A_QuickKey8:
                return "#{sQuick8Cmd}";
            case A_QuickKey9:
                return "#{sQuick9Cmd}";
            case A_QuickKey10:
                return "#{sQuick10Cmd}";
            case A_AlwaysRun:
                return "#{sAlways_Run}";
            case A_QuickSave:
                return "#{sQuickSaveCmd}";
            case A_QuickLoad:
                return "#{sQuickLoadCmd}";
            case A_TogglePostProcessorHUD:
                return "#{OMWEngine:TogglePostProcessorHUD}";
            case A_HeavyAttack:
                return "Heavy attack / guard counter";
            case A_Dodge:
                return "Dodge / hold to sprint";
            case A_Block:
                return "Guard / off-hand attack";
            case A_LockOn:
                return "Lock on";
            case A_CriticalAttack:
                return "Critical attack";
            case A_TwoHand:
                return "Toggle right two-handed grip";
            case A_TwoHandLeft:
                return "Toggle left two-handed grip";
            case A_HeavyModifier:
                return "Heavy attack modifier (hold with attack)";
            case A_CycleOffhand:
                return "Cycle off-hand armament";
            default:
                return {}; // not configurable
        }
    }

    std::string BindingsManager::getActionKeyBindingName(int action)
    {
        if (mInputBinder->getChannel(action)->getControlsCount() == 0)
            return "#{Interface:None}";

        ICS::Control* c = mInputBinder->getChannel(action)->getAttachedControls().front().control;

        SDL_Scancode key = mInputBinder->getKeyBinding(c, ICS::Control::INCREASE);
        unsigned int mouse = mInputBinder->getMouseButtonBinding(c, ICS::Control::INCREASE);
        ICS::InputControlSystem::MouseWheelClick wheel = mInputBinder->getMouseWheelBinding(c, ICS::Control::INCREASE);
        if (key != SDL_SCANCODE_UNKNOWN)
        {
            std::string name = MyGUI::TextIterator::toTagsString(mInputBinder->scancodeToString(key));
            if (mouse != ICS_MAX_DEVICE_BUTTONS)
                name += " / #{sMouse} " + std::to_string(mouse);
            return name;
        }
        else if (mouse != ICS_MAX_DEVICE_BUTTONS)
            return "#{sMouse} " + std::to_string(mouse);
        else if (wheel != ICS::InputControlSystem::MouseWheelClick::UNASSIGNED)
            switch (wheel)
            {
                case ICS::InputControlSystem::MouseWheelClick::UP:
                    return "Mouse Wheel Up";
                case ICS::InputControlSystem::MouseWheelClick::DOWN:
                    return "Mouse Wheel Down";
                case ICS::InputControlSystem::MouseWheelClick::RIGHT:
                    return "Mouse Wheel Right";
                case ICS::InputControlSystem::MouseWheelClick::LEFT:
                    return "Mouse Wheel Left";
                default:
                    return "#{Interface:None}";
            }
        else
            return "#{Interface:None}";
    }

    std::string BindingsManager::getActionControllerBindingName(int action)
    {
        if (mInputBinder->getChannel(action)->getControlsCount() == 0)
            return "#{Interface:None}";

        ICS::Control* c = mInputBinder->getChannel(action)->getAttachedControls().front().control;

        if (mInputBinder->getJoystickAxisBinding(c, sFakeDeviceId, ICS::Control::INCREASE)
            != ICS::InputControlSystem::UNASSIGNED)
            return SDLUtil::sdlControllerAxisToString(
                mInputBinder->getJoystickAxisBinding(c, sFakeDeviceId, ICS::Control::INCREASE));
        else if (mInputBinder->getJoystickButtonBinding(c, sFakeDeviceId, ICS::Control::INCREASE)
            != ICS_MAX_DEVICE_BUTTONS)
            return SDLUtil::sdlControllerButtonToString(
                mInputBinder->getJoystickButtonBinding(c, sFakeDeviceId, ICS::Control::INCREASE));
        else
            return "#{Interface:None}";
    }

    const std::initializer_list<int>& BindingsManager::getActionKeySorting()
    {
        static const std::initializer_list<int> actions{ A_MoveForward, A_MoveBackward, A_MoveLeft, A_MoveRight,
            A_TogglePOV, A_ZoomIn, A_ZoomOut, A_Run, A_AlwaysRun, A_Sneak, A_Activate, A_Use, A_ToggleWeapon,
            A_ToggleSpell, A_CycleSpellLeft, A_CycleSpellRight, A_CycleWeaponLeft, A_CycleWeaponRight, A_AutoMove,
            A_Jump, A_Inventory, A_Journal, A_Rest, A_Console, A_QuickSave, A_QuickLoad, A_ToggleHUD, A_Screenshot,
            A_QuickKeysMenu, A_QuickKey1, A_QuickKey2, A_QuickKey3, A_QuickKey4, A_QuickKey5, A_QuickKey6, A_QuickKey7,
            A_QuickKey8, A_QuickKey9, A_QuickKey10, A_TogglePostProcessorHUD, A_HeavyAttack, A_Dodge, A_Block,
            A_LockOn, A_CriticalAttack, A_TwoHand, A_CycleOffhand, A_HeavyModifier, A_TwoHandLeft };

        return actions;
    }
    const std::initializer_list<int>& BindingsManager::getActionControllerSorting()
    {
        static const std::initializer_list<int> actions{ A_MoveForward, A_MoveBackward, A_MoveLeft, A_MoveRight,
            A_TogglePOV, A_ZoomIn, A_ZoomOut, A_Sneak, A_Activate, A_Use, A_ToggleWeapon, A_ToggleSpell, A_AutoMove,
            A_Jump, A_Inventory, A_Journal, A_Rest, A_QuickSave, A_QuickLoad, A_ToggleHUD, A_Screenshot,
            A_QuickKeysMenu, A_QuickKey1, A_QuickKey2, A_QuickKey3, A_QuickKey4, A_QuickKey5, A_QuickKey6, A_QuickKey7,
            A_QuickKey8, A_QuickKey9, A_QuickKey10, A_CycleSpellLeft, A_CycleSpellRight, A_CycleWeaponLeft,
            A_CycleWeaponRight, A_HeavyAttack, A_Dodge, A_Block, A_LockOn, A_CriticalAttack, A_TwoHand,
            A_CycleOffhand, A_TwoHandLeft };

        return actions;
    }

    void BindingsManager::enableDetectingBindingMode(int action, bool keyboard)
    {
        mListener->setDetectingKeyboard(keyboard);
        ICS::Control* c = mInputBinder->getChannel(action)->getAttachedControls().front().control;
        mInputBinder->enableDetectingBindingState(c, ICS::Control::INCREASE);
    }

    bool BindingsManager::isDetectingBindingState() const
    {
        return mInputBinder->detectingBindingState();
    }

    bool BindingsManager::eldenInputEnabled() const
    {
        const auto& environment = MWBase::Environment::get();
        if (!Settings::game().mEldenRingCombat
            || environment.getStateManager()->getState() != MWBase::StateManager::State_Running)
            return false;
        const auto manager = environment.getInputManager();
        return !manager->controlsDisabled() && manager->getControlSwitch("playercontrols")
            && !environment.getWindowManager()->isGuiMode()
            && manager->getCursorMode() == MWBase::CursorMode::Locked;
    }

    bool BindingsManager::captureEldenAction(int action, bool down, EldenInput::Source source)
    {
        if (isDetectingBindingState() || !eldenInputEnabled())
            return false;
        const auto& environment = MWBase::Environment::get();
        const auto player = environment.getWorld()->getPlayerPtr();
        const bool melee = MWMechanics::useEldenMelee(player)
            && environment.getInputManager()->getControlSwitch("playerfighting");
        const bool wasHeld = mEldenInput.sourceHeld(action);
        mCapturedActions.at(action) = true;
        mEldenInput.set(action, down, melee, source);
        if (down && !wasHeld && action != A_Activate)
            environment.getInputManager()->executeAction(action);
        return true;
    }

    void BindingsManager::mousePressed(const SDL_MouseButtonEvent& arg, Uint8 deviceID)
    {
        if (!isDetectingBindingState())
            for (int action = 0; action < A_Last; ++action)
                if (mInputBinder->getMouseButtonBinding(mInputBinder->getControl(action), ICS::Control::INCREASE)
                    == deviceID)
                    captureEldenAction(action, true, EldenInput::Source::Mouse);
        mInputBinder->mousePressed(arg, deviceID);
    }

    void BindingsManager::mouseReleased(const SDL_MouseButtonEvent& arg, Uint8 deviceID)
    {
        if (!isDetectingBindingState())
            for (int action = 0; action < A_Last; ++action)
                if (mInputBinder->getMouseButtonBinding(mInputBinder->getControl(action), ICS::Control::INCREASE)
                    == deviceID)
                    captureEldenAction(action, false, EldenInput::Source::Mouse);
        mInputBinder->mouseReleased(arg, deviceID);
    }

    void BindingsManager::mouseMoved(const SDLUtil::MouseMotionEvent& arg)
    {
        mInputBinder->mouseMoved(arg);
    }

    void BindingsManager::mouseWheelMoved(const SDL_MouseWheelEvent& arg)
    {
        if (!isDetectingBindingState())
        {
            using Click = ICS::InputControlSystem::MouseWheelClick;
            // Use the same vertical precedence and flipped direction as ICS.
            const int value = arg.y != 0 ? arg.y : arg.x;
            const bool positive = (value > 0) != (arg.direction == SDL_MOUSEWHEEL_FLIPPED);
            const Click click = value == 0 ? Click::UNASSIGNED
                : arg.y != 0 ? (positive ? Click::UP : Click::DOWN)
                             : (positive ? Click::RIGHT : Click::LEFT);
            if (click != Click::UNASSIGNED)
                for (int action = 0; action < A_Last; ++action)
                    if (mInputBinder->getMouseWheelBinding(mInputBinder->getControl(action), ICS::Control::INCREASE)
                        == click)
                    {
                        captureEldenAction(action, true, EldenInput::Source::MouseWheel);
                        captureEldenAction(action, false, EldenInput::Source::MouseWheel);
                    }
        }
        mInputBinder->mouseWheelMoved(arg);
    }

    void BindingsManager::keyPressed(const SDL_KeyboardEvent& arg)
    {
        if (!isDetectingBindingState())
            for (int action = 0; action < A_Last; ++action)
            {
                const SDL_Scancode key = getKeyBinding(action);
                const bool rightShift = action == A_HeavyModifier && key == SDL_SCANCODE_LSHIFT
                    && arg.keysym.scancode == SDL_SCANCODE_RSHIFT;
                if (key == arg.keysym.scancode || rightShift)
                    captureEldenAction(action, true,
                        rightShift ? EldenInput::Source::AlternateShift : EldenInput::Source::Keyboard);
            }
        mInputBinder->keyPressed(arg);
    }

    void BindingsManager::keyReleased(const SDL_KeyboardEvent& arg)
    {
        if (!isDetectingBindingState())
            for (int action = 0; action < A_Last; ++action)
            {
                const SDL_Scancode key = getKeyBinding(action);
                const bool rightShift = action == A_HeavyModifier && key == SDL_SCANCODE_LSHIFT
                    && arg.keysym.scancode == SDL_SCANCODE_RSHIFT;
                if (key == arg.keysym.scancode || rightShift)
                    captureEldenAction(action, false,
                        rightShift ? EldenInput::Source::AlternateShift : EldenInput::Source::Keyboard);
            }
        mInputBinder->keyReleased(arg);
    }

    void BindingsManager::controllerAdded(int deviceID, const SDL_ControllerDeviceEvent& arg)
    {
        mInputBinder->controllerAdded(deviceID, arg);
    }

    void BindingsManager::controllerRemoved(const SDL_ControllerDeviceEvent& arg)
    {
        mEldenInput.release(EldenInput::Source::ControllerButton);
        mEldenInput.release(EldenInput::Source::ControllerAxis);
        mInputBinder->controllerRemoved(arg);
    }

    void BindingsManager::controllerButtonPressed(int deviceID, const SDL_ControllerButtonEvent& arg)
    {
        if (!isDetectingBindingState())
            for (int action = 0; action < A_Last; ++action)
                if (mInputBinder->getJoystickButtonBinding(
                        mInputBinder->getControl(action), deviceID, ICS::Control::INCREASE) == arg.button)
                    captureEldenAction(action, true, EldenInput::Source::ControllerButton);
        mInputBinder->buttonPressed(deviceID, arg);
    }

    void BindingsManager::controllerButtonReleased(int deviceID, const SDL_ControllerButtonEvent& arg)
    {
        if (!isDetectingBindingState())
            for (int action = 0; action < A_Last; ++action)
                if (mInputBinder->getJoystickButtonBinding(
                        mInputBinder->getControl(action), deviceID, ICS::Control::INCREASE) == arg.button)
                    captureEldenAction(action, false, EldenInput::Source::ControllerButton);
        mInputBinder->buttonReleased(deviceID, arg);
    }

    void BindingsManager::controllerAxisMoved(int deviceID, const SDL_ControllerAxisEvent& arg)
    {
        std::vector<int> captured;
        if (!isDetectingBindingState() && eldenInputEnabled())
            for (int action = 0; action < A_Last; ++action)
                if (action != A_MoveForwardBackward && action != A_MoveLeftRight
                    && action != A_LookUpDown && action != A_LookLeftRight
                    && mInputBinder->getJoystickAxisBinding(
                           mInputBinder->getControl(action), deviceID, ICS::Control::INCREASE) == arg.axis)
                {
                    mCapturedActions.at(action) = true;
                    captured.push_back(action);
                }
        mInputBinder->axisMoved(deviceID, arg);
        for (int action : captured)
            captureEldenAction(action, mInputBinder->getChannel(action)->getValue() > 0.6f,
                EldenInput::Source::ControllerAxis);
    }

    SDL_Scancode BindingsManager::getKeyBinding(int actionId)
    {
        return mInputBinder->getKeyBinding(mInputBinder->getControl(actionId), ICS::Control::INCREASE);
    }

    SDL_GameController* BindingsManager::getControllerOrNull() const
    {
        const auto& controllers = mInputBinder->getJoystickInstanceMap();
        if (controllers.empty())
            return nullptr;
        else
            return controllers.begin()->second;
    }

    void BindingsManager::actionValueChanged(int action, float currentValue, float previousValue)
    {
        auto manager = MWBase::Environment::get().getInputManager();
        manager->resetIdleTime();

        if (mDragDrop && action != A_GameMenu && action != A_Inventory)
            return;

        if (mCapturedActions.at(action))
            return;

        if (!Settings::game().mEldenRingCombat && manager->joystickLastUsed()
            && manager->getControlSwitch("playercontrols"))
        {
            if (action == A_Use && actionIsActive(A_ToggleWeapon))
                action = A_CycleWeaponRight;
            else if (action == A_Use && actionIsActive(A_ToggleSpell))
                action = A_CycleSpellRight;
            else if (action == A_Jump && actionIsActive(A_ToggleWeapon))
                action = A_CycleWeaponLeft;
            else if (action == A_Jump && actionIsActive(A_ToggleSpell))
                action = A_CycleSpellLeft;
        }

        if (previousValue <= 0.6 && currentValue > 0.6)
            manager->executeAction(action);
    }

    void BindingsManager::saveBindings()
    {
        std::string newFileName;
        try
        {
            newFileName = Files::pathToUnicodeString(mUserFile) + ".new";
            if (mInputBinder->save(newFileName))
            {
                std::filesystem::rename(Files::pathFromUnicodeString(newFileName), mUserFile);
                Log(Debug::Info) << "Saved input bindings: " << mUserFile;
            }
            else
            {
                Log(Debug::Error) << "Failed to save input bindings to " << newFileName;
            }
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "Failed to save input bindings to " << newFileName << ": " << e.what();
        }
    }
}
