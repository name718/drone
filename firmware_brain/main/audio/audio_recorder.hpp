#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "driver/i2s_std.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief INMP441 I2S 全向数字麦克风录音驱动类
 */
class AudioRecorder {
public:
    /**
     * @brief 构造函数：指定 I2S 录音引脚与采样率
     * @param sck_pin 位时钟 SCK 引脚 (默认 GPIO 6)
     * @param ws_pin 帧同步 WS 引脚 (默认 GPIO 5)
     * @param sd_pin 串行数据 SD 引脚 (默认 GPIO 4)
     * @param sample_rate 语音识别标准采样率 (16000 Hz)
     */
    AudioRecorder(int sck_pin = 6, int ws_pin = 5, int sd_pin = 4, uint32_t sample_rate = 16000);
    ~AudioRecorder();

    /**
     * @brief 初始化 I2S0 RX 接收通道与 DMA
     */
    esp_err_t init();

    /**
     * @brief 从麦克风 DMA 读取一帧 16-bit PCM 音频采样数据
     * @param out_buffer 输出音频数组
     * @param samples_to_read 期望读取的采样点数 (例如 512 点)
     * @return 实际成功读取的采样点数
     */
    size_t read(int16_t *out_buffer, size_t samples_to_read);

    /**
     * @brief 简易声音能量计算 (计算当前一帧音频的音量分贝/能量值，范围 0 ~ 100)
     */
    static float calculateVolume(const int16_t *buffer, size_t samples);

private:
    int m_sckPin;
    int m_wsPin;
    int m_sdPin;
    uint32_t m_sampleRate;
    i2s_chan_handle_t m_rxHandle;  // ESP-IDF I2S 录音接收通道句柄
};
