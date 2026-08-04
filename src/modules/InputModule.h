#pragma once

#include <Arduino.h>
#include <freertos/queue.h>
#include "../app/InputEvent.h"

namespace modules {

class InputModule {
public:
    bool begin();
    void poll();

    // 删除 inputTask。供关机流程调用，避免后续轮询把幽灵事件塞进队列。
    void stop();

    // 设置静默截止时刻(millis):poll() 在此之前直接 return,不发任何事件。
    // 用于开机静默期;将来也可复用于勿扰/OTA 等场景。
    // 必须在 begin() 之前调用,避免与 inputTask 竞态。
    void setQuietUntil(uint32_t quietUntilMs) { quietUntilMs_ = quietUntilMs; }

    QueueHandle_t eventQueue() const { return queue_; }

private:
    static void taskTrampoline(void* arg);

    struct ButtonState {
        uint8_t pin;
        bool    lastRawPressed;
        bool    debouncedPressed;
        uint32_t lastEdgeMs;
        uint32_t pressedAtMs;
        bool    longPressFired;
        bool    powerDownFired;
    };

    ButtonState btn_[3];
    QueueHandle_t queue_ = nullptr;
    TaskHandle_t  task_  = nullptr;
    uint32_t overwrittenCount_ = 0;
    uint32_t droppedCount_ = 0;
    uint32_t quietUntilMs_ = 0;

    void emit(app::InputEvent e);
    void onButtonEdge(uint8_t idx, bool pressed);
};

} // namespace modules
