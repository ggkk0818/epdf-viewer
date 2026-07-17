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

    // Refresh impact score. AppController accumulates this on each render
    // request; when the running total exceeds 100 it triggers a full refresh
    // and resets the counter, otherwise a partial refresh is used.
    virtual uint8_t refreshImpactScore() const { return 2; }
};

} // namespace ui
