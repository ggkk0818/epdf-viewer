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
    std::vector<modules::PdfDoc> docs;
    app.pdf().listDocs(docs);
    for (const auto& d : docs) {
        if (d.name == docName_) {
            pageCount_ = d.pageCount;
            break;
        }
    }
    if (pageBuf_ == nullptr) {
        pageBuf_ = (uint8_t*)heap_caps_malloc(modules::PdfStore::VIEWPORT_BYTES, MALLOC_CAP_SPIRAM);
    }
    loaded_ = loadPage(app, pageIdx_);
}

void DocViewPage::onEvent(::app::InputEvent e, ::app::AppController& app) {
    switch (e) {
        case ::app::InputEvent::UpLeft:
            if (pageIdx_ > 0) {
                pageIdx_--;
                loaded_ = loadPage(app, pageIdx_);
                pageSwitchCount_++;
                if ((pageSwitchCount_ % FULL_REFRESH_INTERVAL) == 0) {
                    app.renderCurrent(modules::RefreshMode::Full);
                } else {
                    app.renderCurrent();
                }
            }
            break;
        case ::app::InputEvent::DownRight:
            if (pageIdx_ + 1 < pageCount_) {
                pageIdx_++;
                loaded_ = loadPage(app, pageIdx_);
                pageSwitchCount_++;
                if ((pageSwitchCount_ % FULL_REFRESH_INTERVAL) == 0) {
                    app.renderCurrent(modules::RefreshMode::Full);
                } else {
                    app.renderCurrent();
                }
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

bool DocViewPage::loadPage(::app::AppController& app, uint16_t idx) {
    if (!pageBuf_) return false;
    return app.pdf().readPageViewport(docName_, idx,
                                      pageBuf_, modules::PdfStore::VIEWPORT_BYTES,
                                      pageW_, pageH_);
}

} // namespace ui
