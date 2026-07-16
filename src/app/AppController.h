#pragma once

#include <Arduino.h>
#include <freertos/queue.h>
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

// Request to jump straight to the DocView page for a given document, posted
// from the BLE work task and consumed on the app task.
struct NavigationRequest {
    static constexpr size_t MAX_DOC_NAME = 256;
    char dirName[MAX_DOC_NAME + 1];  // canonical dir, "yyyy-mm-dd_HH-MM-SS_PPP_name"
    uint16_t page = 0;               // 0-based page index
};

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
    void pushPage(ui::Page* p,
                  modules::RefreshMode mode = modules::RefreshMode::Full);
    void popPage();

    template <typename Fn>
    void mutateUiState(Fn fn) {
        if (!dm_) return;
        dm_->lockState();
        fn();
        dm_->unlockState();
    }

    // Non-blocking render request. Multiple calls coalesce into a single
    // render of the latest page state.
    void requestRender() { requestRender(modules::RefreshMode::Partial); }
    void requestRender(modules::RefreshMode mode) { dm_->requestRender(mode); }

    // Thread-safe: may be called from the BLE work task. Enqueues a request
    // to rebuild the page stack as [MainPage -> DocListPage -> DocViewPage]
    // and scroll the DocViewPage to `req.page`. Returns false only if the
    // (small) navigation queue is full.
    bool requestViewOnDevice(const NavigationRequest& req);

    modules::DisplayModule& display() { return *dm_; }
    modules::BatteryModule& battery() { return *bat_; }
    modules::BleModule&     ble()     { return *ble_; }
    modules::SdModule&      sd()      { return *sd_; }
    modules::PdfStore&      pdf()     { return *pdf_; }
    modules::IconStore&     icons()   { return *icons_; }
    ui::UiCommon&           ui()      { return *ui_; }
    ui::Page*               currentPage() { return stack_.top(); }

private:
    static void taskTrampoline(void* arg);
    static void drawPageTrampoline(void* ctx);
    void run();
    void drawTopPage();
    void navigateToDocView(const NavigationRequest& req);

    modules::DisplayModule* dm_    = nullptr;
    modules::InputModule*   in_    = nullptr;
    modules::BatteryModule* bat_   = nullptr;
    modules::BleModule*     ble_   = nullptr;
    modules::SdModule*      sd_    = nullptr;
    modules::PdfStore*      pdf_   = nullptr;
    modules::IconStore*     icons_ = nullptr;
    ui::UiCommon*           ui_    = nullptr;

    PageStack stack_;
    QueueHandle_t navQueue_ = nullptr;
};

} // namespace app