#include "DisplayModule.h"
#include "../config/Config.h"

namespace modules {

namespace {

struct RefreshRequest {
    RefreshMode mode;
    Rect rect;
};

} // namespace

bool DisplayModule::begin() {
    display_ = new EpdPanel(
        GxEPD2_420_GDEY042T81(
            cfg::pin::EPD_CS,
            cfg::pin::EPD_DC,
            cfg::pin::EPD_RST,
            cfg::pin::EPD_BUSY));

    display_->init(cfg::display::SPI_HZ, true, 50, false);
    display_->setRotation(0);
    display_->setTextColor(GxEPD_BLACK);

    u8g2_.begin(*display_);
    u8g2_.setForegroundColor(GxEPD_BLACK);
    u8g2_.setBackgroundColor(GxEPD_WHITE);

    refreshQueue_ = xQueueCreate(1, sizeof(RefreshRequest));
    doneSem_ = xSemaphoreCreateBinary();
    if (!refreshQueue_ || !doneSem_) {
        log_e("display queue/sem create failed");
        return false;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        &DisplayModule::displayTaskTrampoline,
        "displayTask",
        cfg::task::DISPLAY_STACK,
        this,
        cfg::task::DISPLAY_PRIO,
        nullptr,
        cfg::task::DISPLAY_CORE);
    if (ok != pdPASS) {
        log_e("displayTask create failed");
        return false;
    }

    ready_ = true;
    log_i("DisplayModule ready");
    return true;
}

void DisplayModule::startDraw() {
    display_->setFullWindow();
    display_->fillScreen(GxEPD_WHITE);
}

void DisplayModule::endDraw(RefreshMode mode, const Rect* rect) {
    RefreshRequest req;
    req.mode = mode;
    if (rect && mode == RefreshMode::Partial) {
        req.rect = *rect;
    } else {
        req.rect.x = 0;
        req.rect.y = 0;
        req.rect.w = cfg::display::WIDTH;
        req.rect.h = cfg::display::HEIGHT;
    }
    xQueueSend(refreshQueue_, &req, portMAX_DELAY);
    xSemaphoreTake(doneSem_, portMAX_DELAY);
}

void DisplayModule::displayTaskTrampoline(void* arg) {
    static_cast<DisplayModule*>(arg)->displayLoop();
}

void DisplayModule::displayLoop() {
    RefreshRequest req;
    while (true) {
        if (xQueueReceive(refreshQueue_, &req, portMAX_DELAY) != pdPASS) {
            continue;
        }
        if (req.mode == RefreshMode::Full) {
            display_->display(false);
        } else {
            display_->displayWindow(req.rect.x, req.rect.y, req.rect.w, req.rect.h);
        }
        xSemaphoreGive(doneSem_);
    }
}

} // namespace modules
