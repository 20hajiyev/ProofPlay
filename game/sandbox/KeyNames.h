#pragma once
#include "WickedEngine.h"

#include <string>

// Key names used by Settings::bindings (racer::IsBindableKey) <-> Wicked input buttons.
inline wi::input::BUTTON KeyButton(const std::string& name)
{
    using namespace wi::input;
    if (name.size() == 1)
        return static_cast<BUTTON>(name[0]); // A-Z, 0-9 are their ASCII codes in wiInput
    if (name == "Space") return KEYBOARD_BUTTON_SPACE;
    if (name == "LShift") return KEYBOARD_BUTTON_LSHIFT;
    if (name == "RShift") return KEYBOARD_BUTTON_RSHIFT;
    if (name == "LCtrl") return KEYBOARD_BUTTON_LCONTROL;
    if (name == "RCtrl") return KEYBOARD_BUTTON_RCONTROL;
    if (name == "Up") return KEYBOARD_BUTTON_UP;
    if (name == "Down") return KEYBOARD_BUTTON_DOWN;
    if (name == "Left") return KEYBOARD_BUTTON_LEFT;
    if (name == "Right") return KEYBOARD_BUTTON_RIGHT;
    if (name == "Tab") return KEYBOARD_BUTTON_TAB;
    if (name == "Enter") return KEYBOARD_BUTTON_ENTER;
    return BUTTON_NONE;
}

// The first bindable key pressed this frame, or "" (remap screen listening for a key).
inline std::string PressedKeyName()
{
    using namespace wi::input;
    for (char c = 'A'; c <= 'Z'; ++c)
        if (Press(static_cast<BUTTON>(c)))
            return std::string(1, c);
    for (char c = '0'; c <= '9'; ++c)
        if (Press(static_cast<BUTTON>(c)))
            return std::string(1, c);
    for (const char* n : { "Space", "LShift", "RShift", "LCtrl", "RCtrl", "Up", "Down", "Left", "Right", "Tab" })
        if (Press(KeyButton(n)))
            return n;
    return {};
}
