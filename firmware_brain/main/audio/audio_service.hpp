/**
 * @file audio_service.hpp
 * @brief 机器人音频全双工管理服务 (统一托管播放与录音)
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include "esp_err.h"
#include "audio_player.hpp"
#include "audio_recorder.hpp"

class AudioService {
public:
    /**
     * @brief 构造函数：指定功放与麦克风引脚
     */
    AudioService(
        int spk_bclk = 16, int spk_lrc = 17, int spk_din = 15,
        int mic_sck = 6,   int mic_ws = 5,   int mic_sd = 4,
        uint32_t sample_rate = 16000
    );
    ~AudioService() = default;

    /**
     * @brief 初始化扬声器与麦克风硬件外设
     */
    esp_err_t init();

    /**
     * @brief 播放开机科技上升和弦音
     */
    void playBootSound();

    /**
     * @brief 合成并播放指定频率的纯音
     */
    void playTone(float freq_hz, uint32_t duration_ms, float volume = 0.4f);

    /**
     * @brief 从麦克风 DMA 读取一帧音频数据
     */
    size_t readRecordFrame(int16_t* out_buffer, size_t samples_to_read);

    /**
     * @brief 计算当前音频帧的音量能量 (0.0 ~ 100.0)
     */
    static float calculateVolume(const int16_t* buffer, size_t samples);

private:
    AudioPlayer   m_player;
    AudioRecorder m_recorder;
};
