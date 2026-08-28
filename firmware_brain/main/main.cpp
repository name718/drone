#include <cstdio>

#include "esp_chip_info.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 日志标签 TAG 用于区分模块输出
static const char *TAG = "ROBOT_BRAIN";

extern "C" void app_main(void) {
    // 1. 等待上电和串口通信稳定
    vTaskDelay(pdMS_TO_TICKS(500));

    // 2. 打印系统启动 BANNER
    ESP_LOGI(TAG, "=========================================");
    ESP_LOGI(TAG, "  🤖 桌面自平衡机器人 · 大脑 (ESP32-S3)  ");
    ESP_LOGI(TAG, "=========================================");
    // 3. 读取并打印芯片硬件信息
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "CPU 核心数: %d, 修订版本: %d", chip_info.cores, chip_info.revision);
}
