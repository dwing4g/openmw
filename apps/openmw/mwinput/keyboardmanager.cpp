#include "keyboardmanager.hpp"

#include <cctype>

#include <MyGUI_InputManager.h>

#include <components/sdlutil/sdlmappings.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/windowmanager.hpp"

#include "actions.hpp"
#include "bindingsmanager.hpp"

namespace MWInput
{
    KeyboardManager::KeyboardManager(BindingsManager* bindingsManager)
        : mBindingsManager(bindingsManager)
    {
    }

    void KeyboardManager::textInput(const SDL_TextInputEvent& arg)
    {
        MyGUI::UString ustring(&arg.text[0]);
        MyGUI::UString::utf32string utf32string = ustring.asUTF32();
        for (MyGUI::UString::utf32string::const_iterator it = utf32string.begin(); it != utf32string.end(); ++it)
            MyGUI::InputManager::getInstance().injectKeyPress(MyGUI::KeyCode::None, *it);
    }

    void KeyboardManager::keyPressed(const SDL_KeyboardEvent& arg)
    {
        const auto windowManager = MWBase::Environment::get().getWindowManager();
        const auto input = MWBase::Environment::get().getInputManager();
        // HACK: to make default keybinding for the console work without printing an extra "^" upon closing
        // This assumes that SDL_TextInput events always come *after* the key event
        // (which is somewhat reasonable, and hopefully true for all SDL platforms)
        auto kc = SDLUtil::sdlKeyToMyGUI(arg.keysym.sym);
        if (mBindingsManager->getKeyBinding(A_Console) == arg.keysym.scancode
            // HACK: allow upper case variant of console keybinding.
            && (arg.keysym.mod & KMOD_SHIFT) == 0 && windowManager->isConsoleMode())
            SDL_StopTextInput();

        bool consumed = SDL_IsTextInputActive() && // Little trick to check if key is printable
            (!(SDLK_SCANCODE_MASK & arg.keysym.sym) &&
                // Don't trust isprint for symbols outside the extended ASCII range
                ((kc == MyGUI::KeyCode::None && arg.keysym.sym > 0xff)
                    || (arg.keysym.sym >= 0 && arg.keysym.sym <= 255 && std::isprint(arg.keysym.sym))));
        // Inventory is a toggle in both the player and container windows. Give
        // its current binding priority over GUI focus navigation (notably Tab),
        // then use the normal binding/Lua trigger so overrides still apply.
        // Printable rebound keys remain available to text fields, and modal or
        // unrelated menus retain their own keyboard handling.
        const bool inventoryShortcut = mBindingsManager->getKeyBinding(A_Inventory) == arg.keysym.scancode
            && !mBindingsManager->isDetectingBindingState() && !input->controlsDisabled() && !consumed
            && !MyGUI::InputManager::getInstance().isModalAny() && !windowManager->isConsoleMode()
            && !windowManager->isPostProcessorHudVisible()
            && (!windowManager->isGuiMode() || windowManager->getMode() == MWGui::GM_Inventory
                || windowManager->getMode() == MWGui::GM_Container);
        if (inventoryShortcut)
            mBindingsManager->setPlayerControlsEnabled(!windowManager->isGuiMode());
        else if (kc != MyGUI::KeyCode::None && !mBindingsManager->isDetectingBindingState())
        {
            if (windowManager->injectKeyPress(kc, 0, arg.repeat))
                consumed = true;
            mBindingsManager->setPlayerControlsEnabled(!consumed);
        }

        if (arg.repeat)
            return;

        if (!input->controlsDisabled() && !consumed)
            mBindingsManager->keyPressed(arg);

        if (!consumed)
        {
            MWBase::Environment::get().getLuaManager()->inputEvent(
                { MWBase::LuaManager::InputEvent::KeyPressed, arg.keysym });
        }

        input->setJoystickLastUsed(false);
    }

    void KeyboardManager::keyReleased(const SDL_KeyboardEvent& arg)
    {
        MWBase::Environment::get().getInputManager()->setJoystickLastUsed(false);
        auto kc = SDLUtil::sdlKeyToMyGUI(arg.keysym.sym);

        if (!mBindingsManager->isDetectingBindingState())
            mBindingsManager->setPlayerControlsEnabled(!MyGUI::InputManager::getInstance().injectKeyRelease(kc));
        mBindingsManager->keyReleased(arg);
        MWBase::Environment::get().getLuaManager()->inputEvent(
            { MWBase::LuaManager::InputEvent::KeyReleased, arg.keysym });
    }
}
