#pragma once

#include <Arduino.h>
#include "../modules/DisplayModule.h"
#include "../modules/InputModule.h"
#include "../modules/BatteryModule.h"
#include "../modules/BleModule.h"
#include "../modules/SdModule.h"
#include "../modules/PdfStore.h"
#include "../modules/IconStore.h"
#include "../ui/UiCommon.h"
#include "../ui/Page.h"
#include "PageStack.h"
#include "InputEvent.h"

namespace app {

class AppController {
public:
    void begin(modules::DisplayModule* dm,
               modules::InputModule* in,
               modules::BatteryModule* bat,
               modules::BleModule* ble,
               modules::SdModule* sd,
               modules::PdfStore* pdf,
               modules::IconStore* icons,
               ui::UiCommon* ui);

    bool start();
    void pushPage(ui::Page* p);
    void popPage();
    void renderCurrent();
    void renderCurrent(modules::RefreshMode mode);

    modules::DisplayModule& display() { return *dm_; }
    modules::BatteryModule& battery() { return *bat_; }
    modules::BleModule&     ble()     { return *ble_; }
    modules::PdfStore&      pdf()     { return *pdf_; }
    modules::IconStore&     icons()   { return *icons_; }
    ui::UiCommon&           ui()      { return *ui_; }
    ui::Page*               currentPage() { return stack_.top(); }

private:
    static void taskTrampoline(void* arg);
    void run();

    modules::DisplayModule* dm_    = nullptr;
    modules::InputModule*   in_    = nullptr;
    modules::BatteryModule* bat_   = nullptr;
    modules::BleModule*     ble_   = nullptr;
    modules::SdModule*      sd_    = nullptr;
    modules::PdfStore*      pdf_   = nullptr;
    modules::IconStore*     icons_ = nullptr;
    ui::UiCommon*           ui_    = nullptr;
    bool                    pageNeedsFullRefresh_ = true;

    PageStack stack_;
};

} // namespace app
