#include <Arduino.h>
#include "config/Config.h"
#include "modules/SdModule.h"
#include "modules/DisplayModule.h"
#include "modules/BatteryModule.h"
#include "modules/BleModule.h"
#include "modules/InputModule.h"
#include "modules/IconStore.h"
#include "modules/PdfStore.h"
#include "modules/UploadSession.h"
#include "modules/BleDataTransport.h"
#include "modules/BleCmdDispatcher.h"
#include "ui/UiCommon.h"
#include "ui/MainPage.h"
#include "app/AppController.h"

using modules::SdModule;
using modules::DisplayModule;
using modules::BatteryModule;
using modules::BleModule;
using modules::InputModule;
using modules::IconStore;
using modules::PdfStore;
using modules::BleDataTransport;
using modules::BleCmdDispatcher;

static SdModule         g_sd;
static DisplayModule    g_display;
static BatteryModule    g_battery;
static BleModule        g_ble;
static InputModule      g_input;
static IconStore        g_icons;
static PdfStore         g_pdf;
static BleDataTransport g_transport;
static BleCmdDispatcher g_dispatcher;
static ui::UiCommon     g_ui;
static app::AppController g_app;

static void batteryTask(void* arg) {
    app::AppController* a = static_cast<app::AppController*>(arg);
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(1000U / cfg::battery::SAMPLE_HZ);
    while (true) {
        uint8_t pct = a->battery().getPercent();
        a->ble().setBatteryLevel(pct);
        vTaskDelayUntil(&last, period);
    }
}

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true); // 把 ESP_LOGx/log_x 输出到 Serial
    log_i("=== EPDF Viewer boot ===");

    if (!psramFound()) {
        log_w("PSRAM not found");
    } else {
        log_i("PSRAM size: %u bytes", (unsigned)ESP.getPsramSize());
    }

    bool sdOk = g_sd.begin();
    if (sdOk) {
        g_icons.begin();
    } else {
        log_w("SD init failed, continuing without SD");
    }

    bool dispOk = g_display.begin();
    if (!dispOk) log_e("Display init failed");

    bool batOk = g_battery.begin();
    if (!batOk) log_w("Battery gauge init failed");

    bool bleOk = g_ble.begin();
    if (!bleOk) log_w("BLE init failed");

    bool inOk = g_input.begin();
    if (!inOk) log_e("Input init failed");

    g_pdf.begin(&g_sd);
    g_ui.begin(&g_display, &g_battery, &g_ble, &g_icons);
    g_app.begin(&g_display, &g_input, &g_battery, &g_ble, &g_sd, &g_pdf, &g_icons, &g_ui);

    if (g_dispatcher.begin(&g_ble, &g_pdf, &g_sd, &g_battery, &g_transport, &g_app)) {
        g_dispatcher.start();
    } else {
        log_e("BleCmdDispatcher init failed");
    }

    // Power-on default: Bluetooth discoverable. The BLE watchdog inside
    // BleModule will auto-disable the stack after AUTO_DISABLE_MS with no
    // peer connection.
    if (bleOk) {
        g_ble.setEnabled(true);
    }

    g_app.pushPage(new ui::MainPage());
    g_display.armRendering();

    xTaskCreatePinnedToCore(
        &batteryTask,
        "batteryTask",
        cfg::task::BATTERY_STACK,
        &g_app,
        cfg::task::BATTERY_PRIO,
        nullptr,
        cfg::task::APP_CORE);

    g_app.start();
    log_i("=== boot complete ===");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
