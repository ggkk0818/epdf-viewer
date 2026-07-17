#pragma once

#include "Page.h"
#include <Arduino.h>
#include "../modules/PdfStore.h"

namespace ui {

class DocViewPage : public Page {
public:
    explicit DocViewPage(modules::PdfMeta meta, uint16_t initialPage = 0)
        : meta_(meta), initialPageIdx_(initialPage) {}

    void onEnter(::app::AppController& app) override;
    void onExit(::app::AppController& app) override;
    void onEvent(::app::InputEvent e, ::app::AppController& app) override;
    void render(modules::DisplayModule& dm, UiCommon& ui) override;
    uint8_t refreshImpactScore() const override { return 5; }

private:
    struct LoadWorkerState;

    modules::PdfMeta meta_;
    uint16_t initialPageIdx_ = 0;
    uint16_t requestedPageIdx_ = 0;
    uint16_t visiblePageIdx_ = 0;
    uint16_t pageCount_ = 0;
    uint32_t requestSeq_ = 0;
    uint8_t* visibleBuf_ = nullptr;
    uint16_t visiblePageW_ = 0;
    uint16_t visiblePageH_ = 0;
    bool visibleLoaded_ = false;
    bool loading_ = false;
    bool loadFailed_ = false;
    LoadWorkerState* worker_ = nullptr;

    void switchToPage(::app::AppController& app, uint16_t newIdx);
    bool ensureVisibleBuffer();
    bool startWorker(::app::AppController& app);
    void queueLoad(::app::AppController& app,
                   uint16_t newIdx,
                   bool publishRender = true);
    void commitLoadedPage(uint32_t requestSeq,
                          uint16_t pageIdx,
                          bool ok,
                          const uint8_t* srcBuf,
                          uint16_t pageW,
                          uint16_t pageH);
    static void loaderTaskTrampoline(void* arg);
};

} // namespace ui