#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "BleDataTransport.h"

namespace modules {

class BleModule;
class PdfStore;
class SdModule;
class BatteryModule;

} // namespace modules

namespace app { class AppController; }

namespace modules {

// Parses JSON command lines received on the BLE cmd characteristic and
// dispatches them to PdfStore / BatteryModule / BleDataTransport handlers.
// Runs handlers on a dedicated work task so slow SD operations (recursive
// delete, preview streaming) don't block the BLE host task.
class BleCmdDispatcher {
public:
    bool begin(BleModule* ble, PdfStore* pdf, SdModule* sd, BatteryModule* battery,
               BleDataTransport* transport, app::AppController* app);
    bool start();  // launches work task, registers BLE callbacks

private:
    BleModule*        ble_      = nullptr;
    PdfStore*         pdf_      = nullptr;
    SdModule*         sd_       = nullptr;
    BatteryModule*    battery_  = nullptr;
    BleDataTransport* transport_ = nullptr;
    app::AppController* app_    = nullptr;

    QueueHandle_t     queue_    = nullptr;
    TaskHandle_t      task_     = nullptr;

    // Declared page count from the most recent upload_start (used at upload_end
    // to detect partial uploads).
    uint16_t          uploadDeclaredPages_ = 0;
    String            uploadDirName_;
    volatile bool     uploadQueueOverflow_ = false;
    volatile bool     dropUploadData_      = false;
    portMUX_TYPE      uploadStateLock_     = portMUX_INITIALIZER_UNLOCKED;

    enum class WorkKind : uint8_t {
        CmdLine,
        DataChunk,
    };

    static constexpr size_t WORK_MSG_BYTES =
        (cfg::ble::MAX_CMD_LINE + 1 > cfg::ble::MAX_DATA_CHUNK)
        ? (cfg::ble::MAX_CMD_LINE + 1)
        : cfg::ble::MAX_DATA_CHUNK;

    struct WorkMsg {
        WorkKind kind;
        size_t len;
        uint8_t bytes[WORK_MSG_BYTES];
    };

    static void cmdLineTrampoline(const String& line, void* ctx);
    static void dataChunkTrampoline(const uint8_t* data, size_t len, void* ctx);
    static void connectTrampoline(bool connected, void* ctx);
    static void taskTrampoline(void* ctx);

    void enqueueLine(const String& line);
    void onDataChunk(const uint8_t* data, size_t len);
    void onConnect(bool connected);
    void run();
    void handleQueuedDataChunk(const uint8_t* data, size_t len);
    void noteUploadQueueOverflow();
    bool consumeUploadQueueOverflow();
    void resetUploadQueueState();

    void dispatch(const String& line);
    void handleGetDeviceInfo(JsonDocument& resp);
    void handleGetList(JsonDocument& resp);
    void handleDelete(const JsonDocument& req, JsonDocument& resp);
    void handleUploadStart(const JsonDocument& req, JsonDocument& resp);
    void handleUploadEnd(JsonDocument& resp);
    void handlePreview(const JsonDocument& req, JsonDocument& resp);
    void handleViewOnDevice(const JsonDocument& req, JsonDocument& resp);
    void handleInputEvent(const JsonDocument& req, JsonDocument& resp);

    void sendResponse(const JsonDocument& resp);
    void sendSimple(const char* cmd, const char* status);
};

} // namespace modules