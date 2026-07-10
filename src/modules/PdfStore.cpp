#include "PdfStore.h"
#include "SdModule.h"
#include "../config/Config.h"
#include <SD.h>

namespace modules {

namespace {

constexpr size_t MAX_PAGE_ROW_BYTES = 256;  // pages up to ~2048 px wide

inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

String pagePath(const String& dirName, uint16_t pageIdx) {
    String p = String(cfg::fs::PDF_DIR);
    p += "/";
    p += dirName;
    p += "/";
    p += PdfStore::makePageFileName(pageIdx);
    return p;
}

bool readHeader(File& f, PdfBinHeader& h) {
    uint8_t buf[PdfStore::HEADER_BYTES];
    if (f.read(buf, PdfStore::HEADER_BYTES) != (int)PdfStore::HEADER_BYTES) {
        return false;
    }
    h.magic    = buf[0];
    h.version  = buf[1];
    h.width    = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
    h.height   = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
    h.reserved = (uint16_t)buf[6] | ((uint16_t)buf[7] << 8);
    return true;
}

// Blit `src_byte_len` source bytes (MSB-first, bit 0 = leftmost) into dst_row
// starting at bit `dst_x`. dst_row is viewport-row byte buffer of dst_row_bytes.
void blitRow(uint8_t* dst_row, size_t dst_row_bytes,
             uint16_t dst_x,
             const uint8_t* src, size_t src_byte_len) {
    size_t byte_off = dst_x / 8;
    size_t bit_off  = dst_x & 7;

    if (byte_off >= dst_row_bytes) return;

    if (bit_off == 0) {
        size_t copy = src_byte_len;
        if (byte_off + copy > dst_row_bytes) copy = dst_row_bytes - byte_off;
        for (size_t i = 0; i < copy; i++) {
            dst_row[byte_off + i] |= src[i];
        }
    } else {
        uint8_t hi_shift = (uint8_t)bit_off;
        uint8_t lo_shift = (uint8_t)(8 - bit_off);
        for (size_t i = 0; i < src_byte_len; i++) {
            if (byte_off + i >= dst_row_bytes) break;
            uint8_t b = src[i];
            dst_row[byte_off + i] |= (b >> hi_shift);
            if (byte_off + i + 1 < dst_row_bytes) {
                dst_row[byte_off + i + 1] |= (b << lo_shift);
            }
        }
    }
}

} // namespace

// static
bool PdfStore::parseDirName(const String& dir, PdfMeta& out) {
    // Format: yyyy-mm-dd_HH-MM-SS_PPP_name
    // Positions:  0-3 year | 4 '-' | 5-6 mon | 7 '-' | 8-9 day | 10 '_'
    //            11-12 H | 13 '-' | 14-15 M | 16 '-' | 17-18 S | 19 '_'
    //            20-22 pages | 23 '_' | 24+ name
    const size_t len = dir.length();
    if (len < 25) return false;

    auto check2 = [&](int p) -> bool {
        return isDigit(dir[p]) && isDigit(dir[p + 1]);
    };
    auto check4 = [&](int p) -> bool {
        return isDigit(dir[p]) && isDigit(dir[p + 1])
            && isDigit(dir[p + 2]) && isDigit(dir[p + 3]);
    };

    if (!check4(0)  || dir[4]  != '-' || !check2(5)  || dir[7]  != '-' || !check2(8)  || dir[10] != '_') return false;
    if (!check2(11) || dir[13] != '-' || !check2(14) || dir[16] != '-' || !check2(17) || dir[19] != '_') return false;
    if (!check2(20) || !isDigit(dir[22]) || dir[23] != '_') return false;

    out.dirName = dir;
    out.time = dir.substring(0, 10) + " " +
               dir.substring(11, 13) + ":" +
               dir.substring(14, 16) + ":" +
               dir.substring(17, 19);
    out.pages = (uint16_t)((dir[20] - '0') * 100 +
                           (dir[21] - '0') * 10 +
                           (dir[22] - '0'));
    out.name = dir.substring(24);
    return out.name.length() > 0;
}

// static
String PdfStore::makePageFileName(uint16_t pageIdx) {
    char num[8];
    snprintf(num, sizeof(num), "%03u", (unsigned)(pageIdx + 1));
    return String(num) + cfg::fs::PAGE_SUFFIX;
}

void PdfStore::begin(SdModule* sd) {
    sd_ = sd;
    if (sd_ && sd_->isMounted()) {
        if (!sd_->exists(cfg::fs::PDF_DIR)) {
            sd_->mkdir(cfg::fs::PDF_DIR);
        }
    }
}

bool PdfStore::listDocs(std::vector<PdfMeta>& out) {
    if (!sd_) return false;
    std::vector<String> dirs;
    if (!sd_->listDirs(cfg::fs::PDF_DIR, dirs)) return false;

    for (const String& d : dirs) {
        PdfMeta m;
        if (!parseDirName(d, m)) continue;  // skip non-conforming dirs (e.g., legacy "example")
        out.push_back(m);
    }
    return true;
}

bool PdfStore::readPageViewport(const String& dirName, uint16_t pageIdx,
                                uint8_t* outBuf, size_t bufLen,
                                uint16_t& outPageW, uint16_t& outPageH) {
    if (!sd_ || !outBuf || bufLen != VIEWPORT_BYTES) {
        log_w("PdfStore.readPageViewport invalid args");
        return false;
    }

    String path = pagePath(dirName, pageIdx);
    File f = sd_->openFile(path, FILE_READ);
    if (!f) {
        log_w("PdfStore: cannot open %s", path.c_str());
        return false;
    }

    PdfBinHeader h;
    if (!readHeader(f, h)) {
        log_w("PdfStore: header read failed %s", path.c_str());
        f.close();
        return false;
    }
    if (h.magic != BIN_MAGIC || h.version != BIN_VERSION) {
        log_w("PdfStore: bad magic/version %s (got %02X/%02X)",
              path.c_str(), h.magic, h.version);
        f.close();
        return false;
    }
    if (h.width == 0 || h.height == 0) {
        log_w("PdfStore: zero-sized page %s", path.c_str());
        f.close();
        return false;
    }

    uint16_t page_w = h.width;
    uint16_t page_h = h.height;
    size_t page_row_bytes = (size_t)((page_w + 7) / 8);
    if (page_row_bytes > MAX_PAGE_ROW_BYTES) {
        log_w("PdfStore: page too wide %s (%u bytes/row)",
              path.c_str(), (unsigned)page_row_bytes);
        f.close();
        return false;
    }

    outPageW = page_w;
    outPageH = page_h;

    // Determine visible region within viewport (clip if larger, center if smaller).
    uint16_t draw_w = (page_w >= VIEWPORT_W) ? VIEWPORT_W : page_w;
    uint16_t draw_h = (page_h >= VIEWPORT_H) ? VIEWPORT_H : page_h;
    uint16_t dst_x  = (page_w >= VIEWPORT_W) ? 0
                                              : (uint16_t)((VIEWPORT_W - page_w) / 2);
    uint16_t dst_y  = (page_h >= VIEWPORT_H) ? 0
                                              : (uint16_t)((VIEWPORT_H - page_h) / 2);

    const size_t viewport_row_bytes = (VIEWPORT_W + 7) / 8;
    memset(outBuf, 0, VIEWPORT_BYTES);

    uint8_t src_row[MAX_PAGE_ROW_BYTES];
    size_t src_byte_len = (draw_w + 7) / 8;

    for (uint16_t y = 0; y < draw_h; y++) {
        size_t file_off = HEADER_BYTES + (size_t)y * page_row_bytes;
        if (!f.seek(file_off)) {
            log_w("PdfStore: seek failed @%u for %s",
                  (unsigned)file_off, path.c_str());
            f.close();
            return false;
        }
        if (f.read(src_row, src_byte_len) != (int)src_byte_len) {
            log_w("PdfStore: row read failed y=%u for %s",
                  (unsigned)y, path.c_str());
            f.close();
            return false;
        }
        uint8_t* dst_row = outBuf + (size_t)(dst_y + y) * viewport_row_bytes;
        blitRow(dst_row, viewport_row_bytes, dst_x, src_row, src_byte_len);
    }

    f.close();
    return true;
}

bool PdfStore::deleteDoc(const String& dirName) {
    if (!sd_ || dirName.length() == 0) return false;
    PdfMeta tmp;
    if (!parseDirName(dirName, tmp)) return false;  // refuse to delete arbitrary paths
    String path = String(cfg::fs::PDF_DIR) + "/" + dirName;
    return sd_->rmdirRecursive(path);
}

File PdfStore::openPageFile(const String& dirName, uint16_t pageIdx, size_t& outSize) {
    outSize = 0;
    if (!sd_) return File();
    String path = pagePath(dirName, pageIdx);
    File f = sd_->openFile(path, FILE_READ);
    if (!f) return File();
    outSize = f.size();
    return f;
}

} // namespace modules
