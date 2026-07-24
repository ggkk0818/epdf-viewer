#include "BatteryModule.h"
#include "../config/Config.h"

namespace modules {

namespace {

struct OcvPoint {
    uint16_t vMv;
    uint8_t  pct;
};

// 4.2V Li-ion OCV-SOC table (open-circuit voltage vs. state of charge).
// Ascending in voltage; tick() does linear interpolation between points.
constexpr OcvPoint kOcvTable[] = {
    { 3000,   0 },
    { 3300,   1 },
    { 3400,   5 },
    { 3500,  10 },
    { 3560,  20 },
    { 3620,  30 },
    { 3680,  40 },
    { 3740,  50 },
    { 3800,  60 },
    { 3900,  70 },
    { 4000,  80 },
    { 4100,  90 },
    { 4200, 100 },
};

constexpr uint8_t  kI2cAddr        = cfg::battery::I2C_ADDR;
constexpr uint32_t kVoltageFsMv    = 70800;   // LTC2944 voltage full-scale

// Battery capacity in milliamp-seconds. 1 mAh = 3600 mA·s.
constexpr uint32_t kCapacityMAs    = (uint32_t)cfg::battery::CAPACITY_MAH * 3600;

// LTC2944 ACR LSB in mA·s.
// Datasheet: Q_LSB(mAh) = 0.340 × (50mΩ / Rsense) × (M / 4096)
//   → Q_LSB(mA·s) = 0.340 × 3600 × 50 × M / (Rsense × 4096) = 61200 × M / (Rsense × 4096)
constexpr uint32_t ACR_LSB_MAS =
    (uint32_t)61200 * cfg::battery::M_PRESCALER
    / ((uint32_t)cfg::battery::R_SENSE_MOHM * 4096);
static_assert(cfg::battery::M_PRESCALER == 1024,
              "CONTROL_SCAN_M1024 encodes M=1024; update both together if M changes");

uint16_t codeToVoltageMv(uint16_t code) {
    return (uint16_t)((uint64_t)code * kVoltageFsMv / 65535);
}

int16_t codeToCurrentMa(uint16_t code) {
    int32_t delta = (int32_t)code - 32767;
    int64_t iMa = (int64_t)delta * cfg::battery::SENSE_FS_MV * 1000
                  / 32767 / cfg::battery::R_SENSE_MOHM;
    return (int16_t)iMa;
}

int16_t codeToTempCx10(uint16_t code) {
    // T(K) = 510 * code / 65535 → T(°C)*10 = 5100*code/65535 - 2731
    return (int16_t)((uint32_t)code * 5100 / 65535) - 2731;
}

} // namespace

bool BatteryModule::begin() {
    wire_->begin(cfg::pin::I2C_SDA, cfg::pin::I2C_SCL);
    wire_->setClock(100000);
    delay(10);

    if (!writeReg8(cfg::battery::REG_CONTROL, cfg::battery::CONTROL_SCAN_M1024)) {
        log_e("LTC2944 control write failed");
        present_ = false;
        return false;
    }
    delay(20);

    uint16_t status = 0;
    if (!readReg16(cfg::battery::REG_STATUS, status)) {
        log_e("LTC2944 status read failed");
        present_ = false;
        return false;
    }

    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) {
        log_e("Battery mutex alloc failed");
        present_ = false;
        return false;
    }

    uint16_t vCode = 0, iCode = 0, tCode = 0, acrCode = 0;
    readReg16(cfg::battery::REG_VOLT_HI, vCode);
    readReg16(cfg::battery::REG_CURR_HI, iCode);
    readReg16(cfg::battery::REG_TEMP_HI, tCode);
    readReg16(cfg::battery::REG_ACR_HI,  acrCode);

    voltageMv_ = codeToVoltageMv(vCode);
    currentMa_ = codeToCurrentMa(iCode);
    tempCx10_  = codeToTempCx10(tCode);
    lastACR_   = acrCode;   // baseline; first tick() will see delta = 0

