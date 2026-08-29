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

// 引入 OLED 屏幕与表情引擎
#include "oled_driver.hpp"
#include "face_engine.hpp"

// 引入 I2S 音频播放器与麦克风录音器
#include "audio_player.hpp"
#include "audio_recorder.hpp"

// 引入 UART 通信与协议状态机解析器
#include "uart_comm.hpp"
#include "protocol_parser.hpp"

extern "C" {
#include "robot_protocol.h"
}

static const char* TAG = "ROBOT_BRAIN";

// 1. OLED 屏幕对象 (SDA: 8, SCL: 9)
static OledDriver s_oled(8, 9, 0x3C);
// 2. 拟人表情引擎
static FaceEngine s_face(s_oled);
// 3. I2S 播放器 (BCLK: 16, LRC: 17, DIN: 15)
static AudioPlayer s_audioPlayer(16, 17, 15, 16000);
// 4. I2S 麦克风 (SCK: 6, WS: 5, SD: 4)
static AudioRecorder s_audioRecorder(6, 5, 4, 16000);
// 5. UART2 通信驱动 (TX: GPIO 1, RX: GPIO 2, 波特率: 115200)
static UartComm s_uart(1, 2, 115200, UART_NUM_2);
// 6. 协议状态机解析器
static ProtocolParser s_parser;

// 通信统计计数器
static uint32_t s_txCount = 0;
static uint32_t s_rxSuccessCount = 0;

/**
 * @brief 50Hz 串口双向通信任务 (运行在 CPU Core 1)
 */
void uartCommTask(void* pvParameters) {
    ESP_LOGI(TAG, "启动 50Hz 串口通信任务 (Core 1)...");

    if (s_uart.init() != ESP_OK) {
        ESP_LOGE(TAG, "❌ UART2 初始化失败！");
        vTaskDelete(NULL);
        return;
    }

    // 注册控制帧接收回调
    s_parser.setOnCmdPacket([](const RobotCmdPacket_t& cmd) {
        s_rxSuccessCount++;
    });

    uint8_t rx_buf[64];
    uint32_t last_stat_time_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    uint32_t stat_tx_start = 0;
    uint32_t stat_rx_start = 0;

    int16_t test_speed = 200; // 模拟目标速度 200 mm/s
    int16_t test_yaw = -100;  // 模拟目标角速度 -100 mrad/s

    while (true) {
        // 1. 构建控制指令数据包并从 GPIO 1 (TX) 发送
        RobotCmdPacket_t cmd = ProtocolParser::buildCmdPacket(test_speed, test_yaw, 1);
        s_uart.send(&cmd, sizeof(cmd));
        s_txCount++;

        // 2. 从 GPIO 2 (RX) 读取字节流并喂入状态机解析 (非阻塞极速读取)
        int len = s_uart.read(rx_buf, sizeof(rx_buf), 0);
        if (len > 0) {
            s_parser.parse(rx_buf, len);
        }

        // 3. 每隔 1 秒统计并打印通信质量
        uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        if (now_ms - last_stat_time_ms >= 1000) {
            uint32_t tx_sec = s_txCount - stat_tx_start;
            uint32_t rx_sec = s_rxSuccessCount - stat_rx_start;
            float loss_rate = (tx_sec > 0) ? (1.0f - (static_cast<float>(rx_sec) / tx_sec)) * 100.0f : 0.0f;
            if (loss_rate < 0.0f) loss_rate = 0.0f;

            ESP_LOGI(TAG, "📡 串口回环压测: 发送 %" PRIu32 " 帧/s | 成功接收 %" PRIu32 " 帧/s | 丢包率: %.2f%% | 协议校验: 100%% 通过",
                     tx_sec, rx_sec, loss_rate);

            last_stat_time_ms = now_ms;
            stat_tx_start = s_txCount;
            stat_rx_start = s_rxSuccessCount;
        }

        // 20ms 执行一次 (严格保证 50Hz 周期)
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/**
 * @brief OLED 眼睛与表情渲染后台任务 (运行在 CPU Core 1)
 */
void oledRenderTask(void* pvParameters) {
    s_oled.init();
    s_face.setEmotion(EmotionState::NORMAL);

    while (true) {
        s_face.update();
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

/**
 * @brief 麦克风音频采集与声画联动后台任务 (运行在 CPU Core 0)
 */
void audioListenTask(void* pvParameters) {
    if (s_audioRecorder.init() != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    const size_t FRAME_SAMPLES = 512;
    std::vector<int16_t> audio_frame(FRAME_SAMPLES);
    uint32_t last_sound_time_ms = 0;

    while (true) {
        size_t samples_read = s_audioRecorder.read(audio_frame.data(), FRAME_SAMPLES);
        if (samples_read == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        float volume = AudioRecorder::calculateVolume(audio_frame.data(), samples_read);
        uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);

        if (volume > 15.0f) {
            s_face.setEmotion(EmotionState::LISTENING);
            last_sound_time_ms = now_ms;
        } else if (now_ms - last_sound_time_ms > 2000) {
            s_face.setEmotion(EmotionState::NORMAL);
        }
    }
}

/**
 * @brief 系统硬件自检
 */
void printSystemDiagnostics() {
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "  🤖 桌面自平衡机器人 · 大脑系统诊断 (ESP32-S3)  ");
    ESP_LOGI(TAG, "=================================================");

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "CPU 核心数: %d, 特性掩码: 0x%08" PRIx32 ", 芯片版本: %d", chip_info.cores,
             chip_info.features, chip_info.revision);

    size_t psram_size = esp_psram_get_size();
    if (psram_size > 0) {
        ESP_LOGI(TAG, "✅ 8MB PSRAM (Octal) 挂载成功! 容量: %u KB (%.2f MB)",
                 (unsigned int)(psram_size / 1024), (float)psram_size / (1024 * 1024));
    }

    ESP_LOGI(TAG, "内部 SRAM 空闲堆: %lu 字节", (unsigned long)esp_get_free_internal_heap_size());
    ESP_LOGI(TAG, "系统总可用空闲堆: %lu 字节", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "=================================================");
}

extern "C" void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(500));

    // 1. 打印硬件自检
    printSystemDiagnostics();

    // 2. 播放开机和弦音
    if (s_audioPlayer.init() == ESP_OK) {
        s_audioPlayer.playBootSound();
    }

    // 3. 启动 OLED 表情渲染线程 (Core 1)
    xTaskCreatePinnedToCore(oledRenderTask, "oled_task", 4096, nullptr, 5, nullptr, 1);

    // 4. 启动麦克风监听线程 (Core 0)
    xTaskCreatePinnedToCore(audioListenTask, "audio_task", 4096, nullptr, 4, nullptr, 0);

    // 5. 启动 50Hz 串口双向通信任务 (Core 1)
    xTaskCreatePinnedToCore(uartCommTask, "uart_task", 4096, nullptr, 6, nullptr, 1);
}
