#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <gdey/GxEPD2_420_GDEY042T81.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

namespace modules {

enum class RefreshMode : uint8_t {
    Full,
    Partial,
};

struct Rect {
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t w = 0;
    uint16_t h = 0;
};

using EpdPanel =
    GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT>;

class DisplayModule {
public:
    bool begin();
    void startDraw();
    void endDraw(RefreshMode mode, const Rect* rect = nullptr);

    EpdPanel& gfx() { return *display_; }
    U8G2_FOR_ADAFRUIT_GFX& fonts() { return u8g2_; }

    bool isReady() const { return ready_; }

private:
    static void displayTaskTrampoline(void* arg);
    void displayLoop();

    EpdPanel* display_ = nullptr;
    U8G2_FOR_ADAFRUIT_GFX u8g2_;

    QueueHandle_t refreshQueue_  = nullptr;
    SemaphoreHandle_t doneSem_   = nullptr;
    bool ready_ = false;
};

} // namespace modules
