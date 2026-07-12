#include "BleModule.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <cstring>

#include "../config/Config.h"

// S3 framework uses NimBLE backend for BLE; the legacy Bluedroid APIs
// (esp_gap_ble_api.h, esp_gatts_api.h, BLE2902) are not available. We rely on
// the BLE library's high-level abstraction (BLEDevice/BLEServer/BLECharacteristic),
// which is backend-agnostic. 2M PHY switching via NimBLE's ble_gap_set_prefered_le_phy
// is left for a future optimization pass — many phones auto-upgrade to 2M PHY
// after the first few packets anyway.

namespace modules {

namespace {

const BLEUUID BATTERY_SERVICE_UUID((uint16_t)0x180F);
const BLEUUID BATTERY_LEVEL_UUID((uint16_t)0x2A19);

const BLEUUID EPDF_SERVICE_UUID(cfg::ble::EPDF_SERVICE_UUID);
const BLEUUID CMD_CHAR_UUID(cfg::ble::CMD_CHAR_UUID);
const BLEUUID DATA_CHAR_UUID(cfg::ble::DATA_CHAR_UUID);

BleModule* g_self = nullptr;  // for inner-class trampolines

} // namespace

// ---- Inner-class callback trampolines ----

class BleServerCallbacks : public BLEServerCallbacks {
public:
    void onConnect(BLEServer* server) override {
        (void)server;
        // NimBLE path goes through the desc'd overload below; this is just a fallback.
    }
#if defined(CONFIG_NIMBLE_ENABLED)
    void onConnect(BLEServer* server, ble_gap_conn_desc* desc) override {
        (void)server;
        if (g_self && desc) g_self->handleConnect(desc->conn_handle);
    }
    void onDisconnect(BLEServer* server, ble_gap_conn_desc* desc) override {
        (void)desc;
        // Only auto-resume advertising when BLE is still enabled — avoids
        // touching the stack while end() is tearing it down.
        if (g_self && g_self->isEnabled()) {
            server->getAdvertising()->start();
        }
        if (g_self) g_self->handleDisconnect();
    }
    void onMtuChanged(BLEServer* server, ble_gap_conn_desc* desc, uint16_t mtu) override {
        (void)server;
        if (g_self && desc) g_self->handleMtuChanged(desc->conn_handle, mtu);
    }
#else
    void onDisconnect(BLEServer* server) override {
        if (g_self && g_self->isEnabled()) {
            server->getAdvertising()->start();
        }
        if (g_self) g_self->handleDisconnect();
    }
#endif
};

class BleCmdCallbacks : public BLECharacteristicCallbacks {
public:
    void onWrite(BLECharacteristic* chr) override {
        String v = chr->getValue();
        if (v.length() && g_self) {
            g_self->handleCmdWrite(reinterpret_cast<const uint8_t*>(v.c_str()), v.length());
        }
    }
};

class BleDataCallbacks : public BLECharacteristicCallbacks {
public:
    void onWrite(BLECharacteristic* chr) override {
        String v = chr->getValue();
        if (v.length() && g_self) {
            g_self->handleDataWrite(reinterpret_cast<const uint8_t*>(v.c_str()), v.length());
        }
    }
};

// ---- BleModule ----

bool BleModule::begin() {
    if (initialized_) return true;
    g_self = this;

    BLEDevice::init(cfg::ble::DEVICE_NAME);
    BLEDevice::setMTU(cfg::ble::TARGET_MTU);

    server_ = BLEDevice::createServer();
    server_->setCallbacks(new BleServerCallbacks());

    // --- Battery Service ---
    batterySvc_ = server_->createService(BATTERY_SERVICE_UUID);
    batteryChar_ = batterySvc_->createCharacteristic(
        BATTERY_LEVEL_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    uint8_t initial = 0;
    batteryChar_->setValue(&initial, 1);
    batterySvc_->start();

    // --- EPDF Service ---
    // Cmd characteristic: Write (with response) + Write Without Response + Notify.
    // Both write modes so a peer can pick whichever it prefers.
    epdfSvc_ = server_->createService(EPDF_SERVICE_UUID);

    cmdChar_ = epdfSvc_->createCharacteristic(
        CMD_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR |
        BLECharacteristic::PROPERTY_NOTIFY);
    cmdChar_->setCallbacks(new BleCmdCallbacks());

    dataChar_ = epdfSvc_->createCharacteristic(
        DATA_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR |
        BLECharacteristic::PROPERTY_NOTIFY);
    dataChar_->setCallbacks(new BleDataCallbacks());

    epdfSvc_->start();

    initialized_ = true;
    log_i("BleModule ready (Battery + EPDF service, MTU target=%u)",
          (unsigned)cfg::ble::TARGET_MTU);
    return true;
}

void BleModule::setEnabled(bool on) {
    if (on == enabled_) return;
    if (on) {
        // Re-initialise the BLE stack and services every time we (re)enable.
        // begin() is idempotent when already initialised, and a no-op-safe full
        // re-init after end().
        if (!begin()) {
            log_e("BLE re-init failed; staying disabled");
            return;
        }
        BLEAdvertising* adv = BLEDevice::getAdvertising();
        adv->addServiceUUID(EPDF_SERVICE_UUID);
        adv->addServiceUUID(BATTERY_SERVICE_UUID);
        adv->setScanResponse(true);
        adv->setMinPreferred(0x06);  // helps iPhone discovery
        adv->setMaxPreferred(0x12);
        BLEDevice::startAdvertising();
        enabled_ = true;
        log_i("BLE advertising started");
    } else {
        end();
    }
}

void BleModule::end() {
    if (!initialized_) return;

    // Signal onDisconnect trampoline not to restart advertising during teardown.
    enabled_ = false;

    BLEDevice::stopAdvertising();

    // Drop any active connection before deinit. Calling BLEDevice::deinit()
    // with a live connection crashes the NimBLE host task. Disconnect is
    // asynchronous — poll connected_ (cleared by handleDisconnect from the
    // host task) with a timeout so we don't deadlock if the peer is slow.
    if (connected_ && server_) {
        log_i("BLE teardown: disconnecting connHandle=%u", (unsigned)connHandle_);
        server_->disconnect(connHandle_);
        for (int i = 0; i < 50 && connected_; ++i) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (connected_) {
            log_w("BLE teardown: disconnect did not complete in 500ms, deinit anyway");
        }
    }

    // Fully tear down the BLE stack. NimBLE bonding data persists in NVS, so
    // previously paired peers reconnect without re-pairing after re-init.
    BLEDevice::deinit();

    server_      = nullptr;
    batterySvc_  = nullptr;
    epdfSvc_     = nullptr;
    batteryChar_ = nullptr;
    cmdChar_     = nullptr;
    dataChar_    = nullptr;

    initialized_ = false;
    connected_   = false;
    connHandle_  = 0;
    mtu_         = 23;
    cmdLineBuf_.remove(0, cmdLineBuf_.length());

    log_i("BleModule deinit complete (BLE stack fully disabled)");
}

void BleModule::setBatteryLevel(uint8_t percent) {
    if (!batteryChar_) return;
    uint8_t v = (percent > 100) ? 100 : percent;
    batteryChar_->setValue(&v, 1);
    batteryChar_->notify();
}

void BleModule::notifyCmd(const uint8_t* data, size_t len) {
    if (cmdChar_) {
        cmdChar_->setValue((uint8_t*)data, (size_t)len);
        cmdChar_->notify();
    }
}

void BleModule::notifyData(const uint8_t* data, size_t len) {
    if (dataChar_) {
        size_t maxPayload = (mtu_ > 3) ? (size_t)(mtu_ - 3) : (size_t)20;
        if (len > maxPayload) {
            log_w("BLE data notify len=%u exceeds mtu payload=%u",
                  (unsigned)len,
                  (unsigned)maxPayload);
        }
        dataChar_->setValue((uint8_t*)data, (size_t)len);
        dataChar_->notify();
    }
}

void BleModule::handleConnect(uint16_t connHandle) {
    connected_  = true;
    connHandle_ = connHandle;
    mtu_        = 23;

    // Push connection interval toward the configured range for throughput.
    // The BLE library normalizes this across NimBLE/Bluedroid backends.
    if (server_) {
        server_->updateConnParams(connHandle,
                                   cfg::ble::CONN_INT_MIN,
                                   cfg::ble::CONN_INT_MAX,
                                   cfg::ble::CONN_LATENCY,
                                   cfg::ble::CONN_TIMEOUT);
    }

    log_i("BLE connect connHandle=%u, requesting conn params %u/%u/%u/%u",
          (unsigned)connHandle,
          (unsigned)cfg::ble::CONN_INT_MIN,
          (unsigned)cfg::ble::CONN_INT_MAX,
          (unsigned)cfg::ble::CONN_LATENCY,
          (unsigned)cfg::ble::CONN_TIMEOUT);

    if (connectCb_) connectCb_(true, connectCtx_);
}

void BleModule::handleMtuChanged(uint16_t connHandle, uint16_t mtu) {
    (void)connHandle;
    mtu_ = mtu;
    log_i("BLE MTU negotiated: %u", (unsigned)mtu);
}

void BleModule::handleDisconnect() {
    connected_ = false;
    connHandle_ = 0;
    mtu_       = 23;
    cmdLineBuf_.remove(0, cmdLineBuf_.length());
    log_i("BLE disconnect");
    if (connectCb_) connectCb_(false, connectCtx_);
}

void BleModule::handleCmdWrite(const uint8_t* data, size_t len) {
    if (!data || len == 0) return;

    // Append to line buffer and dispatch any complete '\n'-terminated lines.
    // Long lines beyond MAX_CMD_LINE are flushed to avoid memory bloat from
    // malformed input (no newline ever sent).
    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c == '\n') {
            dispatchCmdLine(cmdLineBuf_);
            cmdLineBuf_.remove(0, cmdLineBuf_.length());
        } else if (c != '\r') {
            cmdLineBuf_ += c;
            if (cmdLineBuf_.length() > cfg::ble::MAX_CMD_LINE) {
                log_w("BLE cmd line overflow (%u), discarding",
                      (unsigned)cmdLineBuf_.length());
                cmdLineBuf_.remove(0, cmdLineBuf_.length());
            }
        }
    }
}

void BleModule::handleDataWrite(const uint8_t* data, size_t len) {
    if (!data || len == 0) return;
    if (dataChunkCb_) dataChunkCb_(data, len, dataChunkCtx_);
}

void BleModule::dispatchCmdLine(const String& line) {
    if (line.length() == 0) return;
    if (cmdLineCb_) cmdLineCb_(line, cmdLineCtx_);
}

} // namespace modules
