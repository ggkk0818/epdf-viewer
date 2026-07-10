#include "DisplayModule.h"
#include "../config/Config.h"

namespace modules {

bool DisplayModule::begin() {
    display_ = new EpdPanel(
        GxEPD2_420_GDEY042T81(
            cfg::pin::EPD_CS,
            cfg::pin::EPD_DC,
            cfg::pin::EPD_RST,
            cfg::pin::EPD_BUSY));
    SPI.begin(cfg::pin::EPD_SCK, -1, cfg::pin::EPD_MOSI, cfg::pin::EPD_CS);
    // init() 1st arg is serial_diag_bitrate (NOT SPI speed). Pass 0 so GxEPD2
    // does not re-init Serial at a different baud.
    display_->init(0, true, 50, false);
    display_->setRotation(1);
    display_->setTextColor(GxEPD_BLACK);

    u8g2_.begin(*display_);
    u8g2_.setForegroundColor(GxEPD_BLACK);
    u8g2_.setBackgroundColor(GxEPD_WHITE);

    // Prime the panel: the first user-visible render can come up blank on a
    // cold-boot SSD1683 because its RAM and LUT state are undefined until a
    // full clear+refresh has run once. Do that synchronously here, before the
    // display task starts, so the first requestRender() lands on a clean
    // panel.
    display_->clearScreen(GxEPD_WHITE);

    stateLock_ = xSemaphoreCreateMutex();
    if (!stateLock_) {
        log_e("stateLock create failed");
        return false;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        &DisplayModule::displayTaskTrampoline,
        "displayTask",
        cfg::task::DISPLAY_STACK,
        this,
        cfg::task::DISPLAY_PRIO,
        &task_,
        cfg::task::DISPLAY_CORE);
    if (ok != pdPASS) {
        log_e("displayTask create failed");
        return false;
    }

    ready_ = true;
    log_i("DisplayModule ready");
    return true;
}

void DisplayModule::requestRender(RefreshMode mode) {
    portENTER_CRITICAL(&spinlock_);
    if ((uint8_t)mode > (uint8_t)pendingMode_) {
        pendingMode_ = mode;
    }
    pending_ = true;
    portEXIT_CRITICAL(&spinlock_);

    if (task_) {
        xTaskNotifyGive(task_);
    }
}

void DisplayModule::displayTaskTrampoline(void* arg) {
    static_cast<DisplayModule*>(arg)->displayLoop();
}

void DisplayModule::displayLoop() {
    while (true) {
        // Block until at least one render request arrives. The take resets
        // the notification count to zero, coalescing any number of wakes
        // into a single render pass.
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (true) {
            // Snapshot the coalesced mode and clear pending under a brief
            // critical section.
            RefreshMode mode;
            portENTER_CRITICAL(&spinlock_);
            mode         = pendingMode_;
            pending_     = false;
            pendingMode_ = RefreshMode::Partial;
            portEXIT_CRITICAL(&spinlock_);

            // Hold the state lock only during the draw phase — this is the
            // only window where page state is read. The e-ink refresh below
            // runs unlocked, so AppController can keep mutating state and
            // firing notifications during the (slow) refresh.
            xSemaphoreTake(stateLock_, portMAX_DELAY);
            display_->setFullWindow();
            display_->fillScreen(GxEPD_WHITE);
            if (drawCb_) {
                drawCb_(drawCtx_);
            }
            xSemaphoreGive(stateLock_);

            // Drive the panel. GxEPD2's display()/displayWindow() block on
            // the BUSY pin, so the next iteration's draw can't start until
            // the refresh finishes — no buffer tearing risk.
            if (mode == RefreshMode::Full) {
                display_->display(false);
            } else {
                display_->displayWindow(0, 0,
                                        cfg::display::WIDTH,
                                        cfg::display::HEIGHT);
            }

            // If another request arrived during this pass, loop and render
            // the latest state. Otherwise wait for the next notification.
            bool more;
            portENTER_CRITICAL(&spinlock_);
            more = pending_;
            portEXIT_CRITICAL(&spinlock_);
            if (!more) break;
        }
    }
}

} // namespace modules
