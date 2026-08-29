/**
 * @file display_service.cpp
 * @brief 机器人视觉与表情后台管理服务实现
 */

#include "display_service.hpp"
#include "esp_log.h"

static const char* TAG = "DISPLAY_SERVICE";

DisplayService::DisplayService(int sda_pin, int scl_pin, uint8_t i2c_addr)
    : m_oled(sda_pin, scl_pin, i2c_addr),
      m_face(m_oled),
      m_taskHandle(nullptr) {
}

DisplayService::~DisplayService() {
    if (m_taskHandle) {
        vTaskDelete(m_taskHandle);
    }
}

esp_err_t DisplayService::init() {
    ESP_LOGI(TAG, "初始化 OLED 屏幕硬件...");
    return m_oled.init();
}

esp_err_t DisplayService::start() {
    ESP_LOGI(TAG, "启动表情渲染后台任务 (运行在 CPU Core 1)...");

    // 默认进入正常大眼待机状态
    m_face.setEmotion(EmotionState::NORMAL);

    BaseType_t ret = xTaskCreatePinnedToCore(
        renderTaskEntry,
        "display_task",
        4096,
        this,
        5,
        &m_taskHandle,
        1 // 绑定在 Core 1
    );

    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}

void DisplayService::setEmotion(EmotionState emotion) {
    m_face.setEmotion(emotion);
}

void DisplayService::renderTaskEntry(void* pvParameters) {
    auto* self = static_cast<DisplayService*>(pvParameters);

    while (true) {
        // 调用表情引擎的心跳更新 (内部自动计算眨眼插值、清屏与刷屏)
        self->m_face.update();

        // 30ms 刷新一帧 (~33 FPS 丝滑动画)
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
