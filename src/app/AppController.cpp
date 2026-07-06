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
    dm_ = dm;
    in_ = in;
    bat_ = bat;
    ble_ = ble;
    sd_ = sd;
    pdf_ = pdf;
    icons_ = icons;
    ui_ = ui;
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

void AppController::pushPage(ui::Page* p) {
    stack_.push(p);
    p->onEnter(*this);
    pageNeedsFullRefresh_ = true;
    renderCurrent();
}

void AppController::popPage() {
    ui::Page* p = stack_.pop();
    if (p) {
        p->onExit(*this);
        delete p;
    }
    ui::Page* top = stack_.top();
    if (top) {
        top->onEnter(*this);
        pageNeedsFullRefresh_ = true;
        renderCurrent();
    }
}

void AppController::renderCurrent() {
    renderCurrent(pageNeedsFullRefresh_ ? modules::RefreshMode::Full
                                        : modules::RefreshMode::Partial);
}

void AppController::renderCurrent(modules::RefreshMode mode) {
    ui::Page* top = stack_.top();
    if (!top) return;
    dm_->startDraw();
    top->render(*dm_, *ui_);
    dm_->endDraw(mode);
    pageNeedsFullRefresh_ = false;
}

} // namespace app
