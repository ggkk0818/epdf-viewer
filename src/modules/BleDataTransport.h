#pragma once

#include <Arduino.h>
#include <SD.h>
#include "../config/Config.h"
#include "UploadSession.h"

namespace modules {

class BleModule;
class SdModule;
class PdfStore;

// Owns the binary state machines for upload (App→ESP32 via data characteristic)
// and preview (ESP32→App via data characteristic notify). Both share the same
// data characteristic so they cannot run concurrently — callers must check
// isBusy() before starting either.
//
// All public methods are safe to call from the BLE host task or a work task.
// Upload data is fed synchronously from BLE onWrite callbacks through a 4KB
// SD write coalescing buffer, so brief BLE host blocking is expected.
class BleDataTransport {
public:
    enum class UploadStartResult {
        Ready,
        BadDirName,
        SdError,
        Busy,          // a preview or another upload is active
    };

    enum class UploadEndResult {
        Ok,
        NotActive,
        PartialPage,  // session closed but last page was incomplete
        IoError,
    };

    enum class PreviewStartResult {
        Ready,
        Busy,
        BadDirName,
        PageOutOfRange,
        OpenFailed,
    };

    // Called by BleCmdDispatcher on the work task.
    UploadStartResult   startUpload(SdModule* sd, const String& dirName);
    UploadEndResult     endUpload(uint16_t& outPagesReceived);

    PreviewStartResult  startPreview(PdfStore* pdf,
                                      const String& dirName,
                                      uint16_t pageIdx,
                                      size_t& outTotalBytes,
                                      uint16_t& outWidth,
                                      uint16_t& outHeight);
    // Streams the opened preview file via MTU-sized BleModule::notifyData
    // notifications until EOF. Runs synchronously — caller (work task) blocks until done.
    // Returns false on IO error mid-stream.
    bool                streamPreview(BleModule* ble,
                                       size_t totalBytes,
                                       uint16_t pageIdx);

    // Abort any in-flight upload/preview (e.g., on BLE disconnect).
    void                abort();

    // Called from BleModule's data write callback (BLE host task).
    // Returns true if a page boundary was crossed (dispatcher should send page_ack).
    bool                onDataChunk(const uint8_t* data, size_t len,
                                     bool& outPageComplete, uint16_t& outPageIdx,
                                     size_t& outPageBytes);

    bool                isUploading()  const { return upload_.isActive(); }
    bool                isPreviewing() const { return previewFile_; }

private:
    UploadSession  upload_;
    File           previewFile_;
    uint16_t       previewPageAtStart_ = 0;
};

} // namespace modules
