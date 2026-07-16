#include "AppController.h"
#include "../config/Config.h"
#include "../ui/MainPage.h"
#include "../ui/DocListPage.h"
#include "../ui/DocViewPage.h"

namespace app {

namespace {
constexpr UBaseType_t NAV_QUEUE_LEN = 4;
constexpr TickType_t APP_POLL_TICKS = pdMS_TO_TICKS(100);
} // namespace

void AppController::begin(modules::DisplayModule* dm,
                          modules::InputModule* in,
                          modules::BatteryModule* bat,
                          modules::BleModule* ble,
                          modules::SdModule* sd,
                          modules::PdfStore* pdf,
                          modules::IconStore* icons,
                          ui::UiCommon* ui) {
    dm_    = dm;
    in_    = in;
    bat_   = bat;
    ble_   = ble;
    sd_    = sd;
    pdf_   = pdf;
    icons_ = icons;
    ui_    = ui;

    dm_->setDrawCallback(&AppController::drawPageTrampoline, this);

    navQueue_ = xQueueCreate(NAV_QUEUE_LEN, sizeof(NavigationRequest));
}

bool AppController::start() {
    BaseType_t ok = xTaskCreatePinnedToCore(
        &AppController::taskTrampoline,
        "appTask",
        cfg::task::APP_STACK,
        this,
        cfg::task::APP_PRIO,
        nullptr,
        cfg::task::APP_CORE);
    if (ok != pdPASS) {
        log_e("appTask create failed");
        return false;
    }
    return true;
}

void AppController::taskTrampoline(void* arg) {
    static_cast<AppController*>(arg)->run();
}

void AppController::drawPageTrampoline(void* ctx) {
    static_cast<AppController*>(ctx)->drawTopPage();
}

void AppController::drawTopPage() {
    ui::Page* top = stack_.top();
    if (top) {
        top->render(*dm_, *ui_);
    }
}

void AppController::run() {
    InputEvent e;
    while (true) {
        // Drain any pending navigation requests first so a BLE-triggered
        // view-on-device takes effect before subsequent input events are
        // dispatched against a stale page.
        if (navQueue_) {
            NavigationRequest req;
            while (xQueueReceive(navQueue_, &req, 0) == pdPASS) {
                navigateToDocView(req);
            }
        }

        if (xQueueReceive(in_->eventQueue(), &e, APP_POLL_TICKS) != pdPASS) {
            continue;
        }
        log_i("event: %s", eventToStr(e));

        // New navigation may have arrived while waiting for input; handle it
        // before dispatching the event.
        if (navQueue_) {
            NavigationRequest req;
            while (xQueueReceive(navQueue_, &req, 0) == pdPASS) {
                navigateToDocView(req);
            }
        }

        ui::Page* top = stack_.top();
        if (!top) continue;
        top->onEvent(e, *this);
    }
}

void AppController::pushPage(ui::Page* p, modules::RefreshMode mode) {
    p->onEnter(*this);

    // Publish the page only after onEnter has initialized the state that
    // render() observes.
    mutateUiState([&] {
        stack_.push(p);
    });
    requestRender(mode);
}

void AppController::popPage() {
    ui::Page* top = nullptr;
    mutateUiState([&] {
        ui::Page* p = stack_.pop();
        if (p) {
            p->onExit(*this);
            delete p;
        }
        top = stack_.top();
        if (top) {
            top->onEnter(*this);
        }
    });

    if (top) {
        requestRender(modules::RefreshMode::Full);
    }
}

bool AppController::requestViewOnDevice(const NavigationRequest& req) {
    if (!navQueue_) return false;
    return xQueueSend(navQueue_, &req, 0) == pdPASS;
}

void AppController::navigateToDocView(const NavigationRequest& req) {
    // Resolve the requested document meta from the canonical dir name so we
    // reuse the exact same DocViewPage path a manual open takes.
    modules::PdfMeta meta;
    if (!modules::PdfStore::parseDirName(String(req.dirName), meta)) {
        log_w("navigateToDocView: bad dir name '%s'", req.dirName);
        return;
    }

    // Tear down the existing stack. Each page's onExit must be called so
    // DocViewPage stops its worker task and frees PSRAM buffers. PageStack's
    // plain clear() only deletes the objects (default destructor) without
    // invoking onExit, so we pop+onExit manually.
    mutateUiState([&] {
        while (stack_.size() > 0) {
            ui::Page* p = stack_.pop();
            if (p) {
                p->onExit(*this);
                delete p;
            }
        }
    });

    // Allocate pages. onEnter must run OUTSIDE the state lock because
    // DocViewPage::onEnter (and DocListPage::onEnter) call mutateUiState
    // internally, and stateLock_ is a non-recursive FreeRTOS mutex.
    // The stack is empty during this window, so the display task (if it
    // happens to run) simply draws nothing — no use-after-free risk.
    ui::MainPage* main = new ui::MainPage();
    ui::DocListPage* list = new ui::DocListPage();
    ui::DocViewPage* view = new ui::DocViewPage(meta, req.page);

    main->onEnter(*this);
    list->onEnter(*this);
    view->onEnter(*this);

    // Publish the rebuilt stack atomically. The display task will pick up
    // the fully-initialized DocViewPage on the next render.
    mutateUiState([&] {
        stack_.push(main);
        stack_.push(list);
        stack_.push(view);
    });

    requestRender(modules::RefreshMode::Full);
}

} // namespace app