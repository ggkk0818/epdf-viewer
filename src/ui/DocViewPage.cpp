#include "DocViewPage.h"
#include "../app/AppController.h"
#include <esp_heap_caps.h>

namespace ui {

namespace {

constexpr uint16_t FULL_REFRESH_INTERVAL = 20;

} // namespace

void DocViewPage::onEnter(::app::AppController& app) {
    pageIdx_ = 0;
    pageSwitchCount_ = 0;
    pageCount_ = meta_.pages;
    if (pageBuf_ == nullptr) {
        pageBuf_ = (uint8_t*)heap_caps_malloc(modules::PdfStore::VIEWPORT_BYTES, MALLOC_CAP_SPIRAM);
    }
    loaded_ = loadPage(app, pageIdx_);
}

void DocViewPage::onEvent(::app::InputEvent e, ::app::AppController& app) {
    switch (e) {
        case ::app::InputEvent::UpLeft:
            if (pageIdx_ > 0) {
                switchToPage(app, pageIdx_ - 1);
            }
            break;
        case ::app::InputEvent::DownRight:
            if (pageIdx_ + 1 < pageCount_) {
                switchToPage(app, pageIdx_ + 1);
            }
            break;
        case ::app::InputEvent::Back:
            app.popPage();
            break;
        default: break;
    }
}

void DocViewPage::render(modules::DisplayModule& dm, UiCommon& ui) {
    ui.drawStatusBar(true, pageIdx_ + 1, pageCount_);
    auto& g = dm.gfx();
    if (loaded_ && pageBuf_) {
        g.drawBitmap(0,
                     cfg::display::CONTENT_Y,
                     pageBuf_,
                     modules::PdfStore::VIEWPORT_W,
                     modules::PdfStore::VIEWPORT_H,
                     GxEPD_BLACK);
    } else {
        auto& f = dm.fonts();
        f.setFont(u8g2_font_wqy12_t_gb2312);
        f.setForegroundColor(GxEPD_BLACK);
        f.setBackgroundColor(GxEPD_WHITE);
        f.setCursor(40, cfg::display::CONTENT_Y + 40);
        f.print("无法读取该页");
    }
}

void DocViewPage::switchToPage(::app::AppController& app, uint16_t newIdx) {
    // Mark the page as not-loaded under the state lock so a concurrent
    // render won't read pageBuf_ while we overwrite it via SD read.
    app.display().lockState();
    pageIdx_ = newIdx;
    loaded_  = false;
    app.display().unlockState();

    // SD read runs unlocked — DisplayModule can keep refreshing the
    // previous frame or the "无法读取该页" placeholder during this time.
    bool ok = loadPage(app, newIdx);

    uint16_t newCount;
    app.display().lockState();
    loaded_      = ok;
    newCount     = ++pageSwitchCount_;
    app.display().unlockState();

    app.requestRender(((newCount % FULL_REFRESH_INTERVAL) == 0)
                          ? modules::RefreshMode::Full
                          : modules::RefreshMode::Partial);
}

bool DocViewPage::loadPage(::app::AppController& app, uint16_t idx) {
    if (!pageBuf_) return false;
    return app.pdf().readPageViewport(meta_.dirName, idx,
                                      pageBuf_, modules::PdfStore::VIEWPORT_BYTES,
                                      pageW_, pageH_);
}

} // namespace ui
