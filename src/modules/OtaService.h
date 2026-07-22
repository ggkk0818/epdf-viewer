#pragma once

#include <Arduino.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "../config/Config.h"

namespace modules {

class BleModule;

// Owns the OTA receive/state machine: START → data stream → END → REBOOT.
//
// Protocol (see cfg::ota for byte values):
//   Phone → ESP32 ctrl char (0xFF01):
//     [0x01][size_le4]   begin OTA, allocate erase partition
//     [0x04][crc_le4]    end OTA, verify CRC, finalize partition
//     [0x05]             reboot into new firmware
//   Phone → ESP32 data char (0xFF02): raw firmware bytes, any chunk size
//   ESP32 → Phone ctrl char notify:
//     [0x10][bytes_le4]  bytes-written-so-far (sliding-window ack)
//     [0x11][err]        begin failed
//     [0x12]             CRC mismatch
//     [0x13]             CRC OK, ready for reboot
//
// All flash I/O happens on a dedicated task so the NimBLE host task is never
// blocked. BLE writes (ctrl or data) are queued here; the worker drains them
// in order and emits ack/status notifies as it progresses.
class OtaService {
public:
    bool begin(BleModule* ble);
    bool start();  // registers BLE callbacks, launches worker task

    // Called from BleModule's BLE-host callbacks — must not block.
    void onCtrlWrite(const uint8_t* data, size_t len);
    void onDataWrite(const uint8_t* data, size_t len);

    // Abort in-flight OTA (called on BLE disconnect). Safe to call from BLE
    // host task — only mutates the volatile active_ flag.
    void abortFromBle();

private:
    enum class WorkKind : uint8_t {
        Ctrl,
        Data,
    };

    struct WorkMsg {
        WorkKind kind;
        size_t   len;
        uint8_t  bytes[cfg::ota::MAX_CHUNK];
    };

    BleModule*   ble_        = nullptr;
    QueueHandle_t queue_     = nullptr;
    TaskHandle_t task_       = nullptr;

    // OTA session state — touched only from the worker task, except for
    // active_/paused_ which are also flipped from BLE disconnect.
    volatile bool active_         = false;
    volatile bool paused_         = false;
    uint32_t      total_          = 0;
    uint32_t      received_       = 0;
    uint32_t      crcAccum_       = 0;       // running CRC32
    uint32_t      lastAckedThresh_ = 0;      // last ACK_INTERVAL_BYTES boundary acked
    bool          ended_          = false;   // CRC already verified + partition finalized

    static void ctrlTrampoline(const uint8_t* data, size_t len, void* ctx);
    static void dataTrampoline(const uint8_t* data, size_t len, void* ctx);
    static void taskTrampoline(void* ctx);

    void run();
    void handleCtrl(const uint8_t* data, size_t len);
    void handleData(const uint8_t* data, size_t len);
    void handleStart(uint32_t totalSize);
    void handleEnd(uint32_t expectedCrc);
    void handleReboot();

    void sendAck();                // [0x10][received_le4]
    void sendStartFail(uint8_t err);
    void sendStatus(uint8_t code); // single byte notify (0x12 / 0x13)
    void resetState();
};

} // namespace modules
