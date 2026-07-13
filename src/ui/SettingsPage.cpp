#include "SettingsPage.h"
#include "../app/AppController.h"
#include "../config/Config.h"

namespace ui {

namespace {

constexpr uint64_t MB = 1024ULL * 1024ULL;

String formatStorage(uint64_t used, uint64_t total) {
    uint32_t usedMB  = (uint32_t)(used  / MB);
    uint32_t totalMB = (uint32_t)(total / MB);
    char buf[40];
    snprintf(buf, sizeof(buf), "已使用 %luMB/%luMB",
             (unsigned long)usedMB, (unsigned long)totalMB);
    return String(buf);
}

} // namespace

void SettingsPage::onEnter(::app::AppController& app) {
    uint64_t storageUsed = app.sd().usedBytes();
    uint64_t storageTotal = app.sd().totalBytes();
    app.mutateUiState([&] {
        storageUsed_  = storageUsed;
        storageTotal_ = storageTotal;
    });
}

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

    String storage = formatStorage(storageUsed_, storageTotal_);
    ui.drawListRow(56, "存储空间", storage, false);
}

} // namespace ui
