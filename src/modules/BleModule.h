#pragma once

#include <Arduino.h>
#include <BLEServer.h>
#include <BLEUtils.h>

namespace modules {

// Owns BLE GATT server lifecycle: Battery Service (0x180F) + custom EPDF
// Service (0xFFE0) with two characteristics — Cmd (Write+Notify) and Data
// (WriteNoResp+Notify). On connect, configures MTU=512, 2M PHY, and tight
// connection params for throughput.
//
// begin()/end() form a full init/teardown pair: end() calls BLEDevice::deinit()
// so the BLE stack is fully disabled (not just advertising paused). Re-calling
// begin() re-registers device name, services and characteristics from scratch.
// NimBLE bonding data lives in NVS and survives end(), so previously paired
// peers reconnect without re-pairing.
//
// Higher-level components (BleCmdDispatcher, BleDataTransport) register
// callbacks to receive framed cmd lines and raw data chunks. The BLE host
// task invokes these callbacks synchronously — they must not block on SD I/O
// for long (use a queue + dedicated task if you need heavy lifting).
class BleModule {
public:
    using CmdLineCallback   = void (*)(const String& line, void* ctx);
    using DataChunkCallback = void (*)(const uint8_t* data, size_t len, void* ctx);
    using ConnectCallback   = void (*)(bool connected, void* ctx);

    bool begin();
    void setEnabled(bool on);
    bool isEnabled() const { return enabled_; }

    void setBatteryLevel(uint8_t percent);

    // EPDF service accessors — used by dispatcher/transport to send notifies.
    bool notifyCmd(const uint8_t* data, size_t len);
    bool notifyData(const uint8_t* data, size_t len, uint32_t* outCode = nullptr);

    bool     isConnected() const  { return connected_; }
    uint16_t negotiatedMtu() const { return mtu_; }

    // Callback registration. Only one of each kind — last registration wins.
    void onCmdLine(CmdLineCallback cb, void* ctx) {
        cmdLineCb_ = cb; cmdLineCtx_ = ctx;
    }
    void onDataChunk(DataChunkCallback cb, void* ctx) {
        dataChunkCb_ = cb; dataChunkCtx_ = ctx;
    }
    void onConnect(ConnectCallback cb, void* ctx) {
        connectCb_ = cb; connectCtx_ = ctx;
    }

private:
    bool                initialized_ = false;
    bool                enabled_     = false;
    bool                connected_   = false;
    bool                dataNotifySubscribed_ = false;
    uint16_t            mtu_         = 23;
    uint16_t            connHandle_  = 0;
    BLECharacteristicCallbacks::Status lastDataNotifyStatus_ = BLECharacteristicCallbacks::Status::SUCCESS_NOTIFY;
    uint32_t            lastDataNotifyCode_ = 0;

    BLEServer*          server_      = nullptr;
    BLEService*         batterySvc_  = nullptr;
    BLEService*         epdfSvc_     = nullptr;
    BLECharacteristic*  batteryChar_ = nullptr;
    BLECharacteristic*  cmdChar_     = nullptr;
    BLECharacteristic*  dataChar_    = nullptr;

    // Line accumulation for the cmd channel (JSON lines end with '\n').
    String              cmdLineBuf_;

    CmdLineCallback     cmdLineCb_    = nullptr;
    void*               cmdLineCtx_   = nullptr;
    DataChunkCallback   dataChunkCb_  = nullptr;
    void*               dataChunkCtx_ = nullptr;
    ConnectCallback     connectCb_    = nullptr;
    void*               connectCtx_   = nullptr;

    // Inner-class trampolines (defined in .cpp) — keep header free of ESP-IDF
    // detail while still letting them reach back into BleModule.
    friend class BleServerCallbacks;
    friend class BleCmdCallbacks;
    friend class BleDataCallbacks;

    void handleConnect(uint16_t connHandle);
    void handleMtuChanged(uint16_t connHandle, uint16_t mtu);
    void handleDisconnect();
    void handleCmdWrite(const uint8_t* data, size_t len);
    void handleDataWrite(const uint8_t* data, size_t len);
    void handleDataSubscribe(uint16_t subValue);
    void handleDataNotifyStatus(BLECharacteristicCallbacks::Status status, uint32_t code);
    void dispatchCmdLine(const String& line);

    // Full teardown inverse of begin(): stops advertising, calls
    // BLEDevice::deinit(), clears all service/characteristic handles.
    void end();
};

} // namespace modules
