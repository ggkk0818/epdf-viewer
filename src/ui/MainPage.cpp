#include "MainPage.h"
#include "../app/AppController.h"
#include "DocListPage.h"
#include "BlePage.h"
#include "SettingsPage.h"

namespace ui {

namespace {

struct MainItem {
    const char* label;
    const char* iconName;
};

constexpr MainItem ITEMS[MainPage::ITEM_COUNT] = {
    { "文档",   "doc"      },
    { "蓝牙",   "ble"      },
    { "设置",   "settings" },
};

} // namespace

void MainPage::onEvent(::app::InputEvent e, ::app::AppController& app) {
    bool changed = false;
    uint8_t selectedIdx = 0;
    bool openPage = false;

    app.display().lockState();
    switch (e) {
        case ::app::InputEvent::UpLeft:
            idx_ = (idx_ + ITEM_COUNT - 1) % ITEM_COUNT;
            changed = true;
            break;
        case ::app::InputEvent::DownRight:
            idx_ = (idx_ + 1) % ITEM_COUNT;
            changed = true;
            break;
        case ::app::InputEvent::Enter:
            selectedIdx = idx_;
            openPage = true;
            break;
        case ::app::InputEvent::Back:
            break;
        default: break;
    }
    app.display().unlockState();

    if (openPage) {
        switch (selectedIdx) {
            case 0: app.pushPage(new DocListPage());    return;
            case 1: app.pushPage(new BlePage());        return;
            case 2: app.pushPage(new SettingsPage());   return;
        }
    }
    if (changed) app.requestRender();
}

void MainPage::render(modules::DisplayModule& dm, UiCommon& ui) {
    ui.drawStatusBar(false, 0, 0);
    for (size_t i = 0; i < ITEM_COUNT; i++) {
        uint8_t col = i % cfg::display::GRID_COLS;
        uint8_t row = i / cfg::display::GRID_COLS;
        ui.drawGridItem(col, row, ITEMS[i].label, ITEMS[i].iconName, i == idx_);
    }
}

} // namespace ui
