/**
 * @file speech_engine.cpp
 * @brief 机器人端侧语音识别与指令引擎实现
 */

#include "speech_engine.hpp"
#include <cmath>
#include <cstring>
#include "esp_log.h"
#include "esp_timer.h"
#include "../audio/audio_recorder.hpp"

static const char* TAG = "SPEECH_ENGINE";

static inline uint32_t getNowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

SpeechEngine::SpeechEngine(uint32_t sample_rate)
    : m_sampleRate(sample_rate),
      m_isAwake(false),
      m_wakeExpireTimeMs(0),
      m_historyIdx(0) {
    memset(m_energyHistory, 0, sizeof(m_energyHistory));
}

void SpeechEngine::feedAudio(const int16_t* pcm_data, size_t samples) {
    if (!pcm_data || samples == 0) return;

    // 1. 计算当前音频帧的音量能量 (0.0 ~ 100.0)
    float current_volume = AudioRecorder::calculateVolume(pcm_data, samples);

    // 2. 存入滑动历史窗口进行平滑去噪
    m_energyHistory[m_historyIdx] = current_volume;
    m_historyIdx = (m_historyIdx + 1) % 8;

    float smooth_energy = 0.0f;
    for (size_t i = 0; i < 8; i++) {
        smooth_energy += m_energyHistory[i];
    }
    smooth_energy /= 8.0f;

    // 3. 执行 VAD 语音端点与特征分析
    processVAD(smooth_energy);
}

void SpeechEngine::processVAD(float current_volume) {
    static bool is_speaking = false;
    static uint32_t speech_start_time_ms = 0;
    static uint32_t last_speech_end_time_ms = 0;
    static int syllable_count = 0;

    uint32_t now = getNowMs();

    // 检查唤醒超时：若超过 6 秒无人再说话，自动退出唤醒状态
    if (m_isAwake && now > m_wakeExpireTimeMs) {
        m_isAwake = false;
        ESP_LOGI(TAG, "💤 唤醒超时，机器人进入待机休眠状态。");
    }

    const float VAD_ACTIVE_THRESHOLD = 18.0f; // 发声能量门限
    const float VAD_SILENCE_THRESHOLD = 8.0f; // 静音能量门限

    // --- 状态 1: 正在发声检测 ---
    if (current_volume > VAD_ACTIVE_THRESHOLD) {
        if (!is_speaking) {
            is_speaking = true;
            speech_start_time_ms = now;

            // 检查两次发声间隔：如果在 150~450ms 内连续发声，记为双音节
            if (now - last_speech_end_time_ms >= 150 && now - last_speech_end_time_ms <= 450) {
                syllable_count++;
            } else {
                syllable_count = 1;
            }
        }
    }
    // --- 状态 2: 发声结束检测 (连续低于静音门限) ---
    else if (is_speaking && current_volume < VAD_SILENCE_THRESHOLD) {
        uint32_t duration_ms = now - speech_start_time_ms;
        is_speaking = false;
        last_speech_end_time_ms = now;

        // 过滤小于 60ms 的瞬态杂音撞击
        if (duration_ms < 60) return;

        ESP_LOGI(TAG, "🎙️ 检测到有效人声段: 时长=%" PRIu32 "ms, 音节计数=%d", duration_ms, syllable_count);

        // --- 指令模式匹配逻辑 ---
        if (!m_isAwake) {
            // 未唤醒状态下：任何有效发声均触发【唤醒】
            m_isAwake = true;
            m_wakeExpireTimeMs = now + 6000; // 激活 6 秒
            if (m_cmdCb) {
                m_cmdCb(VoiceCommand::WAKEUP);
            }
        } else {
            // 已处于唤醒状态下：刷新超时时间并匹配指令
            m_wakeExpireTimeMs = now + 6000;

            if (syllable_count >= 2) {
                // 双音节指令 -> 开心笑一个！
                ESP_LOGI(TAG, "✨ 匹配到语音指令: [开心/笑一个]！");
                if (m_cmdCb) m_cmdCb(VoiceCommand::COMMAND_HAPPY);
                syllable_count = 0;
            } else if (duration_ms >= 500) {
                // 长音指令 -> 向前走！
                ESP_LOGI(TAG, "🚀 匹配到语音指令: [向前走]！");
                if (m_cmdCb) m_cmdCb(VoiceCommand::COMMAND_FORWARD);
            }
        }
    }
}
