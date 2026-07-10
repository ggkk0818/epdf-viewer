#include "UploadSession.h"
#include "SdModule.h"
#include "PdfStore.h"
#include "../config/Config.h"
#include <SD.h>

namespace modules {

UploadSession::~UploadSession() {
    if (phase_ != Phase::Idle) {
        uint16_t dummy = 0;
        close(dummy);
    }
}

bool UploadSession::open(SdModule* sd, const String& dirName) {
    if (phase_ != Phase::Idle) {
        error_ = Error::NotActive;
        return false;
    }
    if (!sd || dirName.length() == 0) {
        error_ = Error::SdError;
        return false;
    }

    // Refuse to open a session for a dir name that doesn't match the canonical
    // format — guards against accidental writes to arbitrary paths.
    PdfMeta probe;
    if (!PdfStore::parseDirName(dirName, probe)) {
        error_ = Error::BadHeader;
        return false;
    }

    sd_ = sd;
    dirName_ = dirName;

    String fullPath = String(cfg::fs::PDF_DIR) + "/" + dirName_;
    if (!sd_->mkdir(fullPath)) {
        error_ = Error::SdError;
        return false;
    }

    phase_         = Phase::Header;
    headerHave_    = 0;
    bodyNeed_      = 0;
    bodyHave_      = 0;
    pageIdx_       = 0;
    pagesReceived_ = 0;
    writeBufHave_  = 0;
    error_         = Error::None;
    return true;
}

bool UploadSession::flushWriteBuf() {
    if (writeBufHave_ == 0) return true;
    size_t n = currentFile_.write(writeBuf_, writeBufHave_);
    if (n != writeBufHave_) {
        error_ = Error::IoError;
        writeBufHave_ = 0;
        return false;
    }
    writeBufHave_ = 0;
    return true;
}

bool UploadSession::startPage() {
    PdfBinHeader h;
    h.magic    = headerBuf_[0];
    h.version  = headerBuf_[1];
    h.width    = (uint16_t)headerBuf_[2] | ((uint16_t)headerBuf_[3] << 8);
    h.height   = (uint16_t)headerBuf_[4] | ((uint16_t)headerBuf_[5] << 8);
    h.reserved = (uint16_t)headerBuf_[6] | ((uint16_t)headerBuf_[7] << 8);

    if (h.magic != PdfStore::BIN_MAGIC || h.version != PdfStore::BIN_VERSION) {
        log_w("UploadSession: bad magic/version %02X/%02X", h.magic, h.version);
        error_ = Error::BadHeader;
        return false;
    }
    if (h.width == 0 || h.height == 0 ||
        h.width > cfg::pdf::MAX_PAGE_DIM_W ||
        h.height > cfg::pdf::MAX_PAGE_DIM_H) {
        log_w("UploadSession: bad dims %ux%u", h.width, h.height);
        error_ = Error::BadHeader;
        return false;
    }

    pageWidth_  = h.width;
    pageHeight_ = h.height;
    bodyNeed_   = (size_t)((h.width + 7) / 8) * h.height;
    if (PdfStore::HEADER_BYTES + bodyNeed_ > cfg::pdf::MAX_PAGE_BYTES) {
        log_w("UploadSession: page too big %u bytes",
              (unsigned)(PdfStore::HEADER_BYTES + bodyNeed_));
        error_ = Error::PageTooBig;
        return false;
    }
    bodyHave_ = 0;

    String path = String(cfg::fs::PDF_DIR) + "/" + dirName_ + "/" +
                  PdfStore::makePageFileName(pageIdx_);
    currentFile_ = SD.open(path, FILE_WRITE);
    if (!currentFile_) {
        log_w("UploadSession: cannot create %s", path.c_str());
        error_ = Error::SdError;
        return false;
    }

    if (currentFile_.write(headerBuf_, PdfStore::HEADER_BYTES) != PdfStore::HEADER_BYTES) {
        currentFile_.close();
        SD.remove(path);
        error_ = Error::IoError;
        return false;
    }
    return true;
}

bool UploadSession::finishPage() {
    if (!flushWriteBuf()) return false;
    currentFile_.close();
    lastPageBytes_ = PdfStore::HEADER_BYTES + bodyHave_;
    pageIdx_++;
    pagesReceived_++;

    phase_      = Phase::Header;
    headerHave_ = 0;
    bodyHave_   = 0;
    bodyNeed_   = 0;
    return true;
}

bool UploadSession::writeBytes(const uint8_t* data, size_t len) {
    if (phase_ == Phase::Idle) {
        error_ = Error::NotActive;
        return false;
    }

    while (len > 0) {
        if (phase_ == Phase::Header) {
            size_t need = PdfStore::HEADER_BYTES - headerHave_;
            size_t take = (len < need) ? len : need;
            memcpy(headerBuf_ + headerHave_, data, take);
            headerHave_ += take;
            data += take;
            len  -= take;

            if (headerHave_ == PdfStore::HEADER_BYTES) {
                if (!startPage()) {
                    phase_ = Phase::Idle;
                    return false;
                }
                phase_ = Phase::Body;
            }
            continue;
        }

        // Phase::Body
        size_t need = bodyNeed_ - bodyHave_;
        size_t take = (len < need) ? len : need;

        const uint8_t* src = data;
        size_t left = take;
        while (left > 0) {
            size_t space = cfg::pdf::SD_WRITE_BUF - writeBufHave_;
            size_t copy  = (left < space) ? left : space;
            memcpy(writeBuf_ + writeBufHave_, src, copy);
            writeBufHave_ += copy;
            src  += copy;
            left -= copy;
            if (writeBufHave_ == cfg::pdf::SD_WRITE_BUF) {
                if (!flushWriteBuf()) {
                    phase_ = Phase::Idle;
                    return false;
                }
            }
        }

        data      += take;
        len       -= take;
        bodyHave_ += take;

        if (bodyHave_ == bodyNeed_) {
            if (!finishPage()) {
                phase_ = Phase::Idle;
                return false;
            }
        }
    }
    return true;
}

bool UploadSession::close(uint16_t& outPagesReceived) {
    outPagesReceived = pagesReceived_;
    if (phase_ == Phase::Idle) return error_ == Error::None;

    bool ok = true;
    if (phase_ == Phase::Body) {
        // Partial page — flush what we have, but don't count it as received.
        // The half-written file is left on disk for inspection; caller can
        // re-upload this doc (mkdir is idempotent, page files overwrite).
        if (currentFile_) {
            ok = flushWriteBuf();
            currentFile_.close();
        }
    }

    phase_ = Phase::Idle;
    return ok && (error_ == Error::None);
}

} // namespace modules
