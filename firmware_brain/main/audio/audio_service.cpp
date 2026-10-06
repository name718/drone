/**
 * @file audio_service.cpp
 * @brief 机器人音频与声学子系统业务服务实现
 */
#include "audio/audio_service.hpp"

#include <cmath>
#include <cstring>
#include <algorithm>
#include "esp_heap_caps.h"
#include "config/board_config.hpp"
#include "esp_log.h"

static const char *TAG = "音频服务";

AudioService &AudioService::getInstance() {
    static AudioService instance;
    return instance;
}

AudioService::AudioService() = default;

AudioService::~AudioService() {
    is_running_ = false;
    is_streaming_ = false;
    if (stream_sem_) {
        vSemaphoreDelete(stream_sem_);
        stream_sem_ = nullptr;
    }
    if (ring_buffer_) {
        free(ring_buffer_);
        ring_buffer_ = nullptr;
    }
}

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

    // 3. 创建音频流同步信号量
    stream_sem_ = xSemaphoreCreateBinary();

    // 4. 分配 64KB 全时段音频环形预缓冲区 (2 秒 16kHz 16-Bit Mono PCM)
    ring_buffer_ = (int16_t *)heap_caps_malloc(RING_BUF_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ring_buffer_) {
        ring_buffer_ = (int16_t *)malloc(RING_BUF_SAMPLES * sizeof(int16_t));
    }
    if (ring_buffer_) {
        memset(ring_buffer_, 0, RING_BUF_SAMPLES * sizeof(int16_t));
        ring_write_count_ = 0;
        ring_read_count_ = 0;
        ESP_LOGI(TAG, "已成功创建 64KB 全时态音频环形预缓冲区 (保全说话首字与唤醒词)！");
    } else {
        ESP_LOGE(TAG, "分配音频环形缓冲区失败，内存不足！");
    }

    ESP_LOGI(TAG, "音频子系统硬件初始化完成！");
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

    ESP_LOGI(TAG, "音频业务服务已在 Core 0 启动就绪！");
    return ESP_OK;
}

