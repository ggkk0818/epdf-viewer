#pragma once

#include <cstring>

namespace app {

enum class InputEvent : uint8_t {
    None = 0,
    Enter,
    Back,
    UpLeft,
    DownRight,
};

inline const char* eventToStr(InputEvent e) {
    switch (e) {
        case InputEvent::Enter:     return "Enter";
        case InputEvent::Back:      return "Back";
        case InputEvent::UpLeft:    return "UpLeft";
        case InputEvent::DownRight: return "DownRight";
        default:                    return "None";
    }
}

inline InputEvent eventFromStr(const char* s) {
    if (!s) return InputEvent::None;
    if (strcmp(s, "enter") == 0)      return InputEvent::Enter;
    if (strcmp(s, "back") == 0)       return InputEvent::Back;
    if (strcmp(s, "up_left") == 0)    return InputEvent::UpLeft;
    if (strcmp(s, "down_right") == 0) return InputEvent::DownRight;
    return InputEvent::None;
}

} // namespace app
