/**
 * @file speaker_driver.hpp
 * @brief MAX98357A I2S 数字音频功放底层驱动头文件
 *
 * 【硬件工作原理与技术指标】：
 *  1. MAX98357A 是一款高性能单声道 D 类音频功放，内置 I2S 硬件 DAC；
 *  2. 采用标准 Philips I2S 协议通信：
 *     - BCLK (位时钟): 每传输 1 位数据产生一个脉冲时钟；
 *     - LRC / WS (声道时钟): 低电平代表左声道，高电平代表右声道；
 *     - DIN (串行数据): 逐位传输 16-bit 有符号 PCM 线性脉冲编码数据；
 *  3. 本驱动内置纯数学波形合成算法（Sine Wave Synthesizer）：
 *     - 零外部音频文件与 SPIFFS 依赖，节省 Flash 存储；
 *     - 具备 5ms 平滑淡入淡出（Envelope Shaping）机制，彻底消除音频启停瞬间的“爆音/破音（Pop Noise）”。
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include "driver/i2s_std.h"
#include "esp_err.h"

class SpeakerDriver {
public:
    SpeakerDriver();
    ~SpeakerDriver();

    SpeakerDriver(const SpeakerDriver &) = delete;
    SpeakerDriver &operator=(const SpeakerDriver &) = delete;

    /**
     * @brief 初始化 I2S1 硬件外设与功放通信引脚
     * @param pin_din 串行数据输出引脚 (DIN -> GPIO 15)
     * @param pin_bclk 连续位时钟引脚 (BCLK -> GPIO 16)
     * @param pin_lrc 声道选择帧时钟引脚 (LRC -> GPIO 17)
     * @param sample_rate 采样率 (默认 16000 Hz)
     * @return esp_err_t ESP_OK 表示硬件初始化成功
     */
    esp_err_t init(int pin_din, int pin_bclk, int pin_lrc, uint32_t sample_rate = 16000);

    /**
     * @brief 设置全局软件音量增益
     * @param volume 范围 0.0f (静音) ~ 1.0f (最大音量)
     */
    void setVolume(float volume);

    /**
     * @brief 获取当前音量
     */
    float getVolume() const { return volume_; }

    /**
     * @brief 播放指定频率与时长的纯音（内置软包络防破音算法）
     * @param freq_hz 频率 (Hz)，例如 440Hz(标准A音), 1000Hz(提示音)
     * @param duration_ms 持续发声时长 (毫秒)
     * @param volume 独立单音音量 (0.0f ~ 1.0f)
     */
    void playTone(float freq_hz, uint32_t duration_ms, float volume = -1.0f);

    /**
     * @brief 播放赛博朋克开机和弦哨音 (类似 R2-D2 灵动多段琶音)
     */
    void playBootSound();

    /**
     * @brief 播放清脆的交互确认提示音 (1000Hz, 80ms)
     */
    void playBeep();

    /**
     * @brief 播放双音交替报警音 (880Hz / 440Hz)
     */
    void playAlert();

    /**
     * @brief 直接向 I2S DMA 写入原始 16-bit 双声道 PCM 音频流
     * @param samples 双声道交叉交织采样数据 (L, R, L, R...)
     * @param sample_count 采样对总数
     * @return esp_err_t 写入结果
     */
    esp_err_t writePCM(const int16_t *samples, size_t sample_count);

    /**
     * @brief 播放单声道 16-bit 线性 PCM 音频数据 (直接对接 CosyVoice TTS 流)
     * @param mono_samples 单声道 16-bit 采样数组指针
     * @param sample_count 单声道采样点总数
     */
    void playMonoPCM(const int16_t *mono_samples, size_t sample_count);

    /**
     * @brief 检查当前功放是否正在发声或处于回声抑制延音期
     *
     * 【声学回声抑制 (Software AEC) 核心原理】：
     *  机器人麦克风与扬声器安装在同一个机身内（物理距离近），喇叭发声时机身振动与空间直达声会
     *  以极大声压进入全向麦克风。若不加抑制，机器人会将自己说的话误当做主人的呼唤，导致死循环自激。
     *  此处除了判断当前是否处于 I2S DMA 写入阶段外，还额外维护了 350ms 的混响衰减窗口：
     *  1. MAX98357A 硬件 DMA FIFO 存在数十毫秒的物理回放延迟；
     *  2. 桌面及室内墙面存在声音多次反射产生的空间混响（Room Reverberation）；
     *  3. 发声结束 350ms 内保持 isPlaying=true，确保系统完全滤除自激回声。
     * @return true 表示扬声器正在发声或处于回声保护期，麦克风应抑制声压上报
     */
    bool isPlaying() const;

    /**
     * @brief 手动设置扬声器播放活跃状态 (供 WebSocket TTS 流式合成会话包裹使用)
     * @param active true 表示开始向功放推流，false 表示推流结束
     */
    void setPlaybackActive(bool active);

private:
    i2s_chan_handle_t tx_handle_{nullptr};  // I2S 硬件输出通道句柄 (用于向 DMA 发送数据)
    uint32_t sample_rate_{16000};          // 当前采样率 (16000Hz 对应行业标准语音交互带宽)
    float volume_{0.4f};                   // 默认软件增益比例 (0.0f ~ 1.0f)
    bool is_initialized_{false};           // 硬件外设初始化标志

    // 线程安全的原子播放状态与时间戳标记 (多核无锁读取)
    std::atomic<bool> is_playing_{false};      // 当前是否正在执行 I2S 传输
    std::atomic<int64_t> last_play_end_ms_{0}; // 上次音频写入结束时的系统单调毫秒时间戳
};
