#include "DocListPage.h"
#include "../app/AppController.h"
#include "DocViewPage.h"

namespace ui {

void DocListPage::onEnter(::app::AppController& app) {
    docs_.clear();
    idx_ = 0;
    top_ = 0;
    app.pdf().listDocs(docs_);
}

void DocListPage::onEvent(::app::InputEvent e, ::app::AppController& app) {
    if (docs_.empty()) {
        if (e == ::app::InputEvent::Back) app.popPage();
        return;
    }
    bool changed = false;
    switch (e) {
        case ::app::InputEvent::UpLeft:
            if (idx_ > 0) {
                idx_--;
                if (idx_ < top_) top_ = idx_;
                changed = true;
            }
            break;
        case ::app::InputEvent::DownRight:
            if (idx_ + 1 < docs_.size()) {
                idx_++;
                if (idx_ - top_ >= VISIBLE_ROWS) top_ = idx_ - VISIBLE_ROWS + 1;
                changed = true;
            }
            break;
        case ::app::InputEvent::Enter:
            app.pushPage(new DocViewPage(docs_[idx_].name));
            break;
        case ::app::InputEvent::Back:
            app.popPage();
            break;
        default: break;
    }
    if (changed) app.requestRender();
}

void DocListPage::render(modules::DisplayModule& dm, UiCommon& ui) {
    ui.drawStatusBar(false, 0, 0);
    if (docs_.empty()) {
        auto& f = dm.fonts();
        f.setFont(u8g2_font_wqy12_t_gb2312);
        f.setForegroundColor(GxEPD_BLACK);
        f.setBackgroundColor(GxEPD_WHITE);
        f.setCursor(60, cfg::display::CONTENT_Y + 40);
        f.print("无文档");
        f.setCursor(40, cfg::display::CONTENT_Y + 60);
        f.print("请放入 /pdf/目录");
        return;
    }
    uint16_t shown = (uint16_t)docs_.size() - top_;
    if (shown > VISIBLE_ROWS) shown = VISIBLE_ROWS;
    for (uint16_t i = 0; i < shown; i++) {
        uint16_t docIdx = top_ + i;
        const auto& d = docs_[docIdx];
        String pages = String(d.pageCount) + " 页";
        ui.drawListRow(i * 28, d.name, pages, docIdx == idx_);
    }
}

} // namespace ui
