#pragma once

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

} // namespace app
