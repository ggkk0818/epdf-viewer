#include "BleModule.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#include "../config/Config.h"

namespace modules {

namespace {

const BLEUUID BATTERY_SERVICE_UUID((uint16_t)0x180F);
const BLEUUID BATTERY_LEVEL_UUID((uint16_t)0x2A19);

BLECharacteristic* g_batteryLevelChar = nullptr;
BLEServer*         g_server           = nullptr;

} // namespace

bool BleModule::begin() {
    BLEDevice::init(cfg::ble::DEVICE_NAME);
    BLEDevice::setMTU(23);

    g_server = BLEDevice::createServer();

    BLEService* service = g_server->createService(BATTERY_SERVICE_UUID);

    g_batteryLevelChar = service->createCharacteristic(
        BATTERY_LEVEL_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

    uint8_t initial = 0;
    g_batteryLevelChar->setValue(&initial, 1);

    service->start();
    initialized_ = true;
    log_i("BleModule ready (Battery Service)");
    return true;
}

void BleModule::setEnabled(bool on) {
    if (!initialized_) return;
    if (on == enabled_) return;
    if (on) {
        BLEAdvertising* adv = BLEDevice::getAdvertising();
        adv->addServiceUUID(BATTERY_SERVICE_UUID);
        adv->setScanResponse(true);
        adv->setMinPreferred(0x06);
        adv->setMaxPreferred(0x12);
        BLEDevice::startAdvertising();
        enabled_ = true;
        log_i("BLE advertising started");
    } else {
        BLEDevice::stopAdvertising();
        enabled_ = false;
        log_i("BLE advertising stopped");
    }
}

void BleModule::setBatteryLevel(uint8_t percent) {
    if (!g_batteryLevelChar) return;
    uint8_t v = (percent > 100) ? 100 : percent;
    g_batteryLevelChar->setValue(&v, 1);
    g_batteryLevelChar->notify();
}

} // namespace modules
