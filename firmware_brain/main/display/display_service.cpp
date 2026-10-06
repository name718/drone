/**
 * @file display_service.cpp
 * @brief 视觉业务服务实现文件
 */
#include "display/display_service.hpp"

#include "esp_log.h"

static const char *TAG = "视觉服务";

DisplayService &DisplayService::getInstance() {
    static DisplayService instance;
    return instance;
}

DisplayService::DisplayService() : engine_(driver_) {}

esp_err_t DisplayService::init() {
    ESP_LOGI(TAG, "正在初始化屏幕底层外设...");
    esp_err_t ret = driver_.init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "屏幕硬件初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "屏幕硬件就绪，准备拉起渲染任务。");
    return ESP_OK;
}

esp_err_t DisplayService::start() {
    if (is_running_)
        return ESP_OK;
    is_running_ = true;

    // 创建专属视觉渲染任务，绑定在 Core 1 上运行 (优先级 5)
    // 机制：Core 0 处理网络与 Web，Core 1 专职高速表情渲染与运动，双核两不误！
    BaseType_t res = xTaskCreatePinnedToCore(renderTask, "DisplayTask", 4096, this,
                                             5,  // 优先级 5 (中等优先级)
                                             &task_handle_,
                                             1  // 绑定在 Core 1
    );

    if (res != pdPASS) {
        ESP_LOGE(TAG, "创建视觉渲染任务失败！");
        is_running_ = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "视觉服务已在 Core 1 启动 (33 FPS 高帧率渲染)！");
    return ESP_OK;
}

void DisplayService::setEmotion(EmotionState emotion) {
    engine_.setEmotion(emotion);
}

void DisplayService::renderTask(void *param) {
    auto *self = static_cast<DisplayService *>(param);

    while (self->is_running_) {
        // 执行单帧数学计算与推流
        self->engine_.update();

        // 保证让出 CPU 1 算力，防止饿死 IDLE1 引发看门狗超时
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    vTaskDelete(nullptr);
}
