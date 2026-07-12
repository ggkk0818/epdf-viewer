#pragma once

#include "Page.h"

namespace ui {

class BlePage : public Page {
public:
    void onEnter(::app::AppController& app) override;
    void onEvent(::app::InputEvent e, ::app::AppController& app) override;
    void render(modules::DisplayModule& dm, UiCommon& ui) override;

private:
    // Row indices into the BLE page list.
    static constexpr uint8_t ROW_SWITCH = 0;
    static constexpr uint8_t ROW_COUNT  = 1;

    bool    bleOn_   = false;
    uint8_t focusIdx_ = ROW_SWITCH;
};

} // namespace ui
