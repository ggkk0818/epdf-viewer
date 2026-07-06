#include "UiCommon.h"
#include "../modules/BatteryModule.h"
#include "../modules/BleModule.h"
#include "../modules/IconStore.h"
#include "../config/Config.h"

namespace ui {

namespace {

constexpr uint16_t CONTENT_W = cfg::display::WIDTH;
constexpr uint16_t CONTENT_TOP = cfg::display::CONTENT_Y;
constexpr uint16_t CONTENT_BOTTOM = cfg::display::HEIGHT;

constexpr uint16_t COLOR_BG = GxEPD_WHITE;
constexpr uint16_t COLOR_FG = GxEPD_BLACK;

constexpr uint8_t  LIST_ROW_H = 28;
constexpr uint8_t  LIST_TEXT_Y_OFFSET = 18;
constexpr uint8_t  LIST_PAD_X = 8;

} // namespace

void UiCommon::begin(modules::DisplayModule* dm,
                     modules::BatteryModule* bat,
                     modules::BleModule* ble,
                     modules::IconStore* icons) {
    dm_ = dm;
    bat_ = bat;
    ble_ = ble;
    icons_ = icons;
}

void UiCommon::drawStatusBar(bool showPage, uint16_t pageCur, uint16_t pageTotal) {
    if (!dm_) return;
    auto& g = dm_->gfx();
    auto& f = dm_->fonts();

    g.fillRect(0, 0, CONTENT_W, cfg::display::STATUS_H, COLOR_FG);
    g.fillRect(0, 0, CONTENT_W, cfg::display::STATUS_H - 1, COLOR_BG);

    f.setFont(u8g2_font_wqy12_t_chinese3);
    f.setForegroundColor(COLOR_FG);
    f.setBackgroundColor(COLOR_BG);

    if (showPage) {
        f.setCursor(4, 14);
        f.printf("%u/%u", (unsigned)pageCur, (unsigned)pageTotal);
    }

    uint16_t rightX = CONTENT_W - 4;

    if (bat_) {
        uint8_t pct = bat_->getPercent();
        char buf[8];
        snprintf(buf, sizeof(buf), "%u%%", pct);
        f.setCursor(rightX - 24, 14);
        f.print(buf);
        drawIconAt(rightX - 4, 2, "battery", 16, 16);
        rightX -= 40;
    }

    if (ble_) {
        const char* iconName = ble_->isEnabled() ? "ble_on" : "ble_off";
        drawIconAt(rightX - 16, 2, iconName, 16, 16);
    }
}

void UiCommon::drawListRow(uint16_t y, const String& title, const String& value, bool focused) {
    if (!dm_) return;
    auto& g = dm_->gfx();
    auto& f = dm_->fonts();

    uint16_t rowY = CONTENT_TOP + y;
    uint16_t rowH = LIST_ROW_H;

    if (focused) {
        g.fillRect(0, rowY, CONTENT_W, rowH, COLOR_FG);
        f.setForegroundColor(COLOR_BG);
        f.setBackgroundColor(COLOR_FG);
    } else {
        f.setForegroundColor(COLOR_FG);
        f.setBackgroundColor(COLOR_BG);
    }

    f.setFont(u8g2_font_wqy12_t_chinese3);
    f.setCursor(LIST_PAD_X, rowY + LIST_TEXT_Y_OFFSET - 4);
    f.print(title);

    if (value.length() > 0) {
        uint16_t valueW = value.length() * 7;
        f.setCursor(CONTENT_W - LIST_PAD_X - valueW, rowY + LIST_TEXT_Y_OFFSET - 4);
        f.print(value);
    }

    f.setForegroundColor(COLOR_FG);
    f.setBackgroundColor(COLOR_BG);
}

void UiCommon::drawGridItem(uint8_t col, uint8_t row, const String& label, const char* iconName, bool focused) {
    if (!dm_) return;
    auto& g = dm_->gfx();
    auto& f = dm_->fonts();

    uint16_t cellW = CONTENT_W / cfg::display::GRID_COLS;
    uint16_t cellH = (cfg::display::CONTENT_H) / cfg::display::GRID_ROWS;
    uint16_t cellX = col * cellW;
    uint16_t cellY = CONTENT_TOP + row * cellH;

    uint16_t iconSize = cfg::display::ICON_SIZE;
    uint16_t iconX = cellX + (cellW - iconSize) / 2;
    uint16_t iconY = cellY + cfg::display::GRID_PAD;

    if (focused) {
        g.drawRect(iconX - 2, iconY - 2, iconSize + 4, iconSize + 4, COLOR_FG);
        g.drawRect(iconX - 4, iconY - 4, iconSize + 8, iconSize + 8, COLOR_FG);
    }

    drawIconAt(iconX, iconY, iconName, iconSize, iconSize);

    f.setFont(u8g2_font_wqy12_t_chinese3);
    f.setForegroundColor(COLOR_FG);
    f.setBackgroundColor(COLOR_BG);
    uint16_t labelW = label.length() * 12;
    f.setCursor(cellX + (cellW - labelW) / 2, iconY + iconSize + 16);
    f.print(label);
}

void UiCommon::drawSwitchRow(uint16_t y, const String& label, bool on, bool focused) {
    if (!dm_) return;
    auto& g = dm_->gfx();
    auto& f = dm_->fonts();

    uint16_t rowY = CONTENT_TOP + y;
    uint16_t rowH = LIST_ROW_H;

    if (focused) {
        g.fillRect(0, rowY, CONTENT_W, rowH, COLOR_FG);
        f.setForegroundColor(COLOR_BG);
        f.setBackgroundColor(COLOR_FG);
    } else {
        f.setForegroundColor(COLOR_FG);
        f.setBackgroundColor(COLOR_BG);
    }

    f.setFont(u8g2_font_wqy12_t_chinese3);
    f.setCursor(LIST_PAD_X, rowY + LIST_TEXT_Y_OFFSET - 4);
    f.print(label);

    uint16_t swW = 30;
    uint16_t swH = 14;
    uint16_t swX = CONTENT_W - LIST_PAD_X - swW;
    uint16_t swY = rowY + (rowH - swH) / 2;

    if (focused) {
        g.fillRect(swX, swY, swW, swH, COLOR_BG);
        if (on) {
            g.fillCircle(swX + swW - swH / 2, swY + swH / 2, swH / 2 - 1, COLOR_FG);
        } else {
            g.fillCircle(swX + swH / 2, swY + swH / 2, swH / 2 - 1, COLOR_BG);
            g.drawCircle(swX + swH / 2, swY + swH / 2, swH / 2 - 1, COLOR_FG);
        }
    } else {
        g.drawRect(swX, swY, swW, swH, COLOR_FG);
        if (on) {
            g.fillRect(swX + 1, swY + 1, swW - 2, swH - 2, COLOR_FG);
            g.fillCircle(swX + swW - swH / 2, swY + swH / 2, swH / 2 - 2, COLOR_BG);
        } else {
            g.fillCircle(swX + swH / 2, swY + swH / 2, swH / 2 - 2, COLOR_FG);
        }
    }

    f.setForegroundColor(COLOR_FG);
    f.setBackgroundColor(COLOR_BG);
}

void UiCommon::drawIconAt(int16_t x, int16_t y, const char* name, uint16_t fallbackW, uint16_t fallbackH) {
    if (!dm_) return;
    auto& g = dm_->gfx();
    if (icons_) {
        const modules::IconEntry* e = icons_->getIcon(name);
        if (e) {
            g.drawBitmap(x, y, e->data, e->width, e->height, COLOR_FG);
            return;
        }
    }
    g.drawRect(x, y, fallbackW, fallbackH, COLOR_FG);
}

uint8_t UiCommon::getBatteryPercent() const {
    return bat_ ? bat_->getPercent() : 0;
}

} // namespace ui
