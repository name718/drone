/**
 * @file mic_driver.cpp
 * @brief INMP441 I2S 全向数字 MEMS 麦克风底层驱动实现
 */
#include "audio/mic_driver.hpp"

#include <cmath>
#include <algorithm>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "麦克风驱动";

MicDriver::MicDriver() = default;

MicDriver::~MicDriver() {
    if (is_initialized_ && rx_handle_) {
        i2s_channel_disable(rx_handle_);
        i2s_del_channel(rx_handle_);
        rx_handle_ = nullptr;
    }
}

esp_err_t MicDriver::init(int pin_sd, int pin_ws, int pin_sck, uint32_t sample_rate) {
    sample_rate_ = sample_rate;
    ESP_LOGI(TAG, "正在初始化 I2S0 数字麦克风硬件 (SCK=GPIO%d, WS=GPIO%d, SD=GPIO%d, 采样率=%luHz)...",
             pin_sck, pin_ws, pin_sd, (unsigned long)sample_rate_);

    // 1. 创建 I2S0 接收通道 (Master 模式，驱动麦克风时钟)
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t ret = i2s_new_channel(&chan_cfg, nullptr, &rx_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建 I2S0 录音通道失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. 配置标准 Philips I2S 协议时钟与引脚
    // INMP441 输出 24 位精度数据，占用 32-bit 物理槽位，左声道 (L/R 接地)
    i2s_std_config_t std_cfg = {};
    std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_);
    std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO);
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.bclk = static_cast<gpio_num_t>(pin_sck);
    std_cfg.gpio_cfg.ws = static_cast<gpio_num_t>(pin_ws);
    std_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.din = static_cast<gpio_num_t>(pin_sd);
    std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.ws_inv = false;

    // 3. 应用配置到 I2S0 硬件寄存器并开启接收通道
    ret = i2s_channel_init_std_mode(rx_handle_, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化 I2S0 麦克风标准模式失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_enable(rx_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "使能 I2S0 通道失败: %s", esp_err_to_name(ret));
        return ret;
    }

    is_initialized_ = true;
    ESP_LOGI(TAG, "INMP441 拾音麦克风驱动就绪！");
    return ESP_OK;
}

esp_err_t MicDriver::readPCM(int32_t *dest, size_t sample_count, size_t *samples_read, uint32_t timeout_ms) {
    if (!is_initialized_ || !rx_handle_) return ESP_ERR_INVALID_STATE;

    size_t bytes_to_read = sample_count * sizeof(int32_t);
    size_t bytes_read = 0;

    esp_err_t ret = i2s_channel_read(rx_handle_, dest, bytes_to_read, &bytes_read, pdMS_TO_TICKS(timeout_ms));
    if (samples_read) {
        *samples_read = bytes_read / sizeof(int32_t);
    }
    return ret;
}

esp_err_t MicDriver::read16BitPCM(int16_t *dest, size_t sample_count, size_t *samples_read, uint32_t timeout_ms) {
    if (!is_initialized_ || !rx_handle_ || !dest || sample_count == 0) return ESP_ERR_INVALID_STATE;

    constexpr size_t BATCH = 128;
    int32_t raw_buf[BATCH];
    size_t total_read = 0;

    while (total_read < sample_count) {
        size_t to_read = std::min(BATCH, sample_count - total_read);
        size_t actual = 0;
        esp_err_t ret = readPCM(raw_buf, to_read, &actual, timeout_ms);
        if (ret != ESP_OK || actual == 0) {
            break;
        }
        for (size_t i = 0; i < actual; i++) {
            // INMP441 24 位有效数据在高位，右移 14 位获得适当增益的 16 位有符号 PCM
            float in_sample = static_cast<float>(raw_buf[i] >> 14);
            // 一阶 IIR 去直流高通滤波 (截止频率约 20Hz: y[n] = x[n] - x[n-1] + 0.992 * y[n-1])
            float out_sample = in_sample - dc_x_prev_ + 0.992f * dc_y_prev_;
            dc_x_prev_ = in_sample;
            dc_y_prev_ = out_sample;

            // 软拐点饱和限制器，杜绝近距离大声破音削顶，极大提升语音识别声学保真度
            float sample_f = out_sample;
            if (sample_f > 29000.0f) {
                sample_f = 29000.0f + (sample_f - 29000.0f) * 0.25f;
            } else if (sample_f < -29000.0f) {
                sample_f = -29000.0f + (sample_f + 29000.0f) * 0.25f;
            }

            int32_t val = static_cast<int32_t>(sample_f);
            if (val > 32767) val = 32767;
            if (val < -32768) val = -32768;
            dest[total_read + i] = static_cast<int16_t>(val);
        }
        total_read += actual;
    }

    if (samples_read) {
        *samples_read = total_read;
    }
    return (total_read > 0) ? ESP_OK : ESP_FAIL;
}

float MicDriver::readVolumeRMS() {
    if (!is_initialized_) return 0.0f;

    // 一次性采集 128 个采样点 (约 8ms 瞬时声压窗口)
    constexpr size_t SAMPLES = 128;
    int16_t buffer[SAMPLES];
    size_t actual_samples = 0;

    esp_err_t ret = read16BitPCM(buffer, SAMPLES, &actual_samples, 20);
    if (ret != ESP_OK || actual_samples == 0) {
        return 0.0f;
    }

    // 计算均方根 (RMS: Root Mean Square) 声压能量
    double sum_squares = 0.0;
    for (size_t i = 0; i < actual_samples; i++) {
        sum_squares += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
    }

    double mean_sq = sum_squares / static_cast<double>(actual_samples);
    double rms = std::sqrt(mean_sq);

    // 归一化映射至 [0.0, 1.0] (以 6000 为饱和声压基准)
    float normalized = static_cast<float>(rms / 6000.0);
    return std::clamp(normalized, 0.0f, 1.0f);
}
