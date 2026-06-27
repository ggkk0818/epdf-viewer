#include "BatteryModule.h"
#include "../config/Config.h"

namespace modules {

namespace {

uint8_t crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1);
        }
    }
    return crc;
}

constexpr uint8_t I2C_ADDR_8 = cfg::battery::I2C_ADDR;

} // namespace

bool BatteryModule::begin() {
    wire_->begin(cfg::pin::I2C_SDA, cfg::pin::I2C_SCL);
    wire_->setClock(100000);
    delay(10);

    bool ok = writeReg(0x15, 0x0001);
    if (!ok) {
        log_e("LC709203F power mode write failed");
        present_ = false;
        return false;
    }
    delay(50);
    writeReg(0x0C, 0x0000);
    delay(10);
    writeReg(0x06, 0x0B1F);
    delay(10);

    uint16_t rsoc = 0;
    if (readReg(cfg::battery::REG_RSOC, rsoc)) {
        lastPercent_ = rsoc & 0xFF;
        present_ = true;
        log_i("LC709203F ok, RSOC=%u%%", lastPercent_);
        return true;
    }
    log_e("LC709203F RSOC read failed");
    present_ = false;
    return false;
}

uint8_t BatteryModule::getPercent() {
    if (!present_) return lastPercent_;
    uint16_t rsoc = 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (readReg(cfg::battery::REG_RSOC, rsoc)) {
            lastPercent_ = rsoc & 0xFF;
            return lastPercent_;
        }
        delay(5);
    }
    return lastPercent_;
}

uint16_t BatteryModule::getVoltageMv() {
    if (!present_) return 0;
    uint16_t v = 0;
    if (readReg(cfg::battery::REG_VOLTAGE, v)) {
        return v;
    }
    return 0;
}

bool BatteryModule::writeReg(uint8_t reg, uint16_t val) {
    uint8_t buf[4] = {
        static_cast<uint8_t>(I2C_ADDR_8 << 1),
        reg,
        static_cast<uint8_t>(val & 0xFF),
        static_cast<uint8_t>((val >> 8) & 0xFF),
    };
    uint8_t pec = crc8(buf, 4);

    wire_->beginTransmission(I2C_ADDR_8);
    wire_->write(reg);
    wire_->write(buf[2]);
    wire_->write(buf[3]);
    wire_->write(pec);
    return wire_->endTransmission() == 0;
}

bool BatteryModule::readReg(uint8_t reg, uint16_t& val) {
    wire_->beginTransmission(I2C_ADDR_8);
    wire_->write(reg);
    if (wire_->endTransmission(false) != 0) return false;

    size_t got = wire_->requestFrom((uint8_t)I2C_ADDR_8, (uint8_t)3);
    if (got != 3) return false;

    uint8_t lo = wire_->read();
    uint8_t hi = wire_->read();
    uint8_t pecRecv = wire_->read();

    uint8_t buf[5] = {
        static_cast<uint8_t>(I2C_ADDR_8 << 1),
        reg,
        static_cast<uint8_t>((I2C_ADDR_8 << 1) | 1u),
        lo,
        hi,
    };
    uint8_t pecCalc = crc8(buf, 5);
    if (pecCalc != pecRecv) return false;

    val = lo | ((uint16_t)hi << 8);
    return true;
}

} // namespace modules
