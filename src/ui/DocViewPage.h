#pragma once

#include "Page.h"
#include <Arduino.h>

namespace ui {

class DocViewPage : public Page {
public:
    explicit DocViewPage(const String& docName) : docName_(docName) {}

    void onEnter(::app::AppController& app) override;
    void onEvent(::app::InputEvent e, ::app::AppController& app) override;
    void render(modules::DisplayModule& dm, UiCommon& ui) override;

private:
    String docName_;
    uint16_t pageIdx_ = 0;
    uint16_t pageCount_ = 0;
    uint16_t pageSwitchCount_ = 0;
    uint8_t* pageBuf_ = nullptr;
    uint16_t pageW_ = 0;
    uint16_t pageH_ = 0;
    bool loaded_ = false;

    bool loadPage(::app::AppController& app, uint16_t idx);
};

} // namespace ui