    uint8_t initPct = ocvToPercent(voltageMv_);
    percent_    = initPct;
    coulombMAs_ = (int32_t)((uint64_t)initPct * kCapacityMAs / 100);
    lastTickMs_ = millis();
    staticSinceMs_ = 0;

    if (currentMa_ > cfg::battery::CURRENT_IDLE_THRESH_MA) {
        powerState_ = PowerState::Charging;
    } else if (currentMa_ >= -cfg::battery::CURRENT_IDLE_THRESH_MA &&
               voltageMv_ >= cfg::battery::V_FULL_THRESH_MV) {
        powerState_ = PowerState::Full;
    } else {
        powerState_ = PowerState::Discharging;
    }

    present_ = true;
    log_i("LTC2944 ok, V=%u mV, I=%d mA, T=%d.%dC, init=%u%%",
          voltageMv_, currentMa_,
          tempCx10_ / 10, (tempCx10_ < 0 ? -(tempCx10_ % 10) : (tempCx10_ % 10)),
          percent_);

    BaseType_t ok = xTaskCreatePinnedToCore(
        &BatteryModule::taskTrampoline,
        "batteryTask",
        cfg::task::BATTERY_STACK,
        this,
        cfg::task::BATTERY_PRIO,
        nullptr,
        cfg::task::APP_CORE);
    if (ok != pdPASS) {
        log_e("Battery task create failed");
        present_ = false;
        return false;
    }
    return true;
}

void BatteryModule::taskTrampoline(void* arg) {
    BatteryModule* self = static_cast<BatteryModule*>(arg);
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(1000U / cfg::battery::SAMPLE_HZ);
    while (true) {
        self->tick();
        if (self->notifyCb_) {
            self->notifyCb_(self->percent_, self->powerState_, self->notifyCtx_);
        }
        vTaskDelayUntil(&last, period);
    }
}

void BatteryModule::tick() {
    uint16_t vCode = 0, iCode = 0, tCode = 0, acrCode = 0;
    bool vOk   = readReg16(cfg::battery::REG_VOLT_HI, vCode);
    bool iOk   = readReg16(cfg::battery::REG_CURR_HI, iCode);
    bool tOk   = readReg16(cfg::battery::REG_TEMP_HI, tCode);
    bool acrOk = readReg16(cfg::battery::REG_ACR_HI,  acrCode);
    if (!vOk || !iOk || !acrOk) return;  // keep last cache on transient I2C failure

    const uint16_t vMv   = codeToVoltageMv(vCode);
    const int16_t  iMa   = codeToCurrentMa(iCode);
    const int16_t  tCx10 = tOk ? codeToTempCx10(tCode) : tempCx10_;

    const uint32_t now = millis();
    const uint32_t dt  = now - lastTickMs_;
    lastTickMs_ = now;

    // Coulomb integration via LTC2944 ACR (hardware-integrated charge).
    // ACR accumulates continuously independent of MCU sleep/I2C reads, so
    // deltas captured across light-sleep windows reflect real net charge.
    // uint16_t subtraction handles 16-bit wraparound; cast to int16_t
    // recovers signed direction (charging → positive, discharging → negative).
    uint16_t rawDelta = acrCode - lastACR_;
    int16_t  deltaLsb = (int16_t)rawDelta;
    coulombMAs_ += (int32_t)deltaLsb * (int32_t)ACR_LSB_MAS;
    lastACR_ = acrCode;

    if (iMa <  cfg::battery::CURRENT_IDLE_THRESH_MA &&
        iMa > -cfg::battery::CURRENT_IDLE_THRESH_MA) {
        staticSinceMs_ += dt;
    } else {
        staticSinceMs_ = 0;
    }

    const int32_t cap = (int32_t)kCapacityMAs;
    if (vMv >= cfg::battery::V_FULL_THRESH_MV && iMa > 0) {
        coulombMAs_ = cap;                                 // charging near full → saturate
    } else if (vMv < cfg::battery::V_EMPTY_MV) {
        coulombMAs_ = 0;                                   // deep discharge clamp
    } else if (staticSinceMs_ >= cfg::battery::STATIC_CALIB_MS) {
        uint8_t vPct = ocvToPercent(vMv);                  // rested → OCV reliable
        coulombMAs_ = (int32_t)((uint64_t)vPct * cap / 100);
    }
    if (coulombMAs_ > cap) coulombMAs_ = cap;
    if (coulombMAs_ < 0)   coulombMAs_ = 0;

    const uint8_t pct = (uint8_t)((uint32_t)coulombMAs_ * 100 / (uint32_t)cap);

    PowerState ps;
    if (iMa >  cfg::battery::CURRENT_IDLE_THRESH_MA) {
        ps = PowerState::Charging;
    } else if (iMa < -cfg::battery::CURRENT_IDLE_THRESH_MA) {
        ps = PowerState::Discharging;
    } else if (vMv >= cfg::battery::V_FULL_THRESH_MV) {
        ps = PowerState::Full;
    } else {
        ps = PowerState::Discharging;  // idle, not full → treat as slow discharge
    }

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
        percent_    = pct;
        voltageMv_  = vMv;
        currentMa_  = iMa;
        tempCx10_   = tCx10;
        powerState_ = ps;
        xSemaphoreGive(mutex_);
    }
}

