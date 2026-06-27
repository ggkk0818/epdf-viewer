#include "InputModule.h"
#include "../config/Config.h"

using app::InputEvent;

namespace modules {

namespace {

constexpr uint8_t IDX_BOOT = 0;
constexpr uint8_t IDX_BTN1 = 1;
constexpr uint8_t IDX_BTN2 = 2;

} // namespace

bool InputModule::begin() {
    btn_[IDX_BOOT] = { cfg::pin::BUTTON_BOOT,   false, false, 0, 0, false };
    btn_[IDX_BTN1] = { cfg::pin::BUTTON_CUSTOM, false, false, 0, 0, false };
    btn_[IDX_BTN2] = { cfg::pin::BUTTON_CUSTOM2,false, false, 0, 0, false };

    pinMode(cfg::pin::BUTTON_BOOT,    INPUT_PULLUP);
    pinMode(cfg::pin::BUTTON_CUSTOM,  INPUT_PULLUP);
    pinMode(cfg::pin::BUTTON_CUSTOM2, INPUT_PULLUP);

    queue_ = xQueueCreate(cfg::input::QUEUE_LEN, sizeof(InputEvent));
    if (!queue_) {
        log_e("input queue create failed");
        return false;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        &InputModule::taskTrampoline,
        "inputTask",
        cfg::task::INPUT_STACK,
        this,
        cfg::task::INPUT_PRIO,
        nullptr,
        cfg::task::APP_CORE);
    if (ok != pdPASS) {
        log_e("inputTask create failed");
        return false;
    }

    log_i("InputModule ready");
    return true;
}

void InputModule::taskTrampoline(void* arg) {
    InputModule* self = static_cast<InputModule*>(arg);
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(1000U / cfg::input::POLL_HZ);
    while (true) {
        self->poll();
        vTaskDelayUntil(&last, period);
    }
}

void InputModule::poll() {
    const uint32_t now = millis();

    for (uint8_t i = 0; i < 3; i++) {
        ButtonState& b = btn_[i];
        bool raw = !digitalRead(b.pin);
        if (raw != b.lastRawPressed) {
            b.lastRawPressed = raw;
            b.lastEdgeMs = now;
        }
        if ((now - b.lastEdgeMs) >= cfg::input::DEBOUNCE_MS && raw != b.debouncedPressed) {
            b.debouncedPressed = raw;
            if (raw) {
                b.pressedAtMs = now;
                b.longPressFired = false;
            }
            onButtonEdge(i, raw);
        }

        if (i == IDX_BOOT && b.debouncedPressed && !b.longPressFired &&
            (now - b.pressedAtMs) >= cfg::input::LONG_PRESS_MS) {
            b.longPressFired = true;
            emit(InputEvent::Back);
        }
    }
}

void InputModule::onButtonEdge(uint8_t idx, bool pressed) {
    if (idx == IDX_BOOT) {
        if (!pressed && !btn_[IDX_BOOT].longPressFired) {
            emit(InputEvent::Enter);
        }
    } else if (idx == IDX_BTN1) {
        if (pressed) emit(InputEvent::UpLeft);
    } else if (idx == IDX_BTN2) {
        if (pressed) emit(InputEvent::DownRight);
    }
}

void InputModule::emit(InputEvent e) {
    if (e == InputEvent::None) return;
    xQueueSendToBack(queue_, &e, 0);
}

} // namespace modules
