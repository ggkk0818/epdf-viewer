#include <Arduino.h>
#include <esp_sleep.h>
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
#include "modules/OtaService.h"
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
using modules::OtaService;

static SdModule         g_sd;
static DisplayModule    g_display;
static BatteryModule    g_battery;
static BleModule        g_ble;
static InputModule      g_input;
static IconStore        g_icons;
static PdfStore         g_pdf;
static BleDataTransport g_transport;
static BleCmdDispatcher g_dispatcher;
static OtaService       g_ota;
static ui::UiCommon     g_ui;
static app::AppController g_app;

static void onBatteryUpdate(uint8_t pct, modules::PowerState /*state*/, void* ctx) {
    static_cast<modules::BleModule*>(ctx)->setBatteryLevel(pct);
}

// 深度休眠唤醒闸门。在 setup() 最早执行 —— 在任何重初始化之前。
// 唤醒等价于 reset，所以"按键持续时长"只能从唤醒瞬间开始测。
//   - 冷启动 / 非 EXT0 唤醒：武装 ext0 唤醒源后放行正常初始化。
//   - EXT0 唤醒：测 Boot 持续时长，<1s 则重入休眠，≥1s 等释放后正常初始化。
static void handleWakeGate() {
    pinMode(cfg::pin::BUTTON_BOOT, INPUT_PULLUP);

    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause != ESP_SLEEP_WAKEUP_EXT0) {
        // 冷启动。武装唤醒源（与唤醒后路径对等），然后正常初始化。
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
        return;
    }

    // Boot 唤醒。先空过 50ms 避开 RTC→PLL 切换瞬间的 GPIO 毛刺。
    const uint32_t start = millis();
    while (millis() - start < cfg::sleep::WAKE_SETTLE_GUARD_MS) {
        vTaskDelay(pdMS_TO_TICKS(cfg::sleep::WAKE_RELEASE_POLL_MS));
    }

    // 测量 Boot 持续按下时长。释放或达到 WAKE_HOLD_MIN_MS 都会退出。
    uint32_t heldMs = cfg::sleep::WAKE_SETTLE_GUARD_MS;
    while (digitalRead(cfg::pin::BUTTON_BOOT) == LOW &&
           heldMs < cfg::sleep::WAKE_HOLD_MIN_MS) {
        vTaskDelay(pdMS_TO_TICKS(cfg::sleep::WAKE_RELEASE_POLL_MS));
        heldMs = millis() - start;
    }

    if (heldMs < cfg::sleep::WAKE_HOLD_MIN_MS) {
        // 按合时间不够 —— 用户只是短按，重入深度休眠。
        log_i("wake: boot released early (held=%u ms), re-sleeping", (unsigned)heldMs);
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
        esp_deep_sleep_start();  // 不返回
    }

    log_i("wake: boot held long enough (held=%u ms), waiting for release",
          (unsigned)heldMs);
    // 等用户释放，避免 InputModule 首次轮询看到"仍按下"导致释放时发 Enter。
    while (digitalRead(cfg::pin::BUTTON_BOOT) == LOW) {
        vTaskDelay(pdMS_TO_TICKS(cfg::sleep::WAKE_RELEASE_POLL_MS));
    }
    // 重新武装唤醒源（与冷启动路径对等）。
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
}

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true); // 把 ESP_LOGx/log_x 输出到 Serial
    log_i("=== EPDF Viewer boot ===");

    // 在任何重初始化之前判断深度休眠唤醒状态。
    handleWakeGate();

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
    if (batOk) {
        g_battery.setNotifyCallback(&onBatteryUpdate, &g_ble);
    } else {
        log_w("Battery gauge init failed");
    }

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

    if (g_ota.begin(&g_ble) && g_ota.start()) {
        log_i("OTA service ready");
    } else {
        log_e("OTA service init failed");
    }

    // Power-on default: Bluetooth discoverable. The BLE watchdog inside
    // BleModule will auto-disable the stack after AUTO_DISABLE_MS with no
    // peer connection.
    if (bleOk) {
        g_ble.setEnabled(true);
    }

    g_app.pushPage(new ui::MainPage());
    g_display.armRendering();

    g_app.start();
    log_i("=== boot complete ===");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
