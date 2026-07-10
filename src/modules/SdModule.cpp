#include "SdModule.h"
#include "../config/Config.h"

namespace modules {

bool SdModule::begin() {
    SPI.begin(cfg::pin::SD_SCK, cfg::pin::SD_MISO, cfg::pin::SD_MOSI, cfg::pin::SD_CS);
    if (!SD.begin(cfg::pin::SD_CS, SPI, cfg::display::SPI_HZ)) {
        log_e("SD card mount failed");
        mounted_ = false;
        return false;
    }
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        log_e("No SD card attached");
        mounted_ = false;
        return false;
    }
    log_i("SD mounted, type=%u, size=%llu MB", cardType, (uint64_t)SD.cardSize() / (1024 * 1024));
    mounted_ = true;
    return true;
}

bool SdModule::listDirs(const String& path, std::vector<String>& outDirs) {
    if (!mounted_) return false;
    File root = SD.open(path);
    if (!root || !root.isDirectory()) {
        log_w("listDirs: path not a directory: %s", path.c_str());
        return false;
    }
    File entry;
    while ((entry = root.openNextFile())) {
        if (entry.isDirectory()) {
            String name = entry.name();
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            if (name.length() > 0 && !name.startsWith(".")) {
                outDirs.push_back(name);
            }
        }
        entry.close();
    }
    root.close();
    return true;
}

bool SdModule::listFiles(const String& path, const String& suffix, std::vector<String>& outFiles) {
    if (!mounted_) return false;
    File root = SD.open(path);
    if (!root || !root.isDirectory()) return false;
    File entry;
    while ((entry = root.openNextFile())) {
        if (!entry.isDirectory()) {
            String name = entry.name();
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            if (suffix.length() == 0 || name.endsWith(suffix)) {
                outFiles.push_back(name);
            }
        }
        entry.close();
    }
    root.close();
    return true;
}

File SdModule::openFile(const String& path, const char* mode) {
    if (!mounted_) return File();
    return SD.open(path, mode);
}

bool SdModule::readFile(const String& path, uint8_t* buf, size_t len) {
    if (!mounted_ || !buf || len == 0) return false;
    File f = SD.open(path, FILE_READ);
    if (!f) {
        log_w("readFile: cannot open %s", path.c_str());
        return false;
    }
    size_t got = f.read(buf, len);
    f.close();
    if (got != len) {
        log_w("readFile: short read %u/%u for %s", (unsigned)got, (unsigned)len, path.c_str());
        return false;
    }
    return true;
}

bool SdModule::exists(const String& path) {
    if (!mounted_) return false;
    return SD.exists(path);
}

bool SdModule::mkdir(const String& path) {
    if (!mounted_) return false;
    if (SD.exists(path)) return true;
    return SD.mkdir(path);
}

bool SdModule::removeFile(const String& path) {
    if (!mounted_) return false;
    return SD.remove(path);
}

bool SdModule::rmdirRecursive(const String& path) {
    if (!mounted_) return false;
    File root = SD.open(path);
    if (!root) return false;
    if (!root.isDirectory()) {
        root.close();
        return SD.remove(path);
    }
    File entry;
    while ((entry = root.openNextFile())) {
        String name = entry.name();
        bool isDir = entry.isDirectory();
        entry.close();
        if (isDir) {
            rmdirRecursive(name);
        } else {
            SD.remove(name);
        }
    }
    root.close();
    return SD.rmdir(path);
}

uint64_t SdModule::totalBytes() const {
    if (!mounted_) return 0;
    return SD.totalBytes();
}

uint64_t SdModule::usedBytes() const {
    if (!mounted_) return 0;
    return SD.usedBytes();
}

} // namespace modules
