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

// 引入 视觉、音频、通信 与 语音 核心子系统
#include "audio_player.hpp"
#include "audio_recorder.hpp"
#include "face_engine.hpp"
#include "oled_driver.hpp"
#include "protocol_parser.hpp"
#include "speech_engine.hpp"
#include "uart_comm.hpp"

extern "C" {
#include "robot_protocol.h"
}

static const char *TAG = "ROBOT_BRAIN";

// 1. OLED 屏幕 (SDA: 8, SCL: 9)
static OledDriver s_oled(8, 9, 0x3C);
// 2. 拟人表情引擎
static FaceEngine s_face(s_oled);
// 3. I2S 扬声器 (BCLK: 16, LRC: 17, DIN: 15)
static AudioPlayer s_audioPlayer(16, 17, 15, 16000);
// 4. I2S 麦克风 (SCK: 6, WS: 5, SD: 4)
static AudioRecorder s_audioRecorder(6, 5, 4, 16000);
// 5. UART2 通信驱动 (TX: GPIO 1, RX: GPIO 2)
static UartComm s_uart(1, 2, 115200, UART_NUM_2);
// 6. 协议状态机解析器
static ProtocolParser s_parser;
// 7. 端侧语音识别引擎
static SpeechEngine s_speechEngine(16000);

// 全局动态控制目标参数
static int16_t s_targetSpeed = 0;  // 目标线速度 (mm/s)
static int16_t s_targetYaw = 0;    // 目标角速度 (mrad/s)
static uint32_t s_stopMotionTimeMs = 0;

// 通信统计计数器
static uint32_t s_txCount = 0;
static uint32_t s_rxSuccessCount = 0;

/**
 * @brief 50Hz 串口双向控制下发任务 (Core 1)
 */
void uartCommTask(void *pvParameters) {
    ESP_LOGI(TAG, "启动 50Hz 串口控制下发任务 (Core 1)...");

    s_uart.init();

    s_parser.setOnCmdPacket([](const RobotCmdPacket_t &cmd) { s_rxSuccessCount++; });

    uint8_t rx_buf[64];
    uint32_t last_stat_time_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    uint32_t stat_tx_start = 0;
    uint32_t stat_rx_start = 0;

    while (true) {
        uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);

        // 运动超时保护：语音触发前进 3 秒后自动减速刹车
        if (s_targetSpeed > 0 && now_ms > s_stopMotionTimeMs) {
            s_targetSpeed = 0;
            s_face.setEmotion(EmotionState::NORMAL);
            ESP_LOGI(TAG, "🛑 语音运动完成，小车平稳减速刹车。");
        }

        // 1. 构建控制包并由 GPIO 1 (TX) 下发
        RobotCmdPacket_t cmd = ProtocolParser::buildCmdPacket(s_targetSpeed, s_targetYaw, 1);
        s_uart.send(&cmd, sizeof(cmd));
        s_txCount++;

        // 2. 非阻塞读取 GPIO 2 (RX) 接收队列
        int len = s_uart.read(rx_buf, sizeof(rx_buf), 0);
        if (len > 0) {
            s_parser.parse(rx_buf, len);
        }

        // 3. 统计输出
        if (now_ms - last_stat_time_ms >= 1000) {
            uint32_t tx_sec = s_txCount - stat_tx_start;
            uint32_t rx_sec = s_rxSuccessCount - stat_rx_start;
            ESP_LOGI(TAG, "📡 串口状态: 发送 %" PRIu32 " 帧/s | 目标速度: %d mm/s", tx_sec,
                     s_targetSpeed);

            last_stat_time_ms = now_ms;
            stat_tx_start = s_txCount;
            stat_rx_start = s_rxSuccessCount;
        }

        vTaskDelay(pdMS_TO_TICKS(20));  // 严格 50Hz 周期
    }
}

/**
 * @brief OLED 眼睛与表情渲染后台任务 (Core 1)
 */
void oledRenderTask(void *pvParameters) {
    s_oled.init();
    s_face.setEmotion(EmotionState::NORMAL);

    while (true) {
        s_face.update();
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

/**
 * @brief 麦克风音频采集与语音指令识别线程 (Core 0)
 */
void audioListenTask(void *pvParameters) {
    ESP_LOGI(TAG, "启动语音监听与识别引擎 (Core 0)...");

    if (s_audioRecorder.init() != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    // 注册语音指令回调事件
    s_speechEngine.setOnCommand([](VoiceCommand cmd) {
        uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);

        switch (cmd) {
            case VoiceCommand::WAKEUP:
                ESP_LOGI(TAG, "🤖 [语音事件] 机器人被唤醒！");
                s_face.setEmotion(EmotionState::LISTENING);  // 水汪汪大眼
                s_audioPlayer.playTone(784.0f, 80, 0.35f);   // G5 轻快提示音
                break;

            case VoiceCommand::COMMAND_HAPPY:
                ESP_LOGI(TAG, "✨ [语音事件] 指令：开心微笑！");
                s_face.setEmotion(EmotionState::HAPPY);      // 月牙微笑眼
                s_audioPlayer.playTone(659.25f, 80, 0.35f);  // 欢快和弦
                s_audioPlayer.playTone(783.99f, 120, 0.40f);
                break;

            case VoiceCommand::COMMAND_FORWARD:
                ESP_LOGI(TAG, "🚀 [语音事件] 指令：向前走！");
                s_targetSpeed = 300;                           // 设定目标速度 300 mm/s
                s_stopMotionTimeMs = now_ms + 3000;            // 持续运动 3 秒
                s_audioPlayer.playTone(1046.50f, 150, 0.40f);  // 高音提示
                break;

            default:
                break;
        }
    });

    const size_t FRAME_SAMPLES = 512;
    std::vector<int16_t> audio_frame(FRAME_SAMPLES);

    while (true) {
        size_t samples_read = s_audioRecorder.read(audio_frame.data(), FRAME_SAMPLES);
        if (samples_read == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 喂入语音识别引擎进行端点与节奏分析
        s_speechEngine.feedAudio(audio_frame.data(), samples_read);
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

    // 1. 打印自检
    printSystemDiagnostics();

    // 2. 播放开机和弦
    if (s_audioPlayer.init() == ESP_OK) {
        s_audioPlayer.playBootSound();
    }

    // 3. 启动 OLED 表情渲染线程 (Core 1)
    xTaskCreatePinnedToCore(oledRenderTask, "oled_task", 4096, nullptr, 5, nullptr, 1);

    // 4. 启动语音识别与声画联动线程 (Core 0)
    xTaskCreatePinnedToCore(audioListenTask, "audio_task", 4096, nullptr, 4, nullptr, 0);

    // 5. 启动 50Hz 串口控制下发线程 (Core 1)
    xTaskCreatePinnedToCore(uartCommTask, "uart_task", 4096, nullptr, 6, nullptr, 1);
}
