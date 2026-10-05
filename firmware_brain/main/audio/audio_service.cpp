/**
 * @file audio_service.cpp
 * @brief 机器人音频与声学子系统业务服务实现
 */
#include "audio/audio_service.hpp"

#include "config/board_config.hpp"
#include "esp_log.h"

static const char *TAG = "音频服务";

AudioService &AudioService::getInstance() {
    static AudioService instance;
    return instance;
}

AudioService::AudioService() = default;

esp_err_t AudioService::init() {
    ESP_LOGI(TAG, "正在初始化音频硬件子系统...");

    // 1. 初始化 MAX98357A 扬声器 (I2S1 硬件通道)
    esp_err_t err = speaker_.init(Config::Audio::PIN_SPK_DIN,
                                  Config::Audio::PIN_SPK_BCLK,
                                  Config::Audio::PIN_SPK_LRC,
                                  Config::Audio::SPK_SAMPLE_RATE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "扬声器驱动初始化失败: %s", esp_err_to_name(err));
        return err;
    }

    // 2. 初始化 INMP441 全向麦克风 (I2S0 硬件通道)
    err = mic_.init(Config::Audio::PIN_MIC_SD,
                    Config::Audio::PIN_MIC_WS,
                    Config::Audio::PIN_MIC_SCK,
                    Config::Audio::MIC_SAMPLE_RATE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "麦克风驱动初始化未就绪 (如果未接麦克风可忽略): %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "✅ 音频子系统硬件初始化完成！");
    return ESP_OK;
}

esp_err_t AudioService::start() {
    if (is_running_) return ESP_OK;

    is_running_ = true;

    // 创建后台声波监测任务，绑定在 Core 0 (避免影响 Core 1 的 33FPS 视觉与实时运动解算)
    BaseType_t ret = xTaskCreatePinnedToCore(
        audioTask,
        Config::Tasks::AUDIO_TASK_NAME,
        Config::Tasks::AUDIO_STACK_SIZE,
        this,
        Config::Tasks::AUDIO_PRIORITY,
        &task_handle_,
        Config::Tasks::AUDIO_CORE_ID  // 绑定在 Core 0
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建音频后台任务失败！");
        is_running_ = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "🎉 音频业务服务已在 Core 0 启动就绪！");
    return ESP_OK;
}

void AudioService::audioTask(void *param) {
    auto *self = static_cast<AudioService *>(param);
    ESP_LOGI(TAG, "🎙️ 声学能量实时监测任务已在 Core 0 启动 (10Hz)");

    while (self->is_running_) {
        // 采集并更新环境声能 (用于网页端 VU 跳动表)
        self->current_energy_ = self->mic_.readVolumeRMS();

        // 100ms (10Hz) 采样节拍
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    vTaskDelete(nullptr);
}

void AudioService::playBootChime() {
    ESP_LOGI(TAG, "🎵 播放开机赛博和弦音效...");
    speaker_.playBootSound();
}

void AudioService::playBeep() {
    speaker_.playBeep();
}

void AudioService::playAlert() {
    ESP_LOGI(TAG, "⚠️ 播放警报音效...");
    speaker_.playAlert();
}

void AudioService::playTone(float freq_hz, uint32_t duration_ms, float volume) {
    speaker_.playTone(freq_hz, duration_ms, volume);
}

void AudioService::setVolume(float volume) {
    speaker_.setVolume(volume);
}

float AudioService::getVolume() const {
    return speaker_.getVolume();
}
