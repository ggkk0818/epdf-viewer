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

constexpr uint16_t WIDTH        = 400;
constexpr uint16_t HEIGHT       = 300;
constexpr uint16_t STATUS_H     = 20;
constexpr uint16_t CONTENT_Y    = STATUS_H;
constexpr uint16_t CONTENT_H    = HEIGHT - STATUS_H;

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
constexpr uint32_t QUEUE_LEN      = 4;

} // namespace input

namespace task {

constexpr uint32_t INPUT_STACK   = 2048;
constexpr uint32_t APP_STACK     = 8192;
constexpr uint32_t BATTERY_STACK = 2048;
constexpr uint32_t DISPLAY_STACK = 4096;

constexpr UBaseType_t INPUT_PRIO    = 12;
constexpr UBaseType_t APP_PRIO      = 10;
constexpr UBaseType_t BATTERY_PRIO  = 5;
constexpr UBaseType_t DISPLAY_PRIO  = 8;

constexpr BaseType_t APP_CORE      = 1;
constexpr BaseType_t DISPLAY_CORE  = 0;

} // namespace task

namespace ble {

constexpr const char* DEVICE_NAME = "EPDF-Viewer";

} // namespace ble

namespace version {

constexpr const char* SW_VERSION = "0.1.0";

} // namespace version

} // namespace cfg
