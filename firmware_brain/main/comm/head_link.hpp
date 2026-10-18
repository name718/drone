/** @file head_link.hpp @brief ESP32头部代理：只有这一个服务能操作UART2。 */
#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "head_protocol.h"
class HeadLink {
public:
    static HeadLink &getInstance();
    esp_err_t init();
    esp_err_t start();
    void setEmotion(uint8_t emotion);
    void lookAt(float pan, float tilt);
    void gesture(uint8_t action);
    void enableServos(bool enable);
    void requestCamera(bool awake);
    HpState desired();
    HpStatus status();
    bool online();  // 状态新鲜且C8确认收到STATE心跳；仅单向回传不算控制在线。
    uint32_t statusAgeMs();

private:
    HeadLink() = default;
    static void task(void *context);
    static void received(const HpFrame *frame, void *context);
    portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    HpState desired_{HP_BOOTING, 0, HP_GESTURE_NONE, 0, 2900, 0, 0};
    HpStatus status_{};
    uint32_t received_ms_ = 0;
    bool have_status_ = false, initialized_ = false, running_ = false, camera_awake_ = false;
    TaskHandle_t task_handle_ = nullptr;
};
