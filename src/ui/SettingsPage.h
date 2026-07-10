#pragma once

#include <cstdint>
#include "Page.h"

namespace ui {

class SettingsPage : public Page {
public:
    void onEnter(::app::AppController& app) override;
    void onEvent(::app::InputEvent e, ::app::AppController& app) override;
    void render(modules::DisplayModule& dm, UiCommon& ui) override;

private:
    uint64_t storageUsed_  = 0;
    uint64_t storageTotal_ = 0;
};

} // namespace ui
