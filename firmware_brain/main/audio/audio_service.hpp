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

#include "audio/mic_driver.hpp"
#include "audio/speaker_driver.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
     * @brief 获取实时环境音量能量分贝 (归一化 0.0f ~ 1.0f)
     * 可供 Web 控制台实时跳动 VU 电平表或给眼睛做声波律动交互
     */
    float getMicEnergy() const { return current_energy_; }

private:
    AudioService();
    ~AudioService() = default;

    AudioService(const AudioService &) = delete;
    AudioService &operator=(const AudioService &) = delete;

    static void audioTask(void *param);

    SpeakerDriver speaker_;  // 扬声器底层硬件驱动
    MicDriver mic_;          // 麦克风底层硬件驱动

    TaskHandle_t task_handle_{nullptr};
    bool is_running_{false};
    float current_energy_{0.0f};  // 当前环境声压能量
};
