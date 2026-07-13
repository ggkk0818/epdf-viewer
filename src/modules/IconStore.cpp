#include "IconStore.h"
#include "SdModule.h"
#include "../config/Config.h"
#include <esp_heap_caps.h>
#include <SD.h>

namespace modules {

namespace {

bool readFully(File& f, uint8_t* buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        int r = f.read(buf + got, len - got);
        if (r <= 0) return false;
        got += r;
    }
    return true;
}

} // namespace

bool IconStore::begin() {
    File root = SD.open(cfg::fs::ICON_DIR);
    if (!root || !root.isDirectory()) {
        log_w("IconStore: icon dir missing: %s", cfg::fs::ICON_DIR);
        return false;
    }

    File entry;
    while ((entry = root.openNextFile()) && count_ < MAX_ICONS) {
        if (entry.isDirectory()) { entry.close(); continue; }
        String full = entry.name();
        String name = full;
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        if (!name.endsWith(cfg::fs::PAGE_SUFFIX)) { entry.close(); continue; }
        name = name.substring(0, name.length() - 4);

        size_t fileSize = entry.size();
        if (fileSize < 4) { entry.close(); continue; }

        uint8_t header[4];
        if (!readFully(entry, header, 4)) { entry.close(); continue; }
        uint16_t w = header[0] | ((uint16_t)header[1] << 8);
        uint16_t h = header[2] | ((uint16_t)header[3] << 8);
        size_t bytes = ((w + 7) / 8) * h;
        if (fileSize < 4 + bytes) {
            log_w("Icon %s truncated: have %u, need %u",
                  name.c_str(), (unsigned)fileSize, (unsigned)(4 + bytes));
            entry.close();
            continue;
        }

        uint8_t* data = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
        if (!data) {
            log_e("Icon %s: PSRAM alloc failed (%u bytes)", name.c_str(), (unsigned)bytes);
            entry.close();
            continue;
        }
        if (!readFully(entry, data, bytes)) {
            log_e("Icon %s: body read failed", name.c_str());
            free(data);
            entry.close();
            continue;
        }

        IconEntry& slot = entries_[count_++];
        memset(slot.name, 0, sizeof(slot.name));
        strncpy(slot.name, name.c_str(), sizeof(slot.name) - 1);
        slot.width = w;
        slot.height = h;
        slot.data = data;
        entry.close();
    }
    root.close();
    log_i("IconStore loaded %u icons", (unsigned)count_);
    return true;
}

const IconEntry* IconStore::getIcon(const char* name) const {
    for (size_t i = 0; i < count_; i++) {
        if (strncmp(entries_[i].name, name, sizeof(IconEntry::name)) == 0) {
            return &entries_[i];
        }
    }
    return nullptr;
}

} // namespace modules
