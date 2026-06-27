#pragma once

#include "../modules/DisplayModule.h"
#include "../app/InputEvent.h"

namespace ui { class UiCommon; }

namespace app { class AppController; }

namespace ui {

class Page {
public:
    virtual ~Page() = default;
    virtual void onEnter(::app::AppController& /*app*/) {}
    virtual void onExit(::app::AppController& /*app*/) {}
    virtual void onEvent(::app::InputEvent /*e*/, ::app::AppController& /*app*/) {}
    virtual void render(modules::DisplayModule& dm, UiCommon& ui) = 0;
};

} // namespace ui
