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

#pragma pack(push, 1)
struct PdfBinHeader {
    uint8_t  magic;
    uint8_t  version;
    uint16_t width;
    uint16_t height;
    uint16_t reserved;
};
#pragma pack(pop)

class PdfStore {
public:
    static constexpr uint8_t  BIN_MAGIC    = 0xE5;
    static constexpr uint8_t  BIN_VERSION  = 0x01;
    static constexpr size_t   HEADER_BYTES = sizeof(PdfBinHeader);

    static constexpr uint16_t VIEWPORT_W = cfg::display::CONTENT_W;
    static constexpr uint16_t VIEWPORT_H = cfg::display::CONTENT_H;
    static constexpr size_t   VIEWPORT_BYTES =
        (((VIEWPORT_W + 7) / 8) * VIEWPORT_H);

    void begin(SdModule* sd) { sd_ = sd; }

    bool listDocs(std::vector<PdfDoc>& out);

    bool readPageViewport(const String& doc, uint16_t pageIdx,
                          uint8_t* outBuf, size_t bufLen,
                          uint16_t& outPageW, uint16_t& outPageH);

private:
    SdModule* sd_ = nullptr;
};

} // namespace modules
