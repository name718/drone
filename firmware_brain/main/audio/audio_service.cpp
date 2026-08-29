/**
 * @file audio_service.cpp
 * @brief 机器人音频全双工管理服务实现
 */

#include "audio_service.hpp"
#include "esp_log.h"

static const char* TAG = "AUDIO_SERVICE";

AudioService::AudioService(
    int spk_bclk, int spk_lrc, int spk_din,
    int mic_sck,  int mic_ws,  int mic_sd,
    uint32_t sample_rate
)
    : m_player(spk_bclk, spk_lrc, spk_din, sample_rate),
      m_recorder(mic_sck, mic_ws, mic_sd, sample_rate) {
}

esp_err_t AudioService::init() {
    ESP_LOGI(TAG, "初始化音频子系统 (MAX98357A 扬声器 + INMP441 麦克风)...");

    // 1. 初始化扬声器 (I2S1 TX)
    esp_err_t ret = m_player.init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ 扬声器初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. 初始化麦克风 (I2S0 RX)
    ret = m_recorder.init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ 麦克风初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "✅ 音频全双工服务初始化成功！");
    return ESP_OK;
}

void AudioService::playBootSound() {
    m_player.playBootSound();
}

void AudioService::playTone(float freq_hz, uint32_t duration_ms, float volume) {
    m_player.playTone(freq_hz, duration_ms, volume);
}

size_t AudioService::readRecordFrame(int16_t* out_buffer, size_t samples_to_read) {
    return m_recorder.read(out_buffer, samples_to_read);
}

float AudioService::calculateVolume(const int16_t* buffer, size_t samples) {
    return AudioRecorder::calculateVolume(buffer, samples);
}
