#include "BleCmdDispatcher.h"
#include "BleModule.h"
#include "PdfStore.h"
#include "SdModule.h"
#include "BatteryModule.h"
#include "../config/Config.h"

#include <ArduinoJson.h>
#include <cstring>

namespace modules {

namespace {

// Maps BleDataTransport::UploadStartResult to a status string for upload_ack.
const char* uploadStartStatusStr(BleDataTransport::UploadStartResult r) {
    switch (r) {
        case BleDataTransport::UploadStartResult::Ready:      return "ready";
        case BleDataTransport::UploadStartResult::BadDirName: return "bad_dir_name";
        case BleDataTransport::UploadStartResult::SdError:    return "sd_error";
        case BleDataTransport::UploadStartResult::Busy:       return "busy";
    }
    return "error";
}

const char* previewStartStatusStr(BleDataTransport::PreviewStartResult r) {
    switch (r) {
        case BleDataTransport::PreviewStartResult::Ready:           return "ready";
        case BleDataTransport::PreviewStartResult::Busy:            return "busy";
        case BleDataTransport::PreviewStartResult::BadDirName:      return "bad_dir_name";
        case BleDataTransport::PreviewStartResult::PageOutOfRange:  return "out_of_range";
        case BleDataTransport::PreviewStartResult::OpenFailed:      return "open_failed";
    }
    return "error";
}

} // namespace

bool BleCmdDispatcher::begin(BleModule* ble, PdfStore* pdf, SdModule* sd,
                              BatteryModule* battery, BleDataTransport* transport) {
    ble_       = ble;
    pdf_       = pdf;
    sd_        = sd;
    battery_   = battery;
    transport_ = transport;
    if (!ble_ || !pdf_ || !sd_ || !battery_ || !transport_) return false;

    queue_ = xQueueCreate(cfg::ble::CMD_QUEUE_LEN, sizeof(WorkMsg));
    return queue_ != nullptr;
}

bool BleCmdDispatcher::start() {
    if (!queue_) return false;

    // Wire BLE callbacks → trampolines.
    ble_->onCmdLine(&BleCmdDispatcher::cmdLineTrampoline, this);
    ble_->onDataChunk(&BleCmdDispatcher::dataChunkTrampoline, this);
    ble_->onConnect(&BleCmdDispatcher::connectTrampoline, this);

    BaseType_t ok = xTaskCreatePinnedToCore(
        &BleCmdDispatcher::taskTrampoline,
        "bleWork",
        cfg::ble::WORK_STACK,
        this,
        cfg::ble::WORK_PRIO,
        &task_,
        cfg::ble::WORK_CORE);
    return ok == pdPASS;
}

// ---- Trampolines ----

void BleCmdDispatcher::cmdLineTrampoline(const String& line, void* ctx) {
    static_cast<BleCmdDispatcher*>(ctx)->enqueueLine(line);
}

void BleCmdDispatcher::dataChunkTrampoline(const uint8_t* data, size_t len, void* ctx) {
    static_cast<BleCmdDispatcher*>(ctx)->onDataChunk(data, len);
}

void BleCmdDispatcher::connectTrampoline(bool connected, void* ctx) {
    static_cast<BleCmdDispatcher*>(ctx)->onConnect(connected);
}

void BleCmdDispatcher::taskTrampoline(void* ctx) {
    static_cast<BleCmdDispatcher*>(ctx)->run();
}

// ---- BLE-side handlers (run on BLE host task) ----

void BleCmdDispatcher::enqueueLine(const String& line) {
    WorkMsg msg{};
    msg.kind = WorkKind::CmdLine;
    msg.len = (line.length() > cfg::ble::MAX_CMD_LINE)
              ? cfg::ble::MAX_CMD_LINE : line.length();
    memcpy(msg.bytes, line.c_str(), msg.len);
    msg.bytes[msg.len] = '\0';
    // Drop on overflow — App can retry.
    xQueueSend(queue_, &msg, 0);
}

void BleCmdDispatcher::onDataChunk(const uint8_t* data, size_t len) {
    if (!transport_ || !data || len == 0) return;
    if (len > cfg::ble::MAX_DATA_CHUNK) {
        log_w("BLE data chunk too large: %u", (unsigned)len);
        noteUploadQueueOverflow();
        return;
    }

    WorkMsg msg{};
    msg.kind = WorkKind::DataChunk;
    msg.len  = len;
    memcpy(msg.bytes, data, len);
    if (xQueueSend(queue_, &msg, 0) != pdPASS) {
        noteUploadQueueOverflow();
    }
}

void BleCmdDispatcher::onConnect(bool connected) {
    if (!connected) {
        // Abort any in-flight upload/preview to release file handles and avoid
        // leaving the session half-finished.
        if (transport_) transport_->abort();
        uploadDeclaredPages_ = 0;
        uploadDirName_       = "";
        resetUploadQueueState();
    }
}

// ---- Work task ----

void BleCmdDispatcher::run() {
    WorkMsg msg;
    while (true) {
        if (xQueueReceive(queue_, &msg, portMAX_DELAY) != pdPASS) continue;

        if (consumeUploadQueueOverflow()) {
            if (transport_ && transport_->isUploading()) {
                transport_->abort();
            }
            uploadDeclaredPages_ = 0;
            uploadDirName_       = "";
            sendSimple("upload_error", "queue_overflow");
        }

        if (msg.kind == WorkKind::CmdLine) {
            String line(reinterpret_cast<const char*>(msg.bytes), msg.len);
            dispatch(line);
            continue;
        }

        handleQueuedDataChunk(msg.bytes, msg.len);
    }
}

void BleCmdDispatcher::handleQueuedDataChunk(const uint8_t* data, size_t len) {
    if (!transport_ || dropUploadData_) return;

    bool pageComplete = false;
    uint16_t pageIdx = 0;
    size_t pageBytes = 0;
    bool ok = transport_->onDataChunk(data, len, pageComplete, pageIdx, pageBytes);
    if (!ok) {
        dropUploadData_ = true;
        sendSimple("upload_error", "session_error");
        return;
    }
    if (pageComplete) {
        JsonDocument resp;
        resp["cmd"]   = "page_ack";
        resp["page"]  = pageIdx;
        resp["bytes"] = (uint32_t)pageBytes;
        sendResponse(resp);
    }
}

void BleCmdDispatcher::noteUploadQueueOverflow() {
    portENTER_CRITICAL(&uploadStateLock_);
    uploadQueueOverflow_ = true;
    dropUploadData_      = true;
    portEXIT_CRITICAL(&uploadStateLock_);
}

bool BleCmdDispatcher::consumeUploadQueueOverflow() {
    portENTER_CRITICAL(&uploadStateLock_);
    bool overflow = uploadQueueOverflow_;
    uploadQueueOverflow_ = false;
    portEXIT_CRITICAL(&uploadStateLock_);
    return overflow;
}

void BleCmdDispatcher::resetUploadQueueState() {
    portENTER_CRITICAL(&uploadStateLock_);
    uploadQueueOverflow_ = false;
    dropUploadData_      = false;
    portEXIT_CRITICAL(&uploadStateLock_);
}

void BleCmdDispatcher::dispatch(const String& line) {
    JsonDocument req;
    DeserializationError err = deserializeJson(req, line);
    if (err) {
        log_w("BLE cmd parse error: %s", err.c_str());
        sendSimple("error", "bad_json");
        return;
    }

    const char* cmd = req["cmd"] | "";
    if (cmd[0] == '\0') {
        sendSimple("error", "missing_cmd");
        return;
    }

    JsonDocument resp;

    if (strcmp(cmd, "get_device_info") == 0) {
        handleGetDeviceInfo(resp);
    } else if (strcmp(cmd, "get_list") == 0) {
        handleGetList(resp);
    } else if (strcmp(cmd, "delete") == 0) {
        handleDelete(req, resp);
    } else if (strcmp(cmd, "upload_start") == 0) {
        handleUploadStart(req, resp);
    } else if (strcmp(cmd, "upload_end") == 0) {
        handleUploadEnd(resp);
    } else if (strcmp(cmd, "preview") == 0) {
        handlePreview(req, resp);
    } else {
        sendSimple("error", "unknown_cmd");
        return;
    }

    sendResponse(resp);
}

// ---- Individual handlers ----

void BleCmdDispatcher::handleGetDeviceInfo(JsonDocument& resp) {
    JsonObject data = resp["data"].to<JsonObject>();
    data["device_name"]      = cfg::ble::DEVICE_NAME;
    data["firmware_version"] = cfg::version::SW_VERSION;
    data["battery_level"]    = battery_->getPercent();
    PowerState ps = battery_->getPowerState();
    data["battery_charging"] = (ps == PowerState::Charging);
    data["screen_width"]     = cfg::display::WIDTH;
    data["screen_height"]    = cfg::display::HEIGHT;
    data["viewport_width"]   = cfg::display::CONTENT_W;
    data["viewport_height"]  = cfg::display::CONTENT_H;
    uint64_t total = sd_->totalBytes() / (1024ULL * 1024ULL);
    uint64_t used  = sd_->usedBytes()  / (1024ULL * 1024ULL);
    data["storage_total_mb"] = (uint32_t)total;
    data["storage_used_mb"]  = (uint32_t)used;
    resp["cmd"] = "device_info_resp";
}

void BleCmdDispatcher::handleGetList(JsonDocument& resp) {
    std::vector<PdfMeta> docs;
    pdf_->listDocs(docs);

    JsonObject data = resp["data"].to<JsonObject>();
    data["count"] = (int)docs.size();
    JsonArray files = data["files"].to<JsonArray>();
    for (const PdfMeta& m : docs) {
        JsonObject f = files.add<JsonObject>();
        f["name"]  = m.name;
        f["time"]  = m.time;
        f["pages"] = m.pages;
    }
    resp["cmd"] = "list_resp";
}

void BleCmdDispatcher::handleDelete(const JsonDocument& req, JsonDocument& resp) {
    JsonVariantConst dataIn = req["data"];
    String name = dataIn["name"] | "";
    String time = dataIn["time"] | "";
    uint16_t pages = dataIn["pages"] | 0;

    JsonObject data = resp["data"].to<JsonObject>();
    data["name"]  = name;
    data["time"]  = time;
    data["pages"] = pages;

    // We need the canonical dir name to delete; reconstruct from name+time+pages.
    // Parse it back via PdfStore.parseDirName once we have the candidate string.
    // Caller is expected to have sent the same fields we returned in list_resp.
    // Build "yyyy-mm-dd_HH-MM-SS_PPP_name" from "yyyy-mm-dd HH:MM:SS" + name + pages.
    String dirName;
    if (time.length() == 19 && pages > 0) {
        // time = "yyyy-mm-dd HH:MM:SS" → "yyyy-mm-dd_HH-MM-SS"
        String t = time;
        t[10] = '_';
        t[13] = '-';
        t[16] = '-';
        char pagesStr[8];
        snprintf(pagesStr, sizeof(pagesStr), "%03u", (unsigned)pages);
        dirName = t + "_" + String(pagesStr) + "_" + name;
    }

    PdfMeta probe;
    bool ok = false;
    if (PdfStore::parseDirName(dirName, probe) && probe.name == name) {
        ok = pdf_->deleteDoc(dirName);
    }
    if (ok) {
        sd_->invalidateStatsCache();
    }
    data["status"] = ok ? "ok" : "error";
    resp["cmd"]    = "delete_resp";
}

void BleCmdDispatcher::handleUploadStart(const JsonDocument& req, JsonDocument& resp) {
    JsonVariantConst dataIn = req["data"];
    String name = dataIn["name"] | "";
    String time = dataIn["time"] | "";
    uint16_t pages = dataIn["pages"] | 0;

    // Build canonical dir name "yyyy-mm-dd_HH-MM-SS_PPP_name" from the
    // display fields ("yyyy-mm-dd HH:MM:SS" + name + pages).
    String dirName;
    bool parsed = false;
    if (time.length() == 19 && pages > 0 && name.length() > 0) {
        String t = time;
        t[10] = '_';
        t[13] = '-';
        t[16] = '-';
        char pagesStr[8];
        snprintf(pagesStr, sizeof(pagesStr), "%03u", (unsigned)pages);
        dirName = t + "_" + String(pagesStr) + "_" + name;
        PdfMeta probe;
        parsed = PdfStore::parseDirName(dirName, probe) && probe.name == name;
    }

    resp["cmd"] = "upload_ack";
    if (!parsed) {
        resp["status"] = "bad_dir_name";
        return;
    }

    auto result = transport_->startUpload(sd_, dirName);
    resp["status"] = uploadStartStatusStr(result);

    if (result == BleDataTransport::UploadStartResult::Ready) {
        resetUploadQueueState();
        uploadDeclaredPages_ = pages;
        uploadDirName_       = dirName;
    }
}

void BleCmdDispatcher::handleUploadEnd(JsonDocument& resp) {
    uint16_t received = 0;
    auto result = transport_->endUpload(received);

    const char* status;
    switch (result) {
        case BleDataTransport::UploadEndResult::Ok:
            status = (uploadDeclaredPages_ == 0 || received == uploadDeclaredPages_)
                     ? "ok" : "partial";
            break;
        case BleDataTransport::UploadEndResult::NotActive:   status = "not_active";   break;
        case BleDataTransport::UploadEndResult::PartialPage: status = "partial";      break;
        case BleDataTransport::UploadEndResult::IoError:     status = "io_error";     break;
        default:                                              status = "error";       break;
    }
    resp["cmd"]            = "upload_end_resp";
    resp["status"]         = status;
    resp["pages_received"] = received;
    resp["pages_declared"] = uploadDeclaredPages_;

    if (result != BleDataTransport::UploadEndResult::NotActive) {
        pdf_->invalidateDocListCache();
        sd_->invalidateStatsCache();
    }

    resetUploadQueueState();
    uploadDeclaredPages_ = 0;
    uploadDirName_       = "";
}

void BleCmdDispatcher::handlePreview(const JsonDocument& req, JsonDocument& resp) {
    JsonVariantConst dataIn = req["data"];
    String name = dataIn["name"] | "";
    String time = dataIn["time"] | "";
    uint16_t pages = dataIn["pages"] | 0;
    uint16_t page = dataIn["page"] | 0;

    // Rebuild dir name.
    String dirName;
    bool parsed = false;
    if (time.length() == 19 && pages > 0 && name.length() > 0) {
        String t = time;
        t[10] = '_';
        t[13] = '-';
        t[16] = '-';
        char pagesStr[8];
        snprintf(pagesStr, sizeof(pagesStr), "%03u", (unsigned)pages);
        dirName = t + "_" + String(pagesStr) + "_" + name;
        PdfMeta probe;
        parsed = PdfStore::parseDirName(dirName, probe) && probe.name == name;
    }

    resp["page"] = page;
    if (!parsed) {
        resp["cmd"]    = "preview_error";
        resp["status"] = "bad_dir_name";
        return;
    }
    if (page >= pages) {
        resp["cmd"]    = "preview_error";
        resp["status"] = "out_of_range";
        return;
    }

    size_t totalBytes = 0;
    uint16_t w = 0, h = 0;
    auto result = transport_->startPreview(pdf_, dirName, page, totalBytes, w, h);
    if (result != BleDataTransport::PreviewStartResult::Ready) {
        resp["cmd"]    = "preview_error";
        resp["status"] = previewStartStatusStr(result);
        return;
    }

    // Stream the file via data characteristic. This blocks the work task;
    // other cmd lines queue up behind. For a single-user viewer that's fine.
    bool ok = transport_->streamPreview(ble_, totalBytes, page);

    resp["cmd"]    = "preview_end";
    resp["page"]   = page;
    resp["bytes"]  = (uint32_t)totalBytes;
    resp["width"]  = w;
    resp["height"] = h;
    resp["status"] = ok ? "ok" : "io_error";
}

// ---- Helpers ----

void BleCmdDispatcher::sendResponse(const JsonDocument& resp) {
    String out;
    serializeJson(resp, out);
    out += '\n';
    if (!ble_ || !ble_->notifyCmd(reinterpret_cast<const uint8_t*>(out.c_str()), out.length())) {
        log_w("BLE cmd response send failed len=%u", (unsigned)out.length());
    }
}

void BleCmdDispatcher::sendSimple(const char* cmd, const char* status) {
    JsonDocument resp;
    resp["cmd"]    = cmd;
    resp["status"] = status;
    sendResponse(resp);
}

} // namespace modules
