#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <freertos/semphr.h>

namespace modules {

enum class PowerState : uint8_t {
    Discharging,
    Charging,
    Full,
};

using BatteryNotifyCb = void (*)(uint8_t percent, PowerState state, void* ctx);

class BatteryModule {
public:
    bool begin();
    uint8_t  getPercent();
    uint16_t getVoltageMv();
    int16_t  getCurrentMa();
    int16_t  getTemperatureC();
    PowerState getPowerState() const { return powerState_; }
    bool isPresent() const { return present_; }

    void setNotifyCallback(BatteryNotifyCb cb, void* ctx) {
        notifyCb_ = cb;
        notifyCtx_ = ctx;
    }

private:
    void tick();
    static void taskTrampoline(void* arg);
    bool readReg16(uint8_t hiReg, uint16_t& val);
    bool writeReg8(uint8_t reg, uint8_t val);
    uint8_t ocvToPercent(uint16_t vMv);

    TwoWire* wire_ = &Wire;
    bool present_ = false;
    SemaphoreHandle_t mutex_ = nullptr;

    // Cached latest readings (written by task under mutex_, read by getters).
    uint8_t    percent_     = 0;
    uint16_t   voltageMv_   = 0;
    int16_t    currentMa_   = 0;
    int16_t    tempCx10_    = 0;
    PowerState powerState_  = PowerState::Discharging;

    // Hybrid algorithm state.
    int32_t  coulombMAs_    = 0;
    uint32_t lastTickMs_    = 0;
    uint32_t staticSinceMs_ = 0;
    uint16_t lastACR_       = 0x7FFF;   // LTC2944 accumulated-charge baseline (chip default)

    // Upper-layer notify callback.
    BatteryNotifyCb notifyCb_  = nullptr;
    void*           notifyCtx_ = nullptr;
};

} // namespace modules
