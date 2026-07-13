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

constexpr uint16_t WIDTH        = 300;
constexpr uint16_t HEIGHT       = 400;
constexpr uint16_t STATUS_H     = 20;
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

constexpr uint8_t  I2C_ADDR      = 0x0B;
constexpr uint8_t  REG_RSOC      = 0x09;
constexpr uint8_t  REG_VOLTAGE   = 0x0A;
constexpr uint8_t  REG_TEMP      = 0x08;
constexpr uint8_t  REG_POWER     = 0x05;
constexpr uint32_t SAMPLE_HZ     = 1;

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

namespace version {

constexpr const char* SW_VERSION = "0.1.0";

} // namespace version

} // namespace cfg
