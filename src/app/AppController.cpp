#include "AppController.h"
#include "../config/Config.h"

namespace app {

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
        if (xQueueReceive(in_->eventQueue(), &e, portMAX_DELAY) != pdPASS) {
            continue;
        }
        log_i("event: %s", eventToStr(e));
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

} // namespace app
