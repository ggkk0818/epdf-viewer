#pragma once

#include "Page.h"

namespace ui {

class BlePage : public Page {
public:
    void onEnter(::app::AppController& app) override;
    void onEvent(::app::InputEvent e, ::app::AppController& app) override;
    void render(modules::DisplayModule& dm, UiCommon& ui) override;

private:
    bool bleOn_ = false;
};

} // namespace ui
