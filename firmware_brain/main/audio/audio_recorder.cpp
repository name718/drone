/**
 * @file audio_recorder.cpp
 * @brief INMP441 I2S 全向数字麦克风录音驱动实现
 */

#include "audio_recorder.hpp"

#include <cmath>
#include <cstdlib>

#include "esp_log.h"

static const char *TAG = "AUDIO_RECORDER";

AudioRecorder::AudioRecorder(int sck_pin, int ws_pin, int sd_pin, uint32_t sample_rate)
    : m_sckPin(sck_pin),
      m_wsPin(ws_pin),
      m_sdPin(sd_pin),
      m_sampleRate(sample_rate),
      m_rxHandle(nullptr) {}

AudioRecorder::~AudioRecorder() {
    if (m_rxHandle) {
        i2s_channel_disable(m_rxHandle);
        i2s_del_channel(m_rxHandle);
    }
}

esp_err_t AudioRecorder::init() {
    ESP_LOGI(TAG, "初始化 I2S0 麦克风录音通道 (SCK: %d, WS: %d, SD: %d, 采样率: %" PRIu32 "Hz)...",
             m_sckPin, m_wsPin, m_sdPin, m_sampleRate);

    // 1. 配置 I2S 控制器为接收角色 (I2S_NUM_0, Master 模式)
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t ret = i2s_new_channel(&chan_cfg, nullptr, &m_rxHandle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S0 接收通道创建失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. 配置标准 Philips I2S 录音槽位 (16位数据位宽, 32-bit 物理槽位,仅接收左声道)
    i2s_std_slot_config_t slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;              // INMP441 L/R 接地，数据位于左声道
    slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;  // INMP441 硬件要求 32-bit槽位

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(m_sampleRate),
        .slot_cfg = slot_cfg,
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = static_cast<gpio_num_t>(m_sckPin),
                .ws = static_cast<gpio_num_t>(m_wsPin),
                .dout = I2S_GPIO_UNUSED,
                .din = static_cast<gpio_num_t>(m_sdPin),
                .invert_flags =
                    {
                        .mclk_inv = false,
                        .bclk_inv = false,
                        .ws_inv = false,
                    },
            },
    };

    ret = i2s_channel_init_std_mode(m_rxHandle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S0 录音模式初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 3. 启用 I2S DMA 接收通道
    ret = i2s_channel_enable(m_rxHandle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S0 接收通道启用失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "✅ I2S 麦克风 (INMP441) 初始化成功！");
    return ESP_OK;
}

size_t AudioRecorder::read(int16_t *out_buffer, size_t samples_to_read) {
    if (!m_rxHandle || !out_buffer || samples_to_read == 0) {
        return 0;
    }

    size_t bytes_to_read = samples_to_read * sizeof(int16_t);
    size_t bytes_read = 0;

    // 从 I2S DMA 硬件队列中同步读取 PCM 数据
    esp_err_t ret =
        i2s_channel_read(m_rxHandle, out_buffer, bytes_to_read, &bytes_read, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        return 0;
    }

    return bytes_read / sizeof(int16_t);
}

float AudioRecorder::calculateVolume(const int16_t *buffer, size_t samples) {
    if (!buffer || samples == 0)
        return 0.0f;

    // 计算音频信号的平均绝对幅值 (Mean Absolute Amplitude)
    int64_t sum = 0;
    for (size_t i = 0; i < samples; i++) {
        sum += std::abs(static_cast<int>(buffer[i]));
    }

    float avg_amplitude = static_cast<float>(sum) / samples;

    // 归一化到 0.0 ~ 100.0 (以 10000 幅度为满量程)
    float volume = (avg_amplitude / 10000.0f) * 100.0f;
    if (volume > 100.0f)
        volume = 100.0f;
    return volume;
}
