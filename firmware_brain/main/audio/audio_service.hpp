/**
 * @file audio_service.hpp
 * @brief 机器人音频与声学子系统业务服务头文件
 *
 * 【架构职责】：
 *  1. 单例外观模式统一纳管扬声器 (MAX98357A) 与全向麦克风 (INMP441)；
 *  2. 托管专属于 Core 0 的音频状态监测与实时环境音量 RMS 计算线程；
 *  3. 提供多线程安全的播报 API：开机和弦音、按键蜂鸣、警报音、自定义正弦波；
 *  4. 预留流式大模型语音输入 (STT) 与语音输出 (TTS) 的原始 PCM 高速缓冲区。
 */
#pragma once

#include <atomic>
#include "audio/mic_driver.hpp"
#include "audio/speaker_driver.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

class AudioService {
public:
    static AudioService &getInstance();

    /**
     * @brief 初始化扬声器功放与麦克风硬件驱动
     */
    esp_err_t init();

    /**
     * @brief 启动 Core 0 上的音频监测后台任务
     */
    esp_err_t start();

    /**
     * @brief 播放赛博朋克开机和弦哨音
     */
    void playBootChime();

    /**
     * @brief 播放按键提示音
     */
    void playBeep();

    /**
     * @brief 播放急促警报音
     */
    void playAlert();

    /**
     * @brief 播放任意单频正弦波音效
     * @param freq_hz 频率 (Hz)
     * @param duration_ms 持续发声时长 (毫秒)
     * @param volume 音量 (0.0f ~ 1.0f, -1 表示使用全局默认音量)
     */
    void playTone(float freq_hz, uint32_t duration_ms, float volume = -1.0f);

    /**
     * @brief 设置喇叭主音量 (0.0f ~ 1.0f)
     */
    void setVolume(float volume);

    /**
     * @brief 获取当前喇叭音量
     */
    float getVolume() const;

    /**
     * @brief 播放单声道 16kHz PCM 音频数据 (直接对接 CosyVoice TTS 流)
     * @param mono_samples 单声道 16 位音频采样
     * @param sample_count 采样点数
     */
    void playStreamPCM(const int16_t *mono_samples, size_t sample_count) {
        speaker_.playMonoPCM(mono_samples, sample_count);
    }

    /**
     * @brief 检查当前功放是否正在发声或处于回声抑制延音期
     */
    bool isPlaying() const {
        return speaker_.isPlaying();
    }

    /**
     * @brief 手动设置扬声器播放活跃状态
     */
    void setPlaybackActive(bool active) {
        speaker_.setPlaybackActive(active);
    }

    /**
     * @brief 设置实时环境音量能量分贝
     */
    void setCurrentEnergy(float energy) {
        current_energy_ = energy;
    }

    /**
     * @brief 获取实时环境音量能量分贝 (归一化 0.0f ~ 1.0f)
     */
    float getMicEnergy() const { return current_energy_; }

    /**
     * @brief 启动音频流消费 (供 ASR 语音识别流式提取)
     * @param preroll_samples 从当前时间点往前提取的历史预缓冲采样点数 (如 12800 点 = 800ms)
     */
    void startAudioStream(size_t preroll_samples = 12800);

    /**
     * @brief 停止音频流消费
     */
    void stopAudioStream();

    /**
     * @brief 从环形音频流中读取 16-Bit 单声道 PCM 数据 (非阻塞/带超时等待)
     * @param dest 目标数据缓冲
     * @param sample_count 期望读取的采样点数
     * @param samples_read 实际读取的采样点数
     * @param timeout_ms 超时毫秒
     * @return esp_err_t ESP_OK 成功读取到数据
     */
    esp_err_t readAudioStream(int16_t *dest, size_t sample_count, size_t *samples_read, uint32_t timeout_ms = 100);

private:
    AudioService();
    ~AudioService();

    AudioService(const AudioService &) = delete;
    AudioService &operator=(const AudioService &) = delete;

    static void audioTask(void *param);

    SpeakerDriver speaker_;  // 扬声器底层硬件驱动
    MicDriver mic_;          // 麦克风底层硬件驱动

    TaskHandle_t task_handle_{nullptr};
    bool is_running_{false};
    float current_energy_{0.0f};  // 当前环境声压能量

    static constexpr size_t RING_BUF_SAMPLES = 32000; // 2秒 16kHz PCM (64KB 环形预缓冲)
    int16_t *ring_buffer_{nullptr};
    size_t ring_write_count_{0};
    size_t ring_read_count_{0};
    std::atomic<bool> is_streaming_{false};
    SemaphoreHandle_t stream_sem_{nullptr};
    portMUX_TYPE ring_mux_ = portMUX_INITIALIZER_UNLOCKED;
};
