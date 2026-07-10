#include "BlePage.h"
#include "../app/AppController.h"
#include "ConfirmDialog.h"

namespace ui {

namespace {

void onRepairConfirm(::app::AppController& app, void* /*ctx*/) {
    app.ble().unpairAll();
}

} // namespace

void BlePage::onEnter(::app::AppController& app) {
    bleOn_    = app.ble().isEnabled();
    focusIdx_ = ROW_SWITCH;
}

void BlePage::onEvent(::app::InputEvent e, ::app::AppController& app) {
    switch (e) {
        case ::app::InputEvent::UpLeft:
            focusIdx_ = (focusIdx_ + ROW_COUNT - 1) % ROW_COUNT;
            app.requestRender();
            break;
        case ::app::InputEvent::DownRight:
            focusIdx_ = (focusIdx_ + 1) % ROW_COUNT;
            app.requestRender();
            break;
        case ::app::InputEvent::Enter:
            if (focusIdx_ == ROW_SWITCH) {
                bleOn_ = !bleOn_;
                app.ble().setEnabled(bleOn_);
                app.requestRender();
            } else {
                using CB = ConfirmDialog::ConfirmCallback;
                app.pushPage(new ConfirmDialog(
                    "重新配对",
                    "将清除已配对设备，确认继续？",
                    "取消",
                    "确认",
                    &onRepairConfirm,
                    nullptr));
            }
            break;
        case ::app::InputEvent::Back:
            app.popPage();
            break;
        default: break;
    }
}

void BlePage::render(modules::DisplayModule& dm, UiCommon& ui) {
    ui.drawStatusBar(false, 0, 0);
    ui.drawSwitchRow(0, "蓝牙开关", bleOn_, focusIdx_ == ROW_SWITCH);
    ui.drawListRow(28, "重新配对", "", focusIdx_ == ROW_REPAIR);
}

} // namespace ui
