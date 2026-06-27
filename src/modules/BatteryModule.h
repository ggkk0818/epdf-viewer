#pragma once

#include <Arduino.h>
#include <Wire.h>

namespace modules {

class BatteryModule {
public:
    bool begin();
    uint8_t getPercent();
    uint16_t getVoltageMv();
    bool isPresent() const { return present_; }

private:
    bool writeReg(uint8_t reg, uint16_t val);
    bool readReg(uint8_t reg, uint16_t& val);

    TwoWire* wire_ = &Wire;
    bool present_ = false;
    uint8_t lastPercent_ = 0;
};

} // namespace modules
