#include "DisplayModule.h"
#include "../config/Config.h"

namespace modules {

bool DisplayModule::begin() {
    const uint32_t clearStart = millis();
    display_ = new EpdPanel(
        GxEPD2_1020_GDEM102T91(
            cfg::pin::EPD_CS,
            cfg::pin::EPD_DC,
            cfg::pin::EPD_RST,
            cfg::pin::EPD_BUSY));
    // SPI.begin(cfg::pin::EPD_SCK, -1, cfg::pin::EPD_MOSI, cfg::pin::EPD_CS);
    // init() 1st arg is serial_diag_bitrate (NOT SPI speed). Pass 0 so GxEPD2
    // does not re-init Serial at a different baud.
    display_->init(0);
    display_->setRotation(1);
    display_->setTextColor(GxEPD_BLACK);

    u8g2_.begin(*display_);
    u8g2_.setForegroundColor(GxEPD_BLACK);
    u8g2_.setBackgroundColor(GxEPD_WHITE);

    // Initial full-refresh white clear so the first partial-refresh render
    // (MainPage) lands on a clean panel. Pad to full_refresh_time — display()
    // returns as soon as BUSY deasserts, which can be before the controller
    // has finished driving the waveform. See displayLoop() for the full
    // rationale.
    const int32_t clearRemaining =
        (int32_t)display_->epd2.full_refresh_time -
        (int32_t)(millis() - clearStart);
    if (clearRemaining > 0) {
        vTaskDelay(pdMS_TO_TICKS(clearRemaining));
    }

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
    bool renderArmed = renderArmed_;
    portEXIT_CRITICAL(&spinlock_);

    if (task_ && renderArmed) {
        xTaskNotifyGive(task_);
    }
}

void DisplayModule::armRendering() {
    bool shouldNotify = false;
    portENTER_CRITICAL(&spinlock_);
    renderArmed_ = true;
    shouldNotify = pending_;
    portEXIT_CRITICAL(&spinlock_);

    if (shouldNotify && task_) {
        xTaskNotifyGive(task_);
    }
}

void DisplayModule::displayTaskTrampoline(void* arg) {
    static_cast<DisplayModule*>(arg)->displayLoop();
}

void DisplayModule::displayLoop() {
    while (true) {
        if (stopped_) return;
        // Block until at least one render request arrives. The take resets
        // the notification count to zero, coalescing any number of wakes
        // into a single render pass.
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (stopped_) return;

        while (true) {
            // Snapshot the coalesced mode and clear pending under a brief
            // critical section.
            RefreshMode mode;
            portENTER_CRITICAL(&spinlock_);
            mode         = pendingMode_;
            pending_     = false;
            pendingMode_ = RefreshMode::Partial;
            portEXIT_CRITICAL(&spinlock_);

            refreshing_ = true;
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

            // Drive the panel. GxEPD2's display()/displayWindow() hand off to
            // _waitWhileBusy(), which only polls the BUSY pin — it returns the
            // instant BUSY deasserts (or its 10s safety timeout fires). The
            // panel's full_refresh_time / partial_refresh_time are *not*
            // enforced as a minimum there, so the call can come back before
            // the SSD1683 has finished driving the refresh waveforms. Letting
            // the loop proceed then re-grabs stateLock_ and re-issues a
            // refresh against a controller that is still mid-update — the
            // BUSY-based serialization between passes is effectively broken.
            //
            // Floor each pass at the panel's nominal refresh duration (read
            // from the panel class, so it tracks a future panel swap) and pad
            // with vTaskDelay if display() returned early.
            const uint16_t refreshMs = (mode == RefreshMode::Full)
                ? display_->epd2.full_refresh_time
                : display_->epd2.partial_refresh_time;
            const uint32_t refreshStart = millis();
            if (mode == RefreshMode::Full) {
                display_->display(false);
            } else {
                display_->displayWindow(0, 0,
                                        cfg::display::WIDTH,
                                        cfg::display::HEIGHT);
            }
            // millis() - refreshStart is wraparound-safe. Cast through signed
            // so a pass that legitimately ran long (refreshMs already elapsed)
            // yields a non-positive remainder and skips the padding.
            const int32_t remaining =
                (int32_t)refreshMs - (int32_t)(millis() - refreshStart);
            if (remaining > 0) {
                vTaskDelay(pdMS_TO_TICKS(remaining));
            }
            display_->powerOff();
            refreshing_ = false;
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

void DisplayModule::stopTask() {
    // 持 stateLock_ 直到任何进行中的 draw 阶段结束。这样删除任务时
    // 不会留下半完成的 draw，也不会有正在执行 draw callback 的任务。
    if (stateLock_) xSemaphoreTake(stateLock_, portMAX_DELAY);
    stopped_ = true;
    if (task_) {
        vTaskDelete(task_);
        task_ = nullptr;
    }
    if (stateLock_) xSemaphoreGive(stateLock_);
}

void DisplayModule::shutdownClear() {
    if (!display_) return;
    // 前置条件：调用方已 stopTask()，无并发任务争用面板。
    display_->setFullWindow();
    display_->fillScreen(GxEPD_WHITE);
    const uint32_t start = millis();
    display_->displayWindow(0, 0, cfg::display::WIDTH, cfg::display::HEIGHT);
    // 按 partial_refresh_time 补足，让 SSD1681 完成波形驱动
    // （与 displayLoop 同样的 floor 逻辑，避免 BUSY 提前返回）。
    const int32_t remaining =
        (int32_t)display_->epd2.partial_refresh_time -
        (int32_t)(millis() - start);
    if (remaining > 0) {
        vTaskDelay(pdMS_TO_TICKS(remaining));
    }
    display_->powerOff();
}

} // namespace modules