void AudioService::audioTask(void *param) {
    auto *self = static_cast<AudioService *>(param);
    ESP_LOGI(TAG, "声学采集与全时段环形预缓冲任务已在 Core 0 启动 (100%% 占空比全时监听)");

    constexpr size_t CHUNK = 320; // 20ms 分片 (320 samples @ 16kHz)
    int16_t chunk_buf[CHUNK];

    while (self->is_running_) {
        // 1. 软件回声抑制 (AEC)：若扬声器当前正在发声，抑制拾音能量，清零声压
        if (self->speaker_.isPlaying()) {
            self->current_energy_ = 0.0f;
            // 播放期间若未处于 ASR 录音会话中，持续更新读指针，避免录入机器人自发声音
            if (!self->is_streaming_.load()) {
                portENTER_CRITICAL(&self->ring_mux_);
                self->ring_read_count_ = self->ring_write_count_;
                portEXIT_CRITICAL(&self->ring_mux_);
            }
            vTaskDelay(pdMS_TO_TICKS(15));
            continue;
        }

        // 2. 从硬件 I2S0 麦克风独占读取 20ms PCM 帧
        size_t samples_read = 0;
        esp_err_t ret = self->mic_.read16BitPCM(chunk_buf, CHUNK, &samples_read, 40);
        if (ret == ESP_OK && samples_read > 0) {
            // 计算实时连续 RMS 能量
            double sum_squares = 0.0;
            for (size_t i = 0; i < samples_read; i++) {
                sum_squares += static_cast<double>(chunk_buf[i]) * static_cast<double>(chunk_buf[i]);
            }
            double rms = std::sqrt(sum_squares / static_cast<double>(samples_read));
            float energy = std::clamp(static_cast<float>(rms / 6000.0), 0.0f, 1.0f);
            self->current_energy_ = energy;

            // 写入环形缓冲区 (全时段滚动存储最近 2 秒音频)
            if (self->ring_buffer_) {
                portENTER_CRITICAL(&self->ring_mux_);
                for (size_t i = 0; i < samples_read; i++) {
                    self->ring_buffer_[(self->ring_write_count_ + i) % RING_BUF_SAMPLES] = chunk_buf[i];
                }
                self->ring_write_count_ += samples_read;
                // 如果消费者落后超过整个缓冲区大小，强制推进读指针防止回绕
                if (self->is_streaming_.load()) {
                    if ((self->ring_write_count_ - self->ring_read_count_) > RING_BUF_SAMPLES) {
                        self->ring_read_count_ = self->ring_write_count_ - RING_BUF_SAMPLES;
                    }
                }
                portEXIT_CRITICAL(&self->ring_mux_);

                if (self->is_streaming_.load() && self->stream_sem_) {
                    xSemaphoreGive(self->stream_sem_);
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    vTaskDelete(nullptr);
}

void AudioService::startAudioStream(size_t preroll_samples) {
    portENTER_CRITICAL(&ring_mux_);
    is_streaming_.store(true);
    size_t safe_preroll = std::min(preroll_samples, RING_BUF_SAMPLES);
    safe_preroll = std::min(safe_preroll, ring_write_count_);
    ring_read_count_ = ring_write_count_ - safe_preroll;
    portEXIT_CRITICAL(&ring_mux_);
    if (stream_sem_) {
        xSemaphoreTake(stream_sem_, 0); // 清空历史信号
    }
    ESP_LOGI(TAG, "启动语音流消费，提取预缓冲采样点: %u (%.2f 秒)",
             (unsigned)safe_preroll, static_cast<float>(safe_preroll) / 16000.0f);
}

void AudioService::stopAudioStream() {
    is_streaming_.store(false);
    if (stream_sem_) {
        xSemaphoreGive(stream_sem_);
    }
    ESP_LOGI(TAG, "停止语音流消费");
}

esp_err_t AudioService::readAudioStream(int16_t *dest, size_t sample_count, size_t *samples_read, uint32_t timeout_ms) {
    if (!ring_buffer_ || !dest || sample_count == 0) return ESP_ERR_INVALID_ARG;
    if (!is_streaming_.load()) return ESP_ERR_INVALID_STATE;

    size_t total_copied = 0;
    TickType_t start_tick = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while (total_copied < sample_count && is_streaming_.load()) {
        size_t available = 0;
        portENTER_CRITICAL(&ring_mux_);
        if (ring_write_count_ > ring_read_count_) {
            available = ring_write_count_ - ring_read_count_;
        }
        size_t to_copy = std::min(available, sample_count - total_copied);
        for (size_t i = 0; i < to_copy; i++) {
            dest[total_copied + i] = ring_buffer_[(ring_read_count_ + i) % RING_BUF_SAMPLES];
        }
        ring_read_count_ += to_copy;
        total_copied += to_copy;
        portEXIT_CRITICAL(&ring_mux_);

        if (total_copied >= sample_count) {
            break;
        }

        // 缓冲区暂时无足够数据，等待后台 audioTask 生产新数据
        TickType_t elapsed = xTaskGetTickCount() - start_tick;
        if (elapsed >= timeout_ticks) {
            break;
        }
        TickType_t wait_ticks = timeout_ticks - elapsed;
        if (stream_sem_) {
            xSemaphoreTake(stream_sem_, std::min(wait_ticks, pdMS_TO_TICKS(20)));
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    if (samples_read) {
        *samples_read = total_copied;
    }
    return (total_copied > 0) ? ESP_OK : ESP_ERR_TIMEOUT;
}

void AudioService::playBootChime() {
    ESP_LOGI(TAG, "播放开机赛博和弦音效...");
    speaker_.playBootSound();
}

void AudioService::playBeep() {
    speaker_.playBeep();
}

void AudioService::playAlert() {
    ESP_LOGI(TAG, "播放警报音效...");
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
