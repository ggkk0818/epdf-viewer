#include "BleDataTransport.h"
#include "BleModule.h"
#include "PdfStore.h"
#include "SdModule.h"
#include "../config/Config.h"

#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace modules {

namespace {

constexpr size_t PREVIEW_CHUNK = 4096;

} // namespace

BleDataTransport::UploadStartResult
BleDataTransport::startUpload(SdModule* sd, const String& dirName) {
    if (upload_.isActive() || previewFile_) return UploadStartResult::Busy;

    if (!upload_.open(sd, dirName)) {
        switch (upload_.lastError()) {
            case UploadSession::Error::BadHeader: return UploadStartResult::BadDirName;
            case UploadSession::Error::SdError:   return UploadStartResult::SdError;
            default:                              return UploadStartResult::SdError;
        }
    }
    return UploadStartResult::Ready;
}

BleDataTransport::UploadEndResult
BleDataTransport::endUpload(uint16_t& outPagesReceived) {
    if (!upload_.isActive()) return UploadEndResult::NotActive;

    bool ok = upload_.close(outPagesReceived);
    if (!ok) {
        // close() returns false on IO error during flush; lastError preserves the cause.
        return (upload_.lastError() == UploadSession::Error::IoError)
               ? UploadEndResult::IoError
               : UploadEndResult::Ok;  // benign: no error recorded
    }
    return UploadEndResult::Ok;
}

BleDataTransport::PreviewStartResult
BleDataTransport::startPreview(PdfStore* pdf, const String& dirName,
                                uint16_t pageIdx, size_t& outTotalBytes,
                                uint16_t& outWidth, uint16_t& outHeight) {
    if (upload_.isActive() || previewFile_) return PreviewStartResult::Busy;

    size_t fileSize = 0;
    File f = pdf->openPageFile(dirName, pageIdx, fileSize);
    if (!f) return PreviewStartResult::OpenFailed;
    if (fileSize < PdfStore::HEADER_BYTES) {
        f.close();
        return PreviewStartResult::OpenFailed;
    }

    // Read header to learn dimensions (sent back in preview_end).
    uint8_t hdr[PdfStore::HEADER_BYTES];
    if (f.read(hdr, PdfStore::HEADER_BYTES) != (int)PdfStore::HEADER_BYTES) {
        f.close();
        return PreviewStartResult::OpenFailed;
    }
    uint8_t magic   = hdr[0];
    uint8_t version = hdr[1];
    uint16_t w = (uint16_t)hdr[2] | ((uint16_t)hdr[3] << 8);
    uint16_t h = (uint16_t)hdr[4] | ((uint16_t)hdr[5] << 8);
    if (magic != PdfStore::BIN_MAGIC || version != PdfStore::BIN_VERSION) {
        f.close();
        return PreviewStartResult::OpenFailed;
    }

    // Rewind so streamPreview sends the header too (App needs it to decode).
    f.seek(0);

    previewFile_        = f;
    previewPageAtStart_ = pageIdx;
    outTotalBytes       = fileSize;
    outWidth            = w;
    outHeight           = h;
    return PreviewStartResult::Ready;
}

bool BleDataTransport::streamPreview(BleModule* ble, size_t totalBytes,
                                      uint16_t pageIdx) {
    (void)pageIdx;
    if (!previewFile_) return false;
    if (!ble) { previewFile_.close(); return false; }

    uint8_t* buf = (uint8_t*)malloc(PREVIEW_CHUNK);
    if (!buf) {
        log_e("preview: malloc %u failed", (unsigned)PREVIEW_CHUNK);
        previewFile_.close();
        return false;
    }

    size_t sent = 0;
    bool ok = true;
    while (sent < totalBytes) {
        size_t want = (totalBytes - sent < PREVIEW_CHUNK)
                      ? (totalBytes - sent) : PREVIEW_CHUNK;
        int got = previewFile_.read(buf, want);
        if (got <= 0) { ok = false; break; }
        ble->notifyData(buf, (size_t)got);
        sent += (size_t)got;
        // Yield to let the BLE stack drain the notify queue. Without this,
        // back-to-back notifies can overflow NimBLE's internal queue.
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    free(buf);
    previewFile_.close();
    return ok;
}

void BleDataTransport::abort() {
    if (upload_.isActive()) {
        uint16_t dummy = 0;
        upload_.close(dummy);
    }
    if (previewFile_) {
        previewFile_.close();
    }
}

bool BleDataTransport::onDataChunk(const uint8_t* data, size_t len,
                                    bool& outPageComplete, uint16_t& outPageIdx,
                                    size_t& outPageBytes) {
    outPageComplete = false;
    outPageIdx      = 0;
    outPageBytes    = 0;

    if (!upload_.isActive()) return false;

    uint16_t beforePage = upload_.currentPageIdx();
    bool ok = upload_.writeBytes(data, len);
    if (!ok) return false;

    uint16_t afterPage = upload_.currentPageIdx();
    if (afterPage != beforePage) {
        outPageComplete = true;
        outPageIdx      = beforePage + 1;  // page that just finished (1-based)
        outPageBytes    = upload_.lastCompletedPageBytes();
    }
    return true;
}

} // namespace modules
