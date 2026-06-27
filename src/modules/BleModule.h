#pragma once

#include <Arduino.h>

namespace modules {

class BleModule {
public:
    bool begin();
    void setEnabled(bool on);
    bool isEnabled() const { return enabled_; }

    void setBatteryLevel(uint8_t percent);

private:
    bool enabled_ = false;
    bool initialized_ = false;
};

} // namespace modules
