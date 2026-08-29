#include <cinttypes>
#include <cstdio>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 引入自己编写的 OLED 屏幕驱动类
#include "./display/face_engine.hpp"
#include "./display/oled_driver.hpp"

// 引入 I2S 数字音频播放器驱动
#include "./audio/audio_player.hpp"
#include "./audio/audio_recorder.hpp"

// 引入与下位机 STM32 共享的纯 C 语言通信协议
extern "C" {
#include "robot_protocol.h"
}

// 定义当前文件的日志 TAG
static const char *TAG = "ROBOT_BRAIN";

// 1. 实例化全局 OLED 屏幕对象 (SDA: GPIO 8, SCL: GPIO 9, I2C从机地址: 0x3C)
static OledDriver s_oled(8, 9, 0x3C);

// 2. 实例化顶层拟人表情引擎，并绑定屏幕驱动
static FaceEngine s_face(s_oled);

// 3. 实例化 MAX98357A 音频播放器 (BCLK: GPIO 16, LRC: GPIO 17, DIN: GPIO 15,采样率: 16000Hz)
static AudioPlayer s_audioPlayer(16, 17, 15, 16000);

// 4. 实例化 INMP441 麦克风录音器 (SCK: 6, WS: 5, SD: 4, 采样率: 16000Hz)
static AudioRecorder s_audioRecorder(6, 5, 4, 16000);

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
 * @brief 麦克风音频采集与声画联动后台任务 (运行在 CPU Core 0)
 */
void audioListenTask(void *pvParameters) {
    ESP_LOGI(TAG, "启动麦克风监听任务 (运行在 Core 0)...");

    // 初始化麦克风硬件
    if (s_audioRecorder.init() != ESP_OK) {
        ESP_LOGE(TAG, "❌ 麦克风初始化失败！请检查接线 (SD->GPIO4, WS->GPIO5, SCK-> GPIO6)");
        vTaskDelete(NULL);
        return;
    }

    // 每次读取 512 个采样点 (约 32 毫秒音频帧)
    const size_t FRAME_SAMPLES = 512;
    std::vector<int16_t> audio_frame(FRAME_SAMPLES);

    uint32_t last_sound_time_ms = 0;
    uint32_t last_print_time_ms = 0;

    while (true) {
        // 从麦克风 DMA 队列读取音频数据
        size_t samples_read = s_audioRecorder.read(audio_frame.data(), FRAME_SAMPLES);
        if (samples_read == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 计算当前帧的音量能量 (0.0 ~ 100.0)
        float volume = AudioRecorder::calculateVolume(audio_frame.data(), samples_read);
        uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);

        // --- 声画联动逻辑 ---
        if (volume > 15.0f) {
            // 检测到外界有说话声或吹气，眼睛立刻好奇放大！
            s_face.setEmotion(EmotionState::LISTENING);
            last_sound_time_ms = now_ms;
        } else if (now_ms - last_sound_time_ms > 2000) {
            // 持续 2 秒恢复安静，自动回到待机大眼睛与自然眨眼
            s_face.setEmotion(EmotionState::NORMAL);
        }

        // 每隔 200ms 在日志输出一次字符动态音量条
        if (now_ms - last_print_time_ms >= 200) {
            last_print_time_ms = now_ms;

            // 生成长度为 15 的动态字符柱状图
            char bar[16] = {0};
            int filled = static_cast<int>((volume / 100.0f) * 15);
            if (filled > 15)
                filled = 15;
            for (int i = 0; i < 15; i++) {
                bar[i] = (i < filled) ? '=' : ' ';
            }

            ESP_LOGI(TAG, "🎤 环境音量: [%s] %.1f%%", bar, volume);
        }
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

    // 2. 初始化 I2S 音频播放器硬件
    if (s_audioPlayer.init() == ESP_OK) {
        // 播放开机科技上升和弦音
        s_audioPlayer.playBootSound();
    }

    // 3. 创建独立 OLED 表情渲染任务 (栈大小 4096 字节，优先级 5，绑定在 Core 1)
    xTaskCreatePinnedToCore(oledRenderTask, "oled_task", 4096, nullptr, 5, nullptr, 1);

    // 4. 启动麦克风监听与声画联动线程 (Core 0)
    xTaskCreatePinnedToCore(audioListenTask, "audio_task", 4096, nullptr, 4, nullptr, 0);
    // // 3. 演示表情动态切换 (主线程每隔几秒切换一次表情)
    // while (true) {
    //     // 状态 1: 正常大眼睛 + 随机眨眼 (持续 6 秒)
    //     s_face.setEmotion(EmotionState::NORMAL);
    //     vTaskDelay(pdMS_TO_TICKS(6000));
    //     // 状态 2: 开心微笑月牙眼 (持续 3 秒)
    //     s_face.setEmotion(EmotionState::HAPPY);
    //     vTaskDelay(pdMS_TO_TICKS(3000));
    //     // 状态 3: 灵动倾听好奇大眼 (持续 3 秒)
    //     s_face.setEmotion(EmotionState::LISTENING);
    //     vTaskDelay(pdMS_TO_TICKS(3000));
    // }
}
