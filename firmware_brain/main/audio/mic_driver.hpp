/**
 * @file mic_driver.hpp
 * @brief INMP441 I2S 全向数字 MEMS 麦克风底层驱动头文件
 *
 * 【硬件工作原理与技术指标】：
 *  1. INMP441 是一款高信噪比 (61dBA)、低功耗全向数字输出 MEMS 麦克风；
 *  2. 内部集成完整的音头、抗混叠滤波器与高精度 24-bit Σ-Δ ADC；
 *  3. 硬件引脚安全关键：
 *     - VDD 必须接 3.3V（绝对不可接 5V）；
 *     - L/R 引脚接地，芯片在 I2S 左声道时隙输出音频；
 *     - 采用标准 32-Bit 物理槽位传输 24 位有效数据。
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include "driver/i2s_std.h"
#include "esp_err.h"

class MicDriver {
public:
    MicDriver();
    ~MicDriver();

    MicDriver(const MicDriver &) = delete;
    MicDriver &operator=(const MicDriver &) = delete;

    /**
     * @brief 初始化 I2S0 硬件外设与麦克风通信引脚
     * @param pin_sd 麦克风音频串行数据输入引脚 (SD -> GPIO 4)
     * @param pin_ws 麦克风字选择时钟引脚 (WS -> GPIO 5)
     * @param pin_sck 麦克风位时钟引脚 (SCK -> GPIO 6)
     * @param sample_rate 采样率 (默认 16000 Hz)
     * @return esp_err_t ESP_OK 表示硬件初始化成功
     */
    esp_err_t init(int pin_sd, int pin_ws, int pin_sck, uint32_t sample_rate = 16000);

    /**
     * @brief 读取原始 32-bit PCM 采样数据
     * @param dest 接收目标缓冲区 (int32_t 数组)
     * @param sample_count 期望读取的采样点数
     * @param samples_read 实际读取到的采样点数
     * @param timeout_ms 超时毫秒数
     * @return esp_err_t 读取结果
     */
    esp_err_t readPCM(int32_t *dest, size_t sample_count, size_t *samples_read, uint32_t timeout_ms = 50);

    /**
     * @brief 读取转为标准 16-bit PCM 采样数据 (用于语音识别 ASR 推流)
     * @param dest 接收目标缓冲区 (int16_t 数组)
     * @param sample_count 期望读取的采样点数
     * @param samples_read 实际读取到的采样点数
     * @param timeout_ms 超时毫秒数
     * @return esp_err_t 读取结果
     */
    esp_err_t read16BitPCM(int16_t *dest, size_t sample_count, size_t *samples_read, uint32_t timeout_ms = 100);

    /**
     * @brief 实时计算当前声场环境的归一化音量能量 (RMS 均方根算法)
     * @return float 0.0f (绝对安静) ~ 1.0f (爆音/大声说话)
     */
    float readVolumeRMS();

private:
    i2s_chan_handle_t rx_handle_{nullptr};  // I2S0 硬件接收通道句柄
    uint32_t sample_rate_{16000};          // 采样率
    bool is_initialized_{false};
    float dc_x_prev_{0.0f};                 // 去直流高通滤波历史输入
    float dc_y_prev_{0.0f};                 // 去直流高通滤波历史输出
};
