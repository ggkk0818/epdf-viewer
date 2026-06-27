#pragma once

#include <Arduino.h>
#include <vector>
#include "../config/Config.h"

namespace modules {

class SdModule;

struct PdfDoc {
    String   name;
    uint16_t pageCount;
};

class PdfStore {
public:
    void begin(SdModule* sd) { sd_ = sd; }

    bool listDocs(std::vector<PdfDoc>& out);
    bool readPage(const String& doc, uint16_t pageIdx, uint8_t* buf, size_t len);

    static constexpr size_t PAGE_BYTES =
        (((cfg::display::WIDTH + 7) / 8) * cfg::display::HEIGHT);

private:
    SdModule* sd_ = nullptr;
};

} // namespace modules
