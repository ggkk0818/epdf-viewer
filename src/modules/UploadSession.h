#pragma once

#include <Arduino.h>
#include <SD.h>
#include "../config/Config.h"
#include "PdfStore.h"

namespace modules {

class SdModule;

// Streaming upload state machine. Receives raw bytes (header + body of each
// page back-to-back) via writeBytes(), validates the 8-byte header per page,
// and writes the body to SD through a 4KB coalescing buffer. RAM usage is
// independent of page size, so any future resolution (e.g. 640x960) works
// without code changes.
class UploadSession {
public:
    enum class Error {
        None,
        NotActive,
        SdError,      // mkdir / open failed
        BadHeader,    // magic/version/dimensions invalid
        PageTooBig,   // body exceeds MAX_PAGE_BYTES
        IoError,      // SD write/flush failed
    };

    UploadSession() = default;
    ~UploadSession();

    // Open a fresh session for `dirName` under /pdf. Creates the dir if absent.
    // If the dir already exists (re-upload after a failed session), individual
    // page files will be overwritten.
    bool open(SdModule* sd, const String& dirName);

    // Feed raw bytes. The caller is responsible for ensuring the byte stream
    // is a concatenation of (8-byte header + 1bpp body) per page, in order.
    // Returns false on any error; subsequent calls are no-ops.
    bool writeBytes(const uint8_t* data, size_t len);

    // Finalize the session. If a page was in progress, it is discarded (caller
    // learns pagesReceived and can decide whether to keep the partial upload).
    // Returns false if any IO error occurred during the session.
    bool close(uint16_t& outPagesReceived);

    bool     isActive()        const { return phase_ != Phase::Idle; }
    uint16_t currentPageIdx()  const { return pageIdx_; }
    Error    lastError()       const { return error_; }

    // Total bytes (header + body) of the page most recently finished via
    // writeBytes. Valid only after currentPageIdx() has incremented.
    size_t   lastCompletedPageBytes() const { return lastPageBytes_; }

    // Diagnostic: bytes consumed for the current (in-progress) page.
    size_t   currentPageBytes() const {
        return (phase_ == Phase::Body) ? PdfStore::HEADER_BYTES + bodyHave_
                                       : headerHave_;
    }

private:
    enum class Phase { Idle, Header, Body };

    bool startPage();    // validate headerBuf_, open file, write header
    bool finishPage();   // flush + close + advance pageIdx
    bool flushWriteBuf();

    SdModule* sd_         = nullptr;
    String    dirName_;
    File      currentFile_;

    Phase     phase_       = Phase::Idle;
    uint8_t   headerBuf_[PdfStore::HEADER_BYTES] = {0};
    size_t    headerHave_  = 0;
    uint16_t  pageWidth_   = 0;
    uint16_t  pageHeight_  = 0;
    size_t    bodyNeed_     = 0;
    size_t    bodyHave_     = 0;
    uint16_t  pageIdx_      = 0;
    uint16_t  pagesReceived_ = 0;

    uint8_t   writeBuf_[cfg::pdf::SD_WRITE_BUF] = {0};
    size_t    writeBufHave_ = 0;

    size_t    lastPageBytes_ = 0;
    Error     error_ = Error::None;
};

} // namespace modules
