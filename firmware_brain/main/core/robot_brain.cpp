/**
 * @file robot_brain.cpp
 * @brief 机器人大脑总控中枢实现
 */

#include "robot_brain.hpp"
#include <cinttypes>
#include <cstdio>
#include <vector>
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "ROBOT_BRAIN";

RobotBrain& RobotBrain::getInstance() {
    static RobotBrain s_instance;
    return s_instance;
}

RobotBrain::RobotBrain()
    : m_display(8, 9, 0x3C),
      m_audio(16, 17, 15, 6, 5, 4, 16000),
      m_motion(1, 2, 115200, UART_NUM_2) {
}

void RobotBrain::printSystemDiagnostics() {
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "  🤖 桌面自平衡机器人 · 大脑系统自检 (ESP32-S3)  ");
    ESP_LOGI(TAG, "=================================================");

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "CPU 核心数: %d, 特性掩码: 0x%08" PRIx32 ", 芯片版本: %d", chip_info.cores,
             chip_info.features, chip_info.revision);

    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        ESP_LOGI(TAG, "Flash 容量: %lu MB", (unsigned long)(flash_size / (1024 * 1024)));
    }

    size_t psram_size = esp_psram_get_size();
    if (psram_size > 0) {
        ESP_LOGI(TAG, "✅ 8MB PSRAM (Octal) 挂载成功! 容量: %u KB (%.2f MB)",
                 (unsigned int)(psram_size / 1024), (float)psram_size / (1024 * 1024));
    }

    ESP_LOGI(TAG, "内部 SRAM 空闲堆: %lu 字节", (unsigned long)esp_get_free_internal_heap_size());
    ESP_LOGI(TAG, "系统总可用空闲堆: %lu 字节", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "=================================================");
}

esp_err_t RobotBrain::init() {
    // 延时等待供电稳定
    vTaskDelay(pdMS_TO_TICKS(500));

    // 1. 打印硬件诊断报告
    printSystemDiagnostics();

    // 2. 初始化视觉子系统
    m_display.init();

    // 3. 初始化音频子系统
    m_audio.init();

    // 4. 初始化底盘通信子系统
    m_motion.init();

    ESP_LOGI(TAG, "✅ 大脑所有子系统初始化完成！");
    return ESP_OK;
}

esp_err_t RobotBrain::start() {
    ESP_LOGI(TAG, "🚀 启动大脑多核任务系统...");

    // 1. 播放开机科技上升和弦音
    m_audio.playBootSound();

    // 2. 启动视觉渲染服务 (Core 1)
    m_display.start();

    // 3. 启动 50Hz 底盘运动控制服务 (Core 1)
    m_motion.start();

    // 4. 启动人机交互监听任务 (Core 0)
    xTaskCreatePinnedToCore(
        interactionTaskEntry,
        "brain_interact",
        4096,
        this,
        4,
        nullptr,
        0 // 绑定在 Core 0
    );

    return ESP_OK;
}

void RobotBrain::interactionTaskEntry(void* pvParameters) {
    auto* self = static_cast<RobotBrain*>(pvParameters);
    const size_t FRAME_SAMPLES = 512;
    std::vector<int16_t> audio_frame(FRAME_SAMPLES);

    uint32_t last_sound_time_ms = 0;

    while (true) {
        // 从麦克风 DMA 读取一帧数据
        size_t samples_read = self->m_audio.readRecordFrame(audio_frame.data(), FRAME_SAMPLES);
        if (samples_read == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 计算当前环境音量
        float volume = AudioService::calculateVolume(audio_frame.data(), samples_read);
        uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);

        // 视听声画联动
        if (volume > 15.0f) {
            self->m_display.setEmotion(EmotionState::LISTENING); // 灵动大眼
            last_sound_time_ms = now_ms;
        } else if (now_ms - last_sound_time_ms > 2000) {
            self->m_display.setEmotion(EmotionState::NORMAL);    // 待机大眼
        }
    }
}
