/**
 * @file speaker_driver.cpp
 * @brief MAX98357A I2S 数字音频功放底层驱动实现
 */
#include "audio/speaker_driver.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "config/board_config.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "功放驱动";

SpeakerDriver::SpeakerDriver() = default;

SpeakerDriver::~SpeakerDriver() {
    if (is_initialized_ && tx_handle_) {
        i2s_channel_disable(tx_handle_);
        i2s_del_channel(tx_handle_);
        tx_handle_ = nullptr;
    }
}

esp_err_t SpeakerDriver::init(int pin_din, int pin_bclk, int pin_lrc, uint32_t sample_rate) {
    sample_rate_ = sample_rate;
    ESP_LOGI(TAG, "正在初始化 I2S1 数字功放硬件 (BCLK=GPIO%d, LRC=GPIO%d, DIN=GPIO%d, 采样率=%luHz)...",
             pin_bclk, pin_lrc, pin_din, (unsigned long)sample_rate_);

    // 1. 配置 I2S 通道基础属性 (作为主机 Master 驱动外部功放从机 Slave)
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.auto_clear_after_cb = true;  // 空闲时自动刷静音零电平，杜绝底噪杂音
    chan_cfg.auto_clear_before_cb = true;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &tx_handle_, nullptr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建 I2S1 发送通道失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. 配置标准 Philips I2S 协议时钟与引脚结构体
    // 【语法细节】：先以 {} 零初始化避免 -Werror 编译告警
    i2s_std_config_t std_cfg = {};
    std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_);
    std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);

    std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.bclk = static_cast<gpio_num_t>(pin_bclk);
    std_cfg.gpio_cfg.ws = static_cast<gpio_num_t>(pin_lrc);
    std_cfg.gpio_cfg.dout = static_cast<gpio_num_t>(pin_din);
    std_cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.ws_inv = false;

    // 3. 应用配置到 I2S 硬件寄存器并使能通道
    ret = i2s_channel_init_std_mode(tx_handle_, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化 I2S 标准模式失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_enable(tx_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "使能 I2S 通道失败: %s", esp_err_to_name(ret));
        return ret;
    }

    is_initialized_ = true;
    ESP_LOGI(TAG, "MAX98357A 音频功放驱动就绪！");
    return ESP_OK;
}

void SpeakerDriver::setVolume(float volume) {
    volume_ = std::clamp(volume, 0.0f, 1.0f);
}

esp_err_t SpeakerDriver::writePCM(const int16_t *samples, size_t sample_count) {
    if (!is_initialized_ || !tx_handle_) return ESP_ERR_INVALID_STATE;

    // 双声道交织格式 (L, R, L, R...)：每个采样对占 2 个 int16_t (4 字节)
    size_t bytes_to_write = sample_count * 2 * sizeof(int16_t);
    size_t bytes_written = 0;

    return i2s_channel_write(tx_handle_, samples, bytes_to_write, &bytes_written, portMAX_DELAY);
}

void SpeakerDriver::playTone(float freq_hz, uint32_t duration_ms, float volume) {
    if (!is_initialized_ || freq_hz <= 0.0f || duration_ms == 0) return;

    float vol = (volume >= 0.0f) ? std::clamp(volume, 0.0f, 1.0f) : volume_;
    size_t total_samples = (sample_rate_ * duration_ms) / 1000;

    // 5ms 平滑淡入淡出（防破音核心算法）
    size_t fade_samples = (sample_rate_ * 5) / 1000;
    if (fade_samples * 2 > total_samples) {
        fade_samples = total_samples / 2;
    }

    // 采用小缓冲区分片推流 (每次 256 采样点对 = 1024 字节)，内存消耗极低
    constexpr size_t CHUNK_SAMPLES = 256;
    int16_t chunk_buffer[CHUNK_SAMPLES * 2];  // 左右双声道缓冲

    float phase = 0.0f;
    float phase_step = (2.0f * M_PI * freq_hz) / static_cast<float>(sample_rate_);
    float max_amplitude = 32767.0f * vol;

    size_t samples_generated = 0;
    while (samples_generated < total_samples) {
        size_t current_chunk = std::min(CHUNK_SAMPLES, total_samples - samples_generated);

        for (size_t i = 0; i < current_chunk; i++) {
            size_t global_idx = samples_generated + i;

            // 计算平滑包络因子 (Envelope: 0.0 ~ 1.0)
            float envelope = 1.0f;
            if (global_idx < fade_samples) {
                envelope = static_cast<float>(global_idx) / static_cast<float>(fade_samples);
            } else if (global_idx > total_samples - fade_samples) {
                envelope = static_cast<float>(total_samples - global_idx) / static_cast<float>(fade_samples);
            }

            // 正弦波生成
            int16_t val = static_cast<int16_t>(std::sin(phase) * max_amplitude * envelope);
            phase += phase_step;
            if (phase >= 2.0f * M_PI) {
                phase -= 2.0f * M_PI;
            }

            // 写入立体声左右两个通道 (MAX98357A 即可输出饱满单声道混音)
            chunk_buffer[i * 2] = val;      // 左声道
            chunk_buffer[i * 2 + 1] = val;  // 右声道
        }

        writePCM(chunk_buffer, current_chunk);
        samples_generated += current_chunk;
    }
}

void SpeakerDriver::playBootSound() {
    // 赛博朋克大调上升三和弦开机音效 (C5 -> E5 -> G5 -> C6)
    playTone(523.25f, 70);   // C5 (Do)
    playTone(659.25f, 70);   // E5 (Mi)
    playTone(783.99f, 80);   // G5 (Sol)
    playTone(1046.50f, 160); // C6 (高音 Do，带回味延音)
}

void SpeakerDriver::playBeep() {
    // 清脆的按键交互音 (1000Hz, 80ms)
    playTone(1000.0f, 80);
}

void SpeakerDriver::playAlert() {
    // 双频交替急促警报音 (880Hz / 440Hz)
    playTone(880.0f, 100);
    vTaskDelay(pdMS_TO_TICKS(30));
    playTone(440.0f, 150);
}
