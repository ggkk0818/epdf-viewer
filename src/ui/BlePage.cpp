#include "BlePage.h"
#include "../app/AppController.h"

namespace ui {

void BlePage::onEnter(::app::AppController& app) {
    bleOn_ = app.ble().isEnabled();
}

void BlePage::onEvent(::app::InputEvent e, ::app::AppController& app) {
    switch (e) {
        case ::app::InputEvent::Enter:
            bleOn_ = !bleOn_;
            app.ble().setEnabled(bleOn_);
            app.renderCurrent(modules::RefreshMode::Full);
            break;
        case ::app::InputEvent::Back:
            app.popPage();
            break;
        default: break;
    }
}

void BlePage::render(modules::DisplayModule& dm, UiCommon& ui) {
    ui.drawStatusBar(false, 0, 0);
    ui.drawSwitchRow(0, "蓝牙开关", bleOn_, true);
}

} // namespace ui
