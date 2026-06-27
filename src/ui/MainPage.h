#pragma once

#include "Page.h"

namespace ui {

class MainPage : public Page {
public:
    void onEnter(::app::AppController& /*app*/) override {}
    void onEvent(::app::InputEvent e, ::app::AppController& app) override;
    void render(modules::DisplayModule& dm, UiCommon& ui) override;

    static constexpr size_t ITEM_COUNT = 3;

private:
    uint8_t idx_ = 0;
};

} // namespace ui
