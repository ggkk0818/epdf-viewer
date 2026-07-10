#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <vector>

namespace modules {

class SdModule {
public:
    bool begin();

    bool listDirs(const String& path, std::vector<String>& outDirs);
    bool listFiles(const String& path, const String& suffix, std::vector<String>& outFiles);

    File openFile(const String& path, const char* mode = FILE_READ);
    bool readFile(const String& path, uint8_t* buf, size_t len);
    bool exists(const String& path);

    bool mkdir(const String& path);
    bool removeFile(const String& path);
    // Remove a directory and everything inside it (files + subdirs).
    bool rmdirRecursive(const String& path);

    uint64_t totalBytes() const;
    uint64_t usedBytes() const;

    bool isMounted() const { return mounted_; }

private:
    bool mounted_ = false;
};

} // namespace modules
