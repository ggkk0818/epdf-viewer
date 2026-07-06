#include "DisplayModule.h"
#include "../config/Config.h"

namespace modules {

namespace {

constexpr UBaseType_t REFRESH_QUEUE_LEN = 4;

struct RefreshRequest {
    RefreshMode mode;
    Rect rect;
};

Rect fullScreenRect() {
    Rect rect;
    rect.x = 0;
    rect.y = 0;
    rect.w = cfg::display::WIDTH;
    rect.h = cfg::display::HEIGHT;
    return rect;
}

} // namespace

bool DisplayModule::begin() {
    display_ = new EpdPanel(
        GxEPD2_420_GDEY042T81(
            cfg::pin::EPD_CS,
            cfg::pin::EPD_DC,
            cfg::pin::EPD_RST,
            cfg::pin::EPD_BUSY));
    SPI.begin(cfg::pin::EPD_SCK, -1, cfg::pin::EPD_MOSI, cfg::pin::EPD_CS);
    display_->init(cfg::display::SPI_HZ, true, 50, false);
    display_->setRotation(1);
    display_->setTextColor(GxEPD_BLACK);

    u8g2_.begin(*display_);
    u8g2_.setForegroundColor(GxEPD_BLACK);
    u8g2_.setBackgroundColor(GxEPD_WHITE);

    refreshQueue_ = xQueueCreate(REFRESH_QUEUE_LEN, sizeof(RefreshRequest));
    drawLock_ = xSemaphoreCreateMutex();
    if (!refreshQueue_ || !drawLock_) {
        log_e("display queue/lock create failed");
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
    xSemaphoreTake(drawLock_, portMAX_DELAY);
    display_->setFullWindow();
    display_->fillScreen(GxEPD_WHITE);
}

void DisplayModule::endDraw(RefreshMode mode, const Rect* rect) {
    RefreshRequest req;
    req.mode = mode;
    if (rect && mode == RefreshMode::Partial) {
        req.rect = *rect;
    } else {
        req.rect = fullScreenRect();
    }

    RefreshRequest dropped;
    while (xQueueSend(refreshQueue_, &req, 0) != pdPASS) {
        if (xQueueReceive(refreshQueue_, &dropped, 0) != pdPASS) {
            continue;
        }
        if (dropped.mode == RefreshMode::Full) {
            req.mode = RefreshMode::Full;
            req.rect = fullScreenRect();
        }
    }
    xSemaphoreGive(drawLock_);
}

void DisplayModule::displayTaskTrampoline(void* arg) {
    static_cast<DisplayModule*>(arg)->displayLoop();
}

void DisplayModule::displayLoop() {
    RefreshRequest req;
    RefreshRequest newer;
    while (true) {
        if (xQueueReceive(refreshQueue_, &req, portMAX_DELAY) != pdPASS) {
            continue;
        }

        xSemaphoreTake(drawLock_, portMAX_DELAY);
        bool mustUseFullRefresh = (req.mode == RefreshMode::Full);
        while (xQueueReceive(refreshQueue_, &newer, 0) == pdPASS) {
            if (newer.mode == RefreshMode::Full) {
                mustUseFullRefresh = true;
            }
            req = newer;
        }
        if (mustUseFullRefresh) {
            req.mode = RefreshMode::Full;
            req.rect = fullScreenRect();
        }

        if (req.mode == RefreshMode::Full) {
            display_->display(false);
        } else {
            display_->displayWindow(req.rect.x, req.rect.y, req.rect.w, req.rect.h);
        }
        xSemaphoreGive(drawLock_);
    }
}

} // namespace modules
