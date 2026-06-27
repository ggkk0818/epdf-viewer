#pragma once

#include "Page.h"

namespace ui {

class SettingsPage : public Page {
public:
    void onEvent(::app::InputEvent e, ::app::AppController& app) override;
    void render(modules::DisplayModule& dm, UiCommon& ui) override;
};

} // namespace ui
