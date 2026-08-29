#pragma once
#include <cstddef>
#include <cstdint>

#include "driver/i2s_std.h"
#include "esp_err.h"

/**
 * @brief MAX98357A I2S 数字音频播放器驱动类
 */

class AudioPlayer {
public:
    /**
     * @brief 构造函数：指定 I2S 引脚与采样率
     * @param bclk_pin 位时钟引脚 (默认 GPIO 16)
     * @param ws_pin 声道时钟/LRC 引脚 (默认 GPIO 17)
     * @param dout_pin 数据输出 DIN 引脚 (默认 GPIO 15)
     * @param sample_rate 默认采样率 (16000 Hz)
     */
    AudioPlayer(int bclk_pin = 16, int ws_pin = 17, int dout_pin = 15,
                uint32_t sample_rate = 16000);
    ~AudioPlayer();

    /**
     * @brief 初始化 I2S1 控制器与 DMA 通道
     */
    esp_err_t init();

    /**
     * @brief 写入原始 PCM 音频数据进行播放
     * @param data 16位 PCM 音频数据指针
     * @param size_bytes 数据字节大小
     */
    esp_err_t write(const void *data, size_t size_bytes);

    /**
     * @brief 用数学公式合成并播放一段指定频率和时长的蜂鸣/纯音 (正弦波)
     * @param freq_hz 声音频率 (例如 1000 Hz 科技音)
     * @param duration_ms 持续时间 (毫秒)
     * @param volume 振幅音量 (0.0 ~ 1.0)
     */
    void playTone(float freq_hz, uint32_t duration_ms, float volume = 0.5f);

    /**
     * @brief 播放一段双音和弦科技感“哔哩哔哩”开机提示音
     */
    void playBootSound();

private:
    int m_bclkPin;
    int m_wsPin;
    int m_doutPin;
    uint32_t m_sampleRate;
    i2s_chan_handle_t m_txHandle;  // ESP-IDF 新版 I2S 发送通道句柄
};
