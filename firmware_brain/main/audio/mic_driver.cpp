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
            // 【数字音频增益与位深映射】：
            // INMP441 硬件输出 24-bit 有符号补码数据，在 32-bit I2S 物理槽位中按高位对齐 (MSB Aligned)。
            // 理论降至 16-bit 需右移 16 位 (32 - 16 = 16)，但此处采用右移 14 位：
            // 相当于预先注入了 +12dB (2^2 = 4倍) 的洁净数字增益，补偿微型 MEMS 振膜拾音灵敏度，
            // 让人在 1~3 米正常距离说话时能饱满填满 16 位动态范围，显著提升远场识别率。
            float in_sample = static_cast<float>(raw_buf[i] >> 14);

            // 【一阶 IIR 去直流高通滤波器 (DC-Removal High-Pass Filter)】：
            // 传递函数：H(z) = (1 - z^-1) / (1 - 0.992 * z^-1)
            // 差分方程：y[n] = x[n] - x[n-1] + 0.992 * y[n-1]
            // MEMS 麦克风由硅微机械结构与电荷泵供电，硬件底层存在固有直流零漂 (DC Offset)。
            // 若不消除零漂，会导致声压能量计算基线抬高、甚至在后续大音量时单侧削顶。
            // 本滤波器在 DC (0Hz) 处设置传输零点，截止频率约为 20Hz (人耳听觉下限)，彻底滤除直流漂移与低频抖晃。
            float out_sample = in_sample - dc_x_prev_ + 0.992f * dc_y_prev_;
            dc_x_prev_ = in_sample;
            dc_y_prev_ = out_sample;

            // 【软拐点动态压缩限制器 (Soft-Knee Dynamic Peak Limiter)】：
            // 针对近距离大声吼叫（如贴近麦克风喊话）设计。硬削顶（Hard Clipping）会瞬间产生
            // 尖锐的奇次谐波方波，严重破坏 Paraformer 声学模型特征提取（MFCC/Fbank 频谱严重畸变）。
            // 当振幅超过 29000 时，超出部分按 4:1 (斜率 0.25) 进行平滑对数型软压缩，保全波形包络完整性。
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
