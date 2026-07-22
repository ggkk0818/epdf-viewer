#include "OtaService.h"
#include "BleModule.h"

#include <Update.h>
#include <cstring>

namespace modules {

namespace {

// Encode a 32-bit little-endian value into dst[0..3].
void putLe32(uint8_t* dst, uint32_t v) {
    dst[0] = (uint8_t)(v & 0xFF);
    dst[1] = (uint8_t)((v >> 8) & 0xFF);
    dst[2] = (uint8_t)((v >> 16) & 0xFF);
    dst[3] = (uint8_t)((v >> 24) & 0xFF);
}

uint32_t readLe32(const uint8_t* src) {
    return  (uint32_t)src[0]
          | ((uint32_t)src[1] << 8)
          | ((uint32_t)src[2] << 16)
          | ((uint32_t)src[3] << 24);
}

// Standard IEEE CRC32 reflected update (polynomial 0xEDB88320). Performs
// only the raw register update — caller is responsible for init (0xFFFFFFFF)
// on the first chunk and final XOR (0xFFFFFFFF) at the end. Matches the
// table-driven version on the Dart side.
uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) {
            const uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

} // namespace

bool OtaService::begin(BleModule* ble) {
    if (!ble) return false;
    ble_ = ble;
    queue_ = xQueueCreate(cfg::ota::QUEUE_LEN, sizeof(WorkMsg));
    return queue_ != nullptr;
}

bool OtaService::start() {
    if (!ble_ || !queue_) return false;

    ble_->onOtaCtrl(&OtaService::ctrlTrampoline, this);
    ble_->onOtaData(&OtaService::dataTrampoline, this);

    BaseType_t ok = xTaskCreatePinnedToCore(
        &OtaService::taskTrampoline,
        "otaWork",
        cfg::ota::WORK_STACK,
        this,
        cfg::ota::WORK_PRIO,
        &task_,
        cfg::ota::WORK_CORE);
    return ok == pdPASS;
}

// ---- Trampolines (BLE host task → member enqueue) ----

void OtaService::ctrlTrampoline(const uint8_t* data, size_t len, void* ctx) {
    static_cast<OtaService*>(ctx)->onCtrlWrite(data, len);
}

void OtaService::dataTrampoline(const uint8_t* data, size_t len, void* ctx) {
    static_cast<OtaService*>(ctx)->onDataWrite(data, len);
}

void OtaService::taskTrampoline(void* ctx) {
    static_cast<OtaService*>(ctx)->run();
}

void OtaService::onCtrlWrite(const uint8_t* data, size_t len) {
    if (!data || len == 0 || len > cfg::ota::MAX_CHUNK) return;
    WorkMsg msg{};
    msg.kind = WorkKind::Ctrl;
    msg.len  = len;
    memcpy(msg.bytes, data, len);
    // Drop on overflow — phone will time out and retry.
    xQueueSend(queue_, &msg, 0);
}

void OtaService::onDataWrite(const uint8_t* data, size_t len) {
    if (!data || len == 0 || len > cfg::ota::MAX_CHUNK) return;
    WorkMsg msg{};
    msg.kind = WorkKind::Data;
    msg.len  = len;
    memcpy(msg.bytes, data, len);
    xQueueSend(queue_, &msg, 0);
}

void OtaService::abortFromBle() {
    // Called from BLE host task on disconnect. Only flips the active flag so
    // the worker stops processing further queued data; actual Update.abort()
    // runs on the next START from the worker context.
    active_ = false;
    paused_ = false;
}

// ---- Worker task ----

void OtaService::run() {
    WorkMsg msg;
    while (true) {
        // Bounded wait so we can detect a dropped connection mid-session and
        // reset state. Without this, a half-finished session lingers and the
        // next phone to connect would inherit stale progress counters.
        if (xQueueReceive(queue_, &msg, pdMS_TO_TICKS(500)) != pdPASS) {
            if (active_ && ended_ == false && ble_ && !ble_->isConnected()) {
                log_i("OTA: connection lost mid-session, resetting");
                if (Update.isRunning()) Update.abort();
                active_ = false;
                resetState();
            }
            continue;
        }

        if (msg.kind == WorkKind::Ctrl) {
            handleCtrl(msg.bytes, msg.len);
        } else {
            handleData(msg.bytes, msg.len);
        }
    }
}

void OtaService::handleCtrl(const uint8_t* data, size_t len) {
    if (!data || len == 0) return;
    const uint8_t code = data[0];
    switch (code) {
        case cfg::ota::cmd::START:
            if (len < 5) {
                sendStartFail(cfg::ota::err::BEGIN_FAILED);
                return;
            }
            handleStart(readLe32(data + 1));
            break;
        case cfg::ota::cmd::END:
            if (len < 5) {
                sendStatus(cfg::ota::status::CRC_FAIL);
                return;
            }
            handleEnd(readLe32(data + 1));
            break;
        case cfg::ota::cmd::REBOOT:
            handleReboot();
            break;
        case cfg::ota::cmd::PAUSE:
            paused_ = true;
            break;
        case cfg::ota::cmd::RESUME:
            paused_ = false;
            break;
        default:
            log_w("OTA unknown ctrl 0x%02x", code);
            break;
    }
}

void OtaService::handleData(const uint8_t* data, size_t len) {
    if (!active_ || paused_ || ended_) return;
    if (!data || len == 0) return;

    if (!Update.isRunning()) {
        // Phone started streaming before/without START — drop and report.
        sendStartFail(cfg::ota::err::UNEXPECTED);
        active_ = false;
        return;
    }

    size_t written = Update.write(const_cast<uint8_t*>(data), len);
    if (written != len) {
        log_e("OTA write short: wrote=%u of %u, err=%s",
              (unsigned)written, (unsigned)len, Update.errorString());
        Update.abort();
        sendStartFail(cfg::ota::err::WRITE_FAILED);
        active_ = false;
        return;
    }

    received_   += (uint32_t)len;
    crcAccum_    = crc32Update(crcAccum_, data, len);

    // Emit ack at every ACK_INTERVAL_BYTES boundary. The phone advances its
    // sliding window off these acks.
    const uint32_t threshold = (received_ / cfg::ota::ACK_INTERVAL_BYTES)
                               * cfg::ota::ACK_INTERVAL_BYTES;
    if (threshold > lastAckedThresh_) {
        lastAckedThresh_ = threshold;
        sendAck();
    }
}

void OtaService::handleStart(uint32_t totalSize) {
    if (totalSize == 0) {
        sendStartFail(cfg::ota::err::BEGIN_FAILED);
        return;
    }

    // If a prior session was left half-finished (e.g. phone disconnected
    // mid-stream), Update.begin() will internally abort it before starting
    // fresh — no explicit reset needed.
    resetState();
    // esp_crc32_le expects an initial register of 0xFFFFFFFF for the standard
    // zip/PNG CRC32; final XOR happens at END.
    crcAccum_ = 0xFFFFFFFFu;

    if (!Update.begin(totalSize)) {
        log_e("Update.begin failed size=%u err=%s",
              (unsigned)totalSize, Update.errorString());
        sendStartFail(cfg::ota::err::BEGIN_FAILED);
        return;
    }

    total_   = totalSize;
    active_  = true;
    ended_   = false;
    log_i("OTA begin size=%u", (unsigned)totalSize);

    // First ack with 0 bytes-written so the phone knows it can start streaming.
    sendAck();
}

void OtaService::handleEnd(uint32_t expectedCrc) {
    if (!active_) {
        log_w("OTA END without active session");
        sendStatus(cfg::ota::status::CRC_FAIL);
        return;
    }
    if (received_ != total_) {
        log_w("OTA END size mismatch received=%u total=%u",
              (unsigned)received_, (unsigned)total_);
        Update.abort();
        sendStatus(cfg::ota::status::CRC_FAIL);
        active_ = false;
        return;
    }

    const uint32_t actual = crcAccum_ ^ 0xFFFFFFFFu;
    if (actual != expectedCrc) {
        log_e("OTA CRC mismatch expected=0x%08x actual=0x%08x",
              (unsigned)expectedCrc, (unsigned)actual);
        Update.abort();
        sendStatus(cfg::ota::status::CRC_FAIL);
        active_ = false;
        return;
    }

    if (!Update.end(true)) {
        log_e("Update.end failed: %s", Update.errorString());
        sendStatus(cfg::ota::status::CRC_FAIL);
        active_ = false;
        return;
    }

    log_i("OTA CRC OK, partition finalized. Awaiting REBOOT.");
    ended_ = true;
    sendStatus(cfg::ota::status::CRC_OK);
    // active_ stays true so REBOOT is accepted; further data writes are
    // dropped because ended_ == true.
}

void OtaService::handleReboot() {
    log_i("OTA REBOOT command — restarting");
    // Small delay so the notify on the previous command has a chance to flush.
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP.restart();
}

void OtaService::sendAck() {
    uint8_t buf[5];
    buf[0] = cfg::ota::status::ACK;
    putLe32(buf + 1, received_);
    if (ble_) ble_->notifyOtaCtrl(buf, sizeof(buf));
}

void OtaService::sendStartFail(uint8_t err) {
    uint8_t buf[2];
    buf[0] = cfg::ota::status::START_FAIL;
    buf[1] = err;
    if (ble_) ble_->notifyOtaCtrl(buf, sizeof(buf));
}

void OtaService::sendStatus(uint8_t code) {
    uint8_t buf[1] = { code };
    if (ble_) ble_->notifyOtaCtrl(buf, sizeof(buf));
}

void OtaService::resetState() {
    total_           = 0;
    received_        = 0;
    crcAccum_        = 0;
    lastAckedThresh_ = 0;
    paused_          = false;
    ended_           = false;
}

} // namespace modules
