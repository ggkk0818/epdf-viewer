#pragma once

#include <cstdint>

namespace cfg {

namespace pin {

constexpr int8_t SD_CS   = 46;
constexpr int8_t SD_MOSI = 11;
constexpr int8_t SD_MISO = 13;
constexpr int8_t SD_SCK  = 12;

constexpr int8_t EPD_CS   = 48;
constexpr int8_t EPD_DC   = 47;
constexpr int8_t EPD_RST  = 21;
constexpr int8_t EPD_BUSY = 45;
constexpr int8_t EPD_SCK  = 12;
constexpr int8_t EPD_MOSI = 11;

constexpr int8_t I2C_SDA = 9;
constexpr int8_t I2C_SCL = 10;

constexpr int8_t BUTTON_BOOT   = 0;
constexpr int8_t BUTTON_CUSTOM = 41;
constexpr int8_t BUTTON_CUSTOM2 = 42;

} // namespace pin

namespace fs {

constexpr const char* SD_ROOT      = "/";
constexpr const char* PDF_DIR      = "/pdf";
constexpr const char* ICON_DIR     = "/sys/icon";
constexpr const char* PAGE_SUFFIX  = ".bin";

} // namespace fs

namespace display {

constexpr uint16_t WIDTH        = 640;
constexpr uint16_t HEIGHT       = 960;
constexpr uint16_t STATUS_H     = 32;
constexpr uint16_t CONTENT_Y    = STATUS_H;
constexpr uint16_t CONTENT_H    = HEIGHT - STATUS_H;
constexpr uint16_t CONTENT_W    = WIDTH;

constexpr uint16_t ICON_SIZE    = 48;
constexpr uint16_t GRID_COLS    = 2;
constexpr uint16_t GRID_ROWS    = 2;
constexpr uint16_t GRID_PAD     = 16;

constexpr uint32_t SPI_HZ = 4000000;

} // namespace display

namespace battery {

// LTC2944 I2C address (7-bit, fixed by ADI).
constexpr uint8_t  I2C_ADDR              = 0x64;

// Sampling task cadence.
constexpr uint32_t SAMPLE_HZ             = 1;

// Sense resistor and full-scale (LTC2944 datasheet: differential ±64 mV).
constexpr uint16_t SENSE_FS_MV           = 64;
constexpr uint16_t R_SENSE_MOHM          = 50;

// Battery spec (single 4.2V Li-ion cell, nominal 3.7V).
constexpr uint16_t CAPACITY_MAH          = 2000;
constexpr uint16_t V_FULL_MV             = 4200;
constexpr uint16_t V_FULL_THRESH_MV      = 4150;
constexpr uint16_t V_EMPTY_MV            = 3250;

// PowerState thresholds.
constexpr int16_t  CURRENT_IDLE_THRESH_MA = 20;
constexpr uint32_t STATIC_CALIB_MS        = 30000;  // 30s rest → trust OCV

// LTC2944 registers.
constexpr uint8_t  REG_STATUS            = 0x00;
constexpr uint8_t  REG_CONTROL           = 0x01;
constexpr uint8_t  REG_ACR_HI            = 0x02;
constexpr uint8_t  REG_ACR_LO            = 0x03;
constexpr uint8_t  REG_VOLT_HI           = 0x08;
constexpr uint8_t  REG_VOLT_LO           = 0x09;
constexpr uint8_t  REG_CURR_HI           = 0x0E;
constexpr uint8_t  REG_CURR_LO           = 0x0F;
constexpr uint8_t  REG_TEMP_HI           = 0x14;
constexpr uint8_t  REG_TEMP_LO           = 0x15;

// Control word: ADC mode=Scan (10), M=1024 (101), ALCC=off (00), !shutdown (0).
constexpr uint8_t  CONTROL_SCAN_M1024    = 0xA8;

} // namespace battery

namespace input {

constexpr uint32_t POLL_HZ        = 50;
constexpr uint32_t DEBOUNCE_MS    = 20;
constexpr uint32_t LONG_PRESS_MS  = 600;
constexpr uint32_t QUEUE_LEN      = 16;

} // namespace input

namespace task {

constexpr uint32_t INPUT_STACK   = 2048;
constexpr uint32_t APP_STACK     = 8192;
constexpr uint32_t BATTERY_STACK = 2048;
constexpr uint32_t DISPLAY_STACK = 4096;
constexpr uint32_t DOC_LOAD_STACK = 4096;

constexpr UBaseType_t INPUT_PRIO    = 12;
constexpr UBaseType_t APP_PRIO      = 10;
constexpr UBaseType_t BATTERY_PRIO  = 5;
constexpr UBaseType_t DISPLAY_PRIO  = 8;
constexpr UBaseType_t DOC_LOAD_PRIO = 9;

constexpr BaseType_t APP_CORE      = 1;
constexpr BaseType_t DISPLAY_CORE  = 0;
constexpr BaseType_t DOC_LOAD_CORE = APP_CORE;

} // namespace task

namespace ble {

constexpr const char* DEVICE_NAME = "EPDF-Viewer";

// GATT Service / Characteristic UUIDs (custom EPDF service).
// Note: 16-bit aliases 0xFFE0/0xFFE1/0xFFE2 are commonly used by HM-10 modules
// and most BLE explorers (nRF Connect, LightBlue) display them readably.
constexpr const char* EPDF_SERVICE_UUID = "0000ffe0-0000-1000-8000-00805f9b34fb";
constexpr const char* CMD_CHAR_UUID     = "0000ffe1-0000-1000-8000-00805f9b34fb";
constexpr const char* DATA_CHAR_UUID    = "0000ffe2-0000-1000-8000-00805f9b34fb";

// MTU / PHY / connection parameters.
// Connection interval is in BLE units of 1.25ms: 0x10 = 20ms, 0x20 = 40ms.
constexpr uint16_t TARGET_MTU      = 512;
constexpr uint16_t CONN_INT_MIN    = 0x10;  // 20ms
constexpr uint16_t CONN_INT_MAX    = 0x20;  // 40ms
constexpr uint16_t CONN_LATENCY    = 0;
constexpr uint16_t CONN_TIMEOUT    = 400;   // 4s (units of 10ms)
constexpr size_t   MAX_CMD_LINE    = 256;   // JSON cmd line buffer
constexpr size_t   MAX_DATA_CHUNK  = 512;   // queued BLE write payload upper bound

// BLE-side work task (must not run on BLE host task — SD writes would block it).
constexpr uint32_t WORK_STACK      = 8192;
constexpr UBaseType_t WORK_PRIO    = 6;
constexpr BaseType_t WORK_CORE     = 1;
constexpr uint32_t  CMD_QUEUE_LEN  = 8;

// Auto-disable watchdog: when enabled but no peer connects for this long,
// fully tear down the BLE stack to save power. Timer is reset on enable and
// on disconnect; a connected peer pauses the check (connected_ == true).
constexpr uint32_t  AUTO_DISABLE_MS   = 10U * 60U * 1000U;  // 10 min
constexpr uint32_t  WATCHDOG_PERIOD_MS = 5000;              // poll interval
constexpr uint32_t  WATCHDOG_STACK    = 2048;
constexpr UBaseType_t WATCHDOG_PRIO   = 5;                   // same level as battery
constexpr BaseType_t WATCHDOG_CORE    = 1;                   // app core, not display core

} // namespace ble

namespace pdf {

// Page dimension caps (defensive). 1bpp page size = ceil(width/8) * height.
// 640x960 ≈ 75KB; 2048x4096 ≈ 1MB. Cap at 2MB to bound UploadSession state.
constexpr uint16_t MAX_PAGE_DIM_W  = 2048;
constexpr uint16_t MAX_PAGE_DIM_H  = 4096;
constexpr size_t   MAX_PAGE_BYTES  = 2 * 1024 * 1024;
constexpr size_t   SD_WRITE_BUF    = 4096;

// Directory name format: yyyy-mm-dd_HH-MM-SS_PPP_name
//   PPP = 3-digit page count, zero-padded (max 999 pages)
//   name may contain any chars except '_' (reserved as separator)
constexpr uint8_t  DIR_PAGE_FIELD_WIDTH = 3;
constexpr uint8_t  DIR_NAME_PREFIX_LEN  = 19;  // "yyyy-mm-dd_HH-MM-SS_"

} // namespace pdf

namespace ota {

// GATT Service / Characteristic UUIDs for the OTA service. Separate from the
// EPDF service (0xFFE0) so OTA traffic does not collide with the JSON cmd
// protocol or PDF data stream.
constexpr const char* OTA_SERVICE_UUID     = "0000ff00-0000-1000-8000-00805f9b34fb";
constexpr const char* OTA_CTRL_CHAR_UUID   = "0000ff01-0000-1000-8000-00805f9b34fb";
constexpr const char* OTA_DATA_CHAR_UUID   = "0000ff02-0000-1000-8000-00805f9b34fb";

// Flow control: ESP32 sends a 0x10 ack every ACK_INTERVAL_BYTES written so the
// phone can advance its sliding window. Window size is enforced on the phone
// side; this constant only sets how often the device reports progress.
constexpr uint32_t ACK_INTERVAL_BYTES = 4096;

// Worker task that drains the OTA queue. Flash erase/write happens here so the
// NimBLE host task is never blocked.
constexpr uint32_t  WORK_STACK  = 8192;
constexpr UBaseType_t WORK_PRIO = 6;     // same level as BLE work task
constexpr BaseType_t WORK_CORE  = 1;     // app core, not display core
constexpr size_t    QUEUE_LEN   = 16;    // 16 * ~512B = 8KB buffer
constexpr size_t    MAX_CHUNK   = 512;

// Protocol byte values — phone and ESP32 must agree on these exactly.
namespace cmd {
constexpr uint8_t START  = 0x01;   // payload: 4-byte LE firmware size
constexpr uint8_t PAUSE  = 0x02;   // optional, not strictly enforced
constexpr uint8_t RESUME = 0x03;   // optional, not strictly enforced
constexpr uint8_t END    = 0x04;   // payload: 4-byte LE CRC32 of firmware
constexpr uint8_t REBOOT = 0x05;
} // namespace cmd

namespace status {
constexpr uint8_t ACK       = 0x10;   // payload: 4-byte LE bytes-written-so-far
constexpr uint8_t START_FAIL = 0x11;  // payload: 1-byte error code
constexpr uint8_t CRC_FAIL  = 0x12;
constexpr uint8_t CRC_OK    = 0x13;
} // namespace status

namespace err {
constexpr uint8_t NONE          = 0x00;
constexpr uint8_t ALREADY_ACTIVE = 0x01;
constexpr uint8_t BEGIN_FAILED  = 0x02;  // Update.begin() rejected size
constexpr uint8_t WRITE_FAILED  = 0x03;  // Update.write() returned short
constexpr uint8_t UNEXPECTED    = 0x04;  // data arrived without START
} // namespace err

} // namespace ota

namespace version {

constexpr const char* SW_VERSION = "0.2.0";

} // namespace version

} // namespace cfg
