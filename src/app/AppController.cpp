#include "AppController.h"
#include "../config/Config.h"
#include "../ui/MainPage.h"
#include "../ui/DocListPage.h"
#include "../ui/DocViewPage.h"

#include <esp_sleep.h>
#include <esp32-hal-gpio.h>

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

    if (ble_) {
        ble_->onAutoDisabled(&AppController::onBleAutoDisabled, this);
    }
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

void AppController::onBleAutoDisabled(void* ctx) {
    // Fired from the BLE watchdog task after it tears the stack down.
    // requestRender() guards stack_.top() with stateLock_ and the impact
    // accumulator with refreshScoreLock_, so it is safe to call cross-task.
    static_cast<AppController*>(ctx)->requestRender();
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

void AppController::pushPage(ui::Page* p) {
    p->onEnter(*this);

    // Publish the page only after onEnter has initialized the state that
    // render() observes.
    mutateUiState([&] {
        stack_.push(p);
    });
    requestRender();
}

void AppController::popPage() {
    // Phase 1 (under lock): tear down the current top. Page::onExit never
    // calls mutateUiState, so running it under the lock is safe.
    ui::Page* top = nullptr;
    mutateUiState([&] {
        ui::Page* p = stack_.pop();
        if (p) {
            p->onExit(*this);
            delete p;
        }
        top = stack_.top();
    });

    if (!top) return;

    // Phase 2 (outside lock): re-enter the new top. Pages like
    // DocListPage::onEnter call mutateUiState internally, and stateLock_
    // is a non-recursive FreeRTOS mutex — calling onEnter under the lock
    // self-deadlocks the app task and trips the task watchdog (reboot).
    // Matches the onEnter-outside-lock pattern used by pushPage() and
    // navigateToDocView().
    top->onEnter(*this);
    requestRender();
}

bool AppController::requestViewOnDevice(const NavigationRequest& req) {
    if (!navQueue_) return false;
    return xQueueSend(navQueue_, &req, 0) == pdPASS;
}

bool AppController::injectInputEvent(InputEvent e) {
    if (e == InputEvent::None || !in_) return false;
    QueueHandle_t q = in_->eventQueue();
    if (!q) return false;
    if (xQueueSendToBack(q, &e, 0) == pdPASS) {
        return true;
    }
    // Queue full — drop the oldest the same way InputModule::emit does, then
    // retry. We avoid the diagnostic counters here (those belong to InputModule).
    InputEvent dropped = InputEvent::None;
    if (xQueueReceive(q, &dropped, 0) == pdPASS &&
        xQueueSendToBack(q, &e, 0) == pdPASS) {
        log_w("input queue full, remote dropped oldest event=%u latest=%u",
              (unsigned)dropped, (unsigned)e);
        return true;
    }
    log_e("input queue saturated, remote failed to enqueue event=%u",
          (unsigned)e);
    return false;
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

    requestRender();
}

void AppController::requestRender() {
    if (!dm_) return;

    // Read the top page's score under stateLock_ so we don't race with
    // push/pop/delete in the app task. (docLoadTask reaches us here without
    // holding stateLock_, so an unlocked read of stack_.top() could observe
    // a freed Page*.)
    uint8_t score = 0;
    dm_->lockState();
    ui::Page* top = stack_.top();
    if (top) score = top->refreshImpactScore();
    dm_->unlockState();

    modules::RefreshMode mode;
    portENTER_CRITICAL(&refreshScoreLock_);
    refreshScore_ += score;
    if (refreshScore_ > 100) {
        mode = modules::RefreshMode::Full;
        refreshScore_ = 0;
    } else {
        mode = modules::RefreshMode::Partial;
    }
    portEXIT_CRITICAL(&refreshScoreLock_);

    dm_->requestRender(mode);
}

void AppController::requestShutdown() {
    performShutdown_();
    // 不应到达此处。防御性兜底。
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void AppController::performShutdown_() {
    log_i("shutdown: starting");

    // 0. 先停 InputModule —— 用户仍按住 Boot，停掉后避免释放时塞幽灵 Enter。
    if (in_) in_->stop();

    // 1. 停 BLE 看门狗（必须在 setEnabled(false) 之前，否则看门狗可能在
    //    拆栈过程中触发 onAutoDisabled → requestRender，留下 stale pending）。
    if (ble_) ble_->stopWatchdog();

    // 2. 完整 BLE 栈拆除（断开连接 + BLEDevice::deinit）。
    if (ble_) ble_->setEnabled(false);

    // 3. 给 BLE work / OTA 任务一个调度窗口，让它们排空任何已在队列里
    //    的 SD 写入，避免下一步 SD.end() 时正在写。
    vTaskDelay(pdMS_TO_TICKS(cfg::sleep::SHUTDOWN_INPUT_DRAIN_MS));

    // 4. 停 displayTask —— 持 stateLock_ 直到任何 draw 阶段结束再删除。
    if (dm_) {
        dm_->stopTask();
        // 5. 同步局刷白屏 —— displayTask 已不存在，可直接驱动面板。
        dm_->shutdownClear();
    }

    // 6. 卸载 SD 卡。
    if (sd_) sd_->end();

    // 7. 停电池采样任务。其他任务（BleCmdDispatcher / OtaService）在 BLE
    //    deinit 后无新工作，会一直阻塞在队列上，由 deep sleep 隐式回收。
    if (bat_) bat_->stop();

    // 8. 武装 Boot(GPIO 0) 作为 ext0 唤醒源，低电平触发（按键按下）。
    //    冷启动路径也在 main.cpp 里武装了一次，重复武装无副作用。
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);

    log_i("shutdown: entering deep sleep");
    // 9. 进入深度休眠 —— 永不返回。
    esp_deep_sleep_start();
}

} // namespace app