#include <cinttypes>
#include <cstdio>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 引入我们自己编写的 OLED 屏幕驱动类
#include "./display/face_engine.hpp"
#include "./display/oled_driver.hpp"

// 引入与下位机 STM32 共享的纯 C 语言通信协议
extern "C" {
#include "robot_protocol.h"
}

// 定义当前文件的日志 TAG
static const char *TAG = "ROBOT_BRAIN";

// 实例化全局 OLED 屏幕对象 (SDA: GPIO 8, SCL: GPIO 9, I2C从机地址: 0x3C)
static OledDriver s_oled(8, 9, 0x3C);

// 2. 实例化顶层拟人表情引擎，并绑定屏幕驱动
static FaceEngine s_face(s_oled);

/**
 * @brief OLED 眼睛与表情渲染后台任务 (运行在 CPU Core 1)
 */
void oledRenderTask(void *pvParameters) {
    ESP_LOGI(TAG, "启动 OLED 渲染任务 (运行在 Core 1)...");

    // 1. 初始化 I2C 总线并给 OLED 发送上电指令序列
    esp_err_t ret = s_oled.init();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ OLED 硬件初始化失败！请检查接线 (SDA->GPIO8, SCL->GPIO9)");
        vTaskDelete(NULL);  // 初始化失败则销毁本任务
        return;
    }
    // 默认进入正常大眼待机状态
    s_face.setEmotion(EmotionState::NORMAL);

    // 渲染主循环 (~33 FPS 丝滑动画)
    while (true) {
        // 调用表情引擎的心跳更新 (内部自动计算眨眼插值、清屏与刷屏)
        s_face.update();

        // 30ms 刷新一帧，眨眼过程极其流畅
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

/**
 * @brief 系统硬件自检与信息输出
 */
void printSystemDiagnostics() {
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "  🤖 桌面自平衡机器人 · 大脑系统诊断 (ESP32-S3)  ");
    ESP_LOGI(TAG, "=================================================");

    // 1. CPU 核心与特性
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "CPU 核心数: %d, 特性掩码: 0x%08" PRIx32 ", 芯片版本: %d", chip_info.cores,
             chip_info.features, chip_info.revision);

    // 2. 16MB Flash 检测
    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        ESP_LOGI(TAG, "Flash 容量: %lu MB", (unsigned long)(flash_size / (1024 * 1024)));
    }

    // 3. 8MB Octal PSRAM 检测
    size_t psram_size = esp_psram_get_size();
    if (psram_size > 0) {
        ESP_LOGI(TAG, "✅ 8MB PSRAM (Octal) 挂载成功! 容量: %u KB (%.2f MB)",
                 (unsigned int)(psram_size / 1024), (float)psram_size / (1024 * 1024));
    } else {
        ESP_LOGE(TAG, "❌ PSRAM 未检测到！请检查 sdkconfig 配置。");
    }

    // 4. 堆内存分布 (SRAM 与 PSRAM)
    ESP_LOGI(TAG, "内部 SRAM 空闲堆: %lu 字节", (unsigned long)esp_get_free_internal_heap_size());
    ESP_LOGI(TAG, "系统总可用空闲堆: %lu 字节", (unsigned long)esp_get_free_heap_size());

    // 5. 跨芯片协议数据包大小校验
    ESP_LOGI(TAG, "-------------------------------------------------");
    ESP_LOGI(TAG, "通信协议 (robot_protocol.h) 校验:");
    ESP_LOGI(TAG, "  • 控制帧 RobotCmdPacket_t 大小: %u 字节",
             (unsigned int)sizeof(RobotCmdPacket_t));
    ESP_LOGI(TAG, "  • 状态帧 RobotStatePacket_t 大小: %u 字节",
             (unsigned int)sizeof(RobotStatePacket_t));
    ESP_LOGI(TAG, "=================================================");
}

/**
 * @brief C++ 主函数入口
 */
extern "C" void app_main(void) {
    // 延时 500ms 等待电源和串口完全稳定
    vTaskDelay(pdMS_TO_TICKS(500));
    // 1. 打印系统启动诊断信息
    printSystemDiagnostics();
    // 2. 创建独立 OLED 表情渲染任务 (栈大小 4096 字节，优先级 5，绑定在 Core 1)
    xTaskCreatePinnedToCore(oledRenderTask, "oled_task", 4096, nullptr, 5, nullptr, 1);

    // 3. 演示表情动态切换 (主线程每隔几秒切换一次表情)
    while (true) {
        // 状态 1: 正常大眼睛 + 随机眨眼 (持续 6 秒)
        s_face.setEmotion(EmotionState::NORMAL);
        vTaskDelay(pdMS_TO_TICKS(6000));
        // 状态 2: 开心微笑月牙眼 (持续 3 秒)
        s_face.setEmotion(EmotionState::HAPPY);
        vTaskDelay(pdMS_TO_TICKS(3000));
        // 状态 3: 灵动倾听好奇大眼 (持续 3 秒)
        s_face.setEmotion(EmotionState::LISTENING);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