uint8_t BatteryModule::getPercent() {
    if (!present_ || !mutex_) return percent_;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
        uint8_t p = percent_;
        xSemaphoreGive(mutex_);
        return p;
    }
    return percent_;
}

uint16_t BatteryModule::getVoltageMv() {
    if (!present_ || !mutex_) return voltageMv_;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
        uint16_t v = voltageMv_;
        xSemaphoreGive(mutex_);
        return v;
    }
    return voltageMv_;
}

int16_t BatteryModule::getCurrentMa() {
    if (!present_ || !mutex_) return currentMa_;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
        int16_t i = currentMa_;
        xSemaphoreGive(mutex_);
        return i;
    }
    return currentMa_;
}

int16_t BatteryModule::getTemperatureC() {
    int16_t tCx10 = tempCx10_;
    if (present_ && mutex_ &&
        xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
        tCx10 = tempCx10_;
        xSemaphoreGive(mutex_);
    }
    return tCx10 / 10;
}

uint8_t BatteryModule::ocvToPercent(uint16_t vMv) {
    if (vMv >= 4200) return 100;
    if (vMv <= 3000) return 0;
    constexpr size_t N = sizeof(kOcvTable) / sizeof(kOcvTable[0]);
    for (size_t i = 1; i < N; i++) {
        if (vMv < kOcvTable[i].vMv) {
            const OcvPoint& lo = kOcvTable[i - 1];
            const OcvPoint& hi = kOcvTable[i];
            uint32_t num = (uint32_t)(vMv - lo.vMv) * (hi.pct - lo.pct);
            uint32_t den = hi.vMv - lo.vMv;
            return lo.pct + (uint8_t)(num / den);
        }
    }
    return 100;
}

bool BatteryModule::readReg16(uint8_t hiReg, uint16_t& val) {
    wire_->beginTransmission(kI2cAddr);
    wire_->write(hiReg);
    if (wire_->endTransmission(false) != 0) return false;
    size_t got = wire_->requestFrom((uint8_t)kI2cAddr, (uint8_t)2);
    if (got != 2) return false;
    uint8_t hi = wire_->read();
    uint8_t lo = wire_->read();
    val = ((uint16_t)hi << 8) | lo;
    return true;
}

bool BatteryModule::writeReg8(uint8_t reg, uint8_t val) {
    wire_->beginTransmission(kI2cAddr);
    wire_->write(reg);
    wire_->write(val);
    return wire_->endTransmission() == 0;
}

} // namespace modules
