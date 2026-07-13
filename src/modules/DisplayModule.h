#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <gdey/GxEPD2_420_GDEY042T81.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace modules {

enum class RefreshMode : uint8_t {
    Partial = 0,
    Full    = 1,
};

using EpdPanel =
    GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT>;

class DisplayModule {
public:
    using DrawCallback = void (*)(void* ctx);

    bool begin();

    // Non-blocking render request. Coalesces: if multiple requests arrive
    // before the previous render completes, only the latest state is drawn.
    // If any intermediate request asked for Full, the next render is Full.
    void requestRender(RefreshMode mode);
    void armRendering();

    // Register the callback that draws the current page into the GxEPD2
    // buffer. Called on the DisplayModule task with the state lock held.
    void setDrawCallback(DrawCallback cb, void* ctx) {
        drawCb_   = cb;
        drawCtx_  = ctx;
    }

    // State lock. Held briefly by AppController during page-state mutation,
    // held by DisplayModule during the draw phase only (not the e-ink
    // refresh phase, so AppController can keep firing notifications).
    void lockState()   { xSemaphoreTake(stateLock_, portMAX_DELAY); }
    void unlockState() { xSemaphoreGive(stateLock_); }

    EpdPanel& gfx()                  { return *display_; }
    U8G2_FOR_ADAFRUIT_GFX& fonts()  { return u8g2_; }
    bool isReady() const             { return ready_; }

private:
    static void displayTaskTrampoline(void* arg);
    void displayLoop();

    EpdPanel*               display_ = nullptr;
    U8G2_FOR_ADAFRUIT_GFX   u8g2_;

    DrawCallback            drawCb_  = nullptr;
    void*                   drawCtx_ = nullptr;

    SemaphoreHandle_t       stateLock_ = nullptr;
    TaskHandle_t            task_      = nullptr;
    portMUX_TYPE            spinlock_  = portMUX_INITIALIZER_UNLOCKED;

    // Concurrent state, protected by spinlock_.
    volatile RefreshMode    pendingMode_ = RefreshMode::Partial;
    volatile bool           pending_     = false;
    volatile bool           renderArmed_ = false;

    bool                    ready_ = false;
};

} // namespace modules
