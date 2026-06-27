#pragma once

#include <Arduino.h>

namespace modules {

struct IconEntry {
    char     name[24];
    uint16_t width;
    uint16_t height;
    uint8_t* data;
};

class IconStore {
public:
    bool begin();
    const IconEntry* getIcon(const char* name) const;
    size_t count() const { return count_; }

private:
    static constexpr size_t MAX_ICONS = 32;
    IconEntry entries_[MAX_ICONS];
    size_t count_ = 0;
};

} // namespace modules
