#include "DocViewPage.h"
#include "../app/AppController.h"
#include "../config/Config.h"
#include <esp_heap_caps.h>

namespace ui {

namespace {

constexpr uint16_t FULL_REFRESH_INTERVAL = 20;
constexpr int16_t LOADING_TEXT_X = 44;
constexpr int16_t LOADING_TEXT_Y = cfg::display::CONTENT_Y + 40;
constexpr int16_t ERROR_TEXT_X = 40;
constexpr int16_t ERROR_TEXT_Y = cfg::display::CONTENT_Y + 40;

} // namespace

struct DocViewPage::LoadWorkerState {
    ::app::AppController* app = nullptr;
    DocViewPage* owner = nullptr;
    TaskHandle_t task = nullptr;
    String dirName;
    uint8_t* stagingBuf = nullptr;
    uint16_t pendingPageIdx = 0;
    uint32_t pendingSeq = 0;
    modules::RefreshMode pendingMode = modules::RefreshMode::Partial;
    bool stopRequested = false;
};

void DocViewPage::onEnter(::app::AppController& app) {
    if (!ensureVisibleBuffer() || !startWorker(app)) {
        app.mutateUiState([&] {
            requestedPageIdx_ = 0;
            visiblePageIdx_ = 0;
            pageCount_ = meta_.pages;
            pageSwitchCount_ = 0;
            requestSeq_ = 0;
            visiblePageW_ = 0;
            visiblePageH_ = 0;
            visibleLoaded_ = false;
            loading_ = false;
            loadFailed_ = true;
        });
        return;
    }

    app.mutateUiState([&] {
        requestedPageIdx_ = 0;
        visiblePageIdx_ = 0;
        pageCount_ = meta_.pages;
        pageSwitchCount_ = 0;
        requestSeq_ = 0;
        visiblePageW_ = 0;
        visiblePageH_ = 0;
        visibleLoaded_ = false;
        loading_ = false;
        loadFailed_ = false;
    });
    queueLoad(app, 0, modules::RefreshMode::Full, false);
}

void DocViewPage::onExit(::app::AppController& /*app*/) {
    if (worker_) {
        TaskHandle_t task = worker_->task;
        worker_->owner = nullptr;
        worker_->stopRequested = true;
        worker_ = nullptr;
        if (task) {
            xTaskNotifyGive(task);
        }
    }

    if (visibleBuf_) {
        heap_caps_free(visibleBuf_);
        visibleBuf_ = nullptr;
    }
}

void DocViewPage::onEvent(::app::InputEvent e, ::app::AppController& app) {
    switch (e) {
        case ::app::InputEvent::UpLeft:
            if (requestedPageIdx_ > 0) {
                switchToPage(app, requestedPageIdx_ - 1);
            }
            break;
        case ::app::InputEvent::DownRight:
            if (requestedPageIdx_ + 1 < pageCount_) {
                switchToPage(app, requestedPageIdx_ + 1);
            }
            break;
        case ::app::InputEvent::Back:
            app.popPage();
            break;
        default: break;
    }
}

void DocViewPage::render(modules::DisplayModule& dm, UiCommon& ui) {
    ui.drawStatusBar(true, requestedPageIdx_ + 1, pageCount_);
    auto& g = dm.gfx();
    if (loading_) {
        auto& f = dm.fonts();
        f.setFont(u8g2_font_wqy12_t_gb2312);
        f.setForegroundColor(GxEPD_BLACK);
        f.setBackgroundColor(GxEPD_WHITE);
        f.setCursor(LOADING_TEXT_X, LOADING_TEXT_Y);
        f.print("正在加载...");
        return;
    }

    if (visibleLoaded_ && visibleBuf_) {
        g.drawBitmap(0,
                     cfg::display::CONTENT_Y,
                     visibleBuf_,
                     modules::PdfStore::VIEWPORT_W,
                     modules::PdfStore::VIEWPORT_H,
                     GxEPD_BLACK);
    } else {
        auto& f = dm.fonts();
        f.setFont(u8g2_font_wqy12_t_gb2312);
        f.setForegroundColor(GxEPD_BLACK);
        f.setBackgroundColor(GxEPD_WHITE);
        f.setCursor(ERROR_TEXT_X, ERROR_TEXT_Y);
        f.print("无法读取该页");
    }
}

void DocViewPage::switchToPage(::app::AppController& app, uint16_t newIdx) {
    bool shouldQueue = false;
    modules::RefreshMode mode = modules::RefreshMode::Partial;

    app.mutateUiState([&] {
        if (newIdx != requestedPageIdx_) {
            uint16_t newCount = ++pageSwitchCount_;
            mode = ((newCount % FULL_REFRESH_INTERVAL) == 0)
                ? modules::RefreshMode::Full
                : modules::RefreshMode::Partial;
            shouldQueue = true;
        }
    });

    if (shouldQueue) {
        queueLoad(app, newIdx, mode);
    }
}

bool DocViewPage::ensureVisibleBuffer() {
    if (visibleBuf_ != nullptr) {
        return true;
    }

    visibleBuf_ = (uint8_t*)heap_caps_malloc(modules::PdfStore::VIEWPORT_BYTES,
                                             MALLOC_CAP_SPIRAM);
    return visibleBuf_ != nullptr;
}

bool DocViewPage::startWorker(::app::AppController& app) {
    if (worker_) {
        worker_->owner = this;
        return true;
    }

    LoadWorkerState* worker = new LoadWorkerState();
    if (!worker) {
        return false;
    }

    worker->app = &app;
    worker->owner = this;
    worker->dirName = meta_.dirName;
    worker->stagingBuf = (uint8_t*)heap_caps_malloc(modules::PdfStore::VIEWPORT_BYTES,
                                                    MALLOC_CAP_SPIRAM);
    if (!worker->stagingBuf) {
        delete worker;
        return false;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(&DocViewPage::loaderTaskTrampoline,
                                            "docLoadTask",
                                            cfg::task::DOC_LOAD_STACK,
                                            worker,
                                            cfg::task::DOC_LOAD_PRIO,
                                            &worker->task,
                                            cfg::task::DOC_LOAD_CORE);
    if (ok != pdPASS) {
        heap_caps_free(worker->stagingBuf);
        delete worker;
        return false;
    }

    worker_ = worker;
    return true;
}

void DocViewPage::queueLoad(::app::AppController& app,
                            uint16_t newIdx,
                            modules::RefreshMode mode,
                            bool publishRender) {
    if (!worker_) {
        app.mutateUiState([&] {
            requestedPageIdx_ = newIdx;
            visiblePageIdx_ = newIdx;
            visibleLoaded_ = false;
            loading_ = false;
            loadFailed_ = true;
        });
        if (publishRender) {
            app.requestRender(mode);
        }
        return;
    }

    app.mutateUiState([&] {
        requestedPageIdx_ = newIdx;
        visibleLoaded_ = false;
        loading_ = true;
        loadFailed_ = false;
        visiblePageW_ = 0;
        visiblePageH_ = 0;
        requestSeq_++;
        worker_->pendingPageIdx = newIdx;
        worker_->pendingSeq = requestSeq_;
        worker_->pendingMode = mode;
    });

    if (publishRender) {
        app.requestRender(mode);
    }
    if (worker_->task) {
        xTaskNotifyGive(worker_->task);
    }
}

void DocViewPage::commitLoadedPage(uint32_t requestSeq,
                                   uint16_t pageIdx,
                                   bool ok,
                                   const uint8_t* srcBuf,
                                   uint16_t pageW,
                                   uint16_t pageH) {
    if (requestSeq != requestSeq_) {
        return;
    }

    visiblePageIdx_ = pageIdx;
    visiblePageW_ = pageW;
    visiblePageH_ = pageH;
    visibleLoaded_ = ok;
    loading_ = false;
    loadFailed_ = !ok;

    if (ok && visibleBuf_ && srcBuf) {
        memcpy(visibleBuf_, srcBuf, modules::PdfStore::VIEWPORT_BYTES);
    }
}

void DocViewPage::loaderTaskTrampoline(void* arg) {
    LoadWorkerState* worker = static_cast<LoadWorkerState*>(arg);
    uint32_t lastHandledSeq = 0;

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (true) {
            uint32_t requestSeq = 0;
            uint16_t pageIdx = 0;
            modules::RefreshMode mode = modules::RefreshMode::Partial;
            bool stopRequested = false;

            worker->app->mutateUiState([&] {
                stopRequested = worker->stopRequested;
                requestSeq = worker->pendingSeq;
                pageIdx = worker->pendingPageIdx;
                mode = worker->pendingMode;
            });

            if (stopRequested) {
                if (worker->stagingBuf) {
                    heap_caps_free(worker->stagingBuf);
                }
                delete worker;
                vTaskDelete(nullptr);
            }
            if (requestSeq == 0 || requestSeq == lastHandledSeq) {
                break;
            }

            uint16_t pageW = 0;
            uint16_t pageH = 0;
            bool ok = worker->app->pdf().readPageViewport(worker->dirName,
                                                          pageIdx,
                                                          worker->stagingBuf,
                                                          modules::PdfStore::VIEWPORT_BYTES,
                                                          pageW,
                                                          pageH);

            bool shouldRender = false;
            worker->app->mutateUiState([&] {
                if (!worker->stopRequested && worker->owner != nullptr &&
                    worker->pendingSeq == requestSeq &&
                    worker->pendingPageIdx == pageIdx) {
                    worker->owner->commitLoadedPage(requestSeq,
                                                    pageIdx,
                                                    ok,
                                                    worker->stagingBuf,
                                                    pageW,
                                                    pageH);
                    shouldRender = true;
                }
            });

            if (shouldRender) {
                worker->app->requestRender(mode);
            }

            lastHandledSeq = requestSeq;

            bool moreWork = false;
            worker->app->mutateUiState([&] {
                moreWork = worker->stopRequested || worker->pendingSeq != lastHandledSeq;
            });

            if (!moreWork) {
                break;
            }
        }
    }
}

} // namespace ui
