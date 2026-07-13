#include "BlePage.h"
#include "../app/AppController.h"

namespace ui {

void BlePage::onEnter(::app::AppController& app) {
    bool bleOn = app.ble().isEnabled();
    app.mutateUiState([&] {
        bleOn_    = bleOn;
        focusIdx_ = ROW_SWITCH;
    });
}

void BlePage::onEvent(::app::InputEvent e, ::app::AppController& app) {
    bool changed = false;
    bool applyBle = false;
    bool newBleOn = false;
    bool goBack = false;

    app.mutateUiState([&] {
        switch (e) {
            case ::app::InputEvent::UpLeft:
                focusIdx_ = (focusIdx_ + ROW_COUNT - 1) % ROW_COUNT;
                changed = true;
                break;
            case ::app::InputEvent::DownRight:
                focusIdx_ = (focusIdx_ + 1) % ROW_COUNT;
                changed = true;
                break;
            case ::app::InputEvent::Enter:
                if (focusIdx_ == ROW_SWITCH) {
                    bleOn_ = !bleOn_;
                    newBleOn = bleOn_;
                    applyBle = true;
                    changed = true;
                }
                break;
            case ::app::InputEvent::Back:
                goBack = true;
                break;
            default: break;
        }
    });

    if (applyBle) {
        app.ble().setEnabled(newBleOn);
    }
    if (goBack) {
        app.popPage();
        return;
    }
    if (changed) {
        app.requestRender();
    }
}

void BlePage::render(modules::DisplayModule& dm, UiCommon& ui) {
    ui.drawStatusBar(false, 0, 0);
    ui.drawSwitchRow(0, "蓝牙开关", bleOn_, focusIdx_ == ROW_SWITCH);
}

} // namespace ui
