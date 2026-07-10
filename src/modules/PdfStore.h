#pragma once

#include <Arduino.h>
#include <SD.h>
#include <vector>
#include "../config/Config.h"

namespace modules {

class SdModule;

struct PdfMeta {
    String   name;     // human-readable doc name (last segment of dir)
    String   time;     // "yyyy-mm-dd HH:MM:SS" for display
    uint16_t pages;    // declared page count (from dir name); 0 if unknown
    String   dirName;  // canonical dir name on SD ("yyyy-mm-dd_HH-MM-SS_PPP_name")
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

    void begin(SdModule* sd);

    bool listDocs(std::vector<PdfMeta>& out);

    bool readPageViewport(const String& dirName, uint16_t pageIdx,
                          uint8_t* outBuf, size_t bufLen,
                          uint16_t& outPageW, uint16_t& outPageH);

    bool deleteDoc(const String& dirName);

    // Open a page file for binary streaming (preview). On success, outSize is
    // the total file size (header + body). Caller owns the returned File and
    // must close it.
    File openPageFile(const String& dirName, uint16_t pageIdx, size_t& outSize);

    // Parse "yyyy-mm-dd_HH-MM-SS_PPP_name" → PdfMeta. Returns false if the
    // string does not match the canonical format (used to filter listDocs).
    static bool parseDirName(const String& dir, PdfMeta& out);

    static String makePageFileName(uint16_t pageIdx);

private:
    SdModule* sd_ = nullptr;
};

} // namespace modules
