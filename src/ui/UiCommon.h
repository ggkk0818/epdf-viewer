#pragma once

#include <Arduino.h>
#include "../modules/DisplayModule.h"

namespace modules { class BatteryModule; }
namespace modules { class BleModule; }
namespace modules { class IconStore; }

namespace ui {

class UiCommon {
public:
    void begin(modules::DisplayModule* dm,
               modules::BatteryModule* bat,
               modules::BleModule* ble,
               modules::IconStore* icons);

    void drawStatusBar(bool showPage, uint16_t pageCur, uint16_t pageTotal);
    void drawListRow(uint16_t y, const String& title, const String& value, bool focused);
    void drawGridItem(uint8_t col, uint8_t row, const String& label, const char* iconName, bool focused);
    void drawSwitchRow(uint16_t y, const String& label, bool on, bool focused);

    uint16_t textHeight() const { return 12; }
    uint8_t  getBatteryPercent() const;

private:
    modules::DisplayModule* dm_   = nullptr;
    modules::BatteryModule* bat_  = nullptr;
    modules::BleModule*     ble_  = nullptr;
    modules::IconStore*     icons_ = nullptr;

    void drawIconAt(int16_t x, int16_t y, const char* name, uint16_t fallbackW, uint16_t fallbackH);
};

} // namespace ui
