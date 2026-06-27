#include "SettingsPage.h"
#include "../app/AppController.h"
#include "../config/Config.h"

namespace ui {

void SettingsPage::onEvent(::app::InputEvent e, ::app::AppController& app) {
    if (e == ::app::InputEvent::Back) {
        app.popPage();
    }
}

void SettingsPage::render(modules::DisplayModule& dm, UiCommon& ui) {
    ui.drawStatusBar(false, 0, 0);

    String pct = String(ui.getBatteryPercent()) + "%";
    ui.drawListRow(0,  "电量",     pct, false);
    ui.drawListRow(28, "系统版本", cfg::version::SW_VERSION, false);
}

} // namespace ui
