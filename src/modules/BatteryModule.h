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

    // 删除采样任务。供关机流程调用。
    void stop();

    // 当前是否处于 tick() 执行窗口内。供浅睡眠决策避免在 I2C transaction
    // 中途暂停 CPU。
    bool isTicking() const { return ticking_; }

    // 距离下一次 tick() 的预估毫秒数。基于 lastTickMs_（上次 tick 的 millis）
    // 和 1Hz 任务周期推算，对齐浅睡眠定时器到下一轮电池采样。任务未启动时
    // 返回完整周期 1000ms。
    uint32_t msToNextTick() const;

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
    TaskHandle_t task_ = nullptr;

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
    volatile bool ticking_  = false;   // 1Hz tick() 执行窗口标志，供浅睡眠决策读取

    // Upper-layer notify callback.
    BatteryNotifyCb notifyCb_  = nullptr;
    void*           notifyCtx_ = nullptr;
};

} // namespace modules
