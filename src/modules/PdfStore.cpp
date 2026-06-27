#include "PdfStore.h"
#include "SdModule.h"
#include "../config/Config.h"

namespace modules {

namespace {

String pagePath(const String& doc, uint16_t pageIdx) {
    char num[8];
    snprintf(num, sizeof(num), "%03u", (unsigned)(pageIdx + 1));
    String p = String(cfg::fs::PDF_DIR);
    p += "/";
    p += doc;
    p += "/";
    p += num;
    p += cfg::fs::PAGE_SUFFIX;
    return p;
}

} // namespace

bool PdfStore::listDocs(std::vector<PdfDoc>& out) {
    if (!sd_) return false;
    std::vector<String> dirs;
    if (!sd_->listDirs(cfg::fs::PDF_DIR, dirs)) return false;

    for (const String& d : dirs) {
        String path = String(cfg::fs::PDF_DIR) + "/" + d;
        std::vector<String> files;
        sd_->listFiles(path, cfg::fs::PAGE_SUFFIX, files);
        if (files.empty()) continue;
        PdfDoc doc;
        doc.name = d;
        doc.pageCount = (uint16_t)files.size();
        out.push_back(doc);
    }
    return true;
}

bool PdfStore::readPage(const String& doc, uint16_t pageIdx, uint8_t* buf, size_t len) {
    if (!sd_ || !buf || len != PAGE_BYTES) {
        log_w("PdfStore.readPage invalid args");
        return false;
    }
    String path = pagePath(doc, pageIdx);
    return sd_->readFile(path, buf, len);
}

} // namespace modules
