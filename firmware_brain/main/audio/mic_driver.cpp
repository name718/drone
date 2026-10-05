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

float MicDriver::readVolumeRMS() {
    if (!is_initialized_) return 0.0f;

    // 一次性采集 128 个采样点 (约 8ms 瞬时声压窗口)
    constexpr size_t SAMPLES = 128;
    int32_t buffer[SAMPLES];
    size_t actual_samples = 0;

    esp_err_t ret = readPCM(buffer, SAMPLES, &actual_samples, 20);
    if (ret != ESP_OK || actual_samples == 0) {
        return 0.0f;
    }

    // 计算均方根 (RMS: Root Mean Square) 声压能量
    double sum_squares = 0.0;
    for (size_t i = 0; i < actual_samples; i++) {
        // INMP441 数据位于高 24 位，右移 14 位截取有感幅度
        int32_t sample = buffer[i] >> 14;
        sum_squares += static_cast<double>(sample) * static_cast<double>(sample);
    }

    double mean_sq = sum_squares / static_cast<double>(actual_samples);
    double rms = std::sqrt(mean_sq);

    // 归一化映射至 [0.0, 1.0] (以 8000 为饱和声压基准)
    float normalized = static_cast<float>(rms / 8000.0);
    return std::clamp(normalized, 0.0f, 1.0f);
}
