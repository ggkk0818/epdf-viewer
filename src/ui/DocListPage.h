#pragma once

#include "Page.h"
#include <vector>
#include "../modules/PdfStore.h"

namespace ui {

class DocListPage : public Page {
public:
    void onEnter(::app::AppController& app) override;
    void onEvent(::app::InputEvent e, ::app::AppController& app) override;
    void render(modules::DisplayModule& dm, UiCommon& ui) override;

private:
    std::vector<modules::PdfMeta> docs_;
    uint16_t idx_ = 0;
    uint16_t top_ = 0;
    static constexpr uint8_t VISIBLE_ROWS = (cfg::display::CONTENT_H) / 28;
};

} // namespace ui
