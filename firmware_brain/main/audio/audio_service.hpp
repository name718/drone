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
     *
     * 【首字保全与预缓冲回溯 (Pre-roll Buffer) 核心机制】：
     *  在声控交互中，当主人喊出“小智同学”时，声音能量需要经历起音阶段（约 200~500ms）
     *  才能累积超过唤醒阈值。如果系统在检测到声音时才开始创建录音，往往会导致前 1~2 个字
     *  （如“小”、“智”）被截断，云端 ASR 只能听到半截话而识别失败或无法匹配唤醒词。
     *  本接口通过在 64KB PSRAM 环形缓冲区中将读指针回溯 preroll_samples（如 12800 点 = 800ms），
     *  使得随后的 readAudioStream 能无损读出“尚未触发唤醒前”的历史发音，保全完整语音。
     * @param preroll_samples 从当前时间点往前提取的历史预缓冲采样点数 (12800 点 @ 16kHz = 800ms)
     */
    void startAudioStream(size_t preroll_samples = 12800);

    /**
     * @brief 停止音频流消费 (重置消费状态，结束当前 ASR 会话推流)
     */
    void stopAudioStream();

    /**
     * @brief 从环形音频流中读取 16-Bit 单声道 PCM 数据 (非阻塞/带超时等待)
     *
     * 【单生产者单消费者 (SPSC) 队列模型】：
     *  后台 audioTask 作为唯一生产者，以 100% 占空比不断将麦克风采集的 PCM 写入 PSRAM 环形缓冲区；
     *  本接口作为唯一消费者从中读取数据。当缓冲区中积存数据不足 sample_count 时，
     *  任务会自动挂起在 stream_sem_ 信号量上等待后台生产，避免忙轮询消耗 CPU 算力。
     * @param dest 目标数据缓冲数组指针
     * @param sample_count 期望读取的采样点数 (如 1600 点 = 100ms)
     * @param samples_read 实际成功读取的采样点数指针
     * @param timeout_ms 超时毫秒数
     * @return esp_err_t ESP_OK 成功读取到数据，ESP_ERR_TIMEOUT 超时
     */
    esp_err_t readAudioStream(int16_t *dest, size_t sample_count, size_t *samples_read, uint32_t timeout_ms = 100);

private:
    AudioService();
    ~AudioService();

    AudioService(const AudioService &) = delete;
    AudioService &operator=(const AudioService &) = delete;

    static void audioTask(void *param);

    SpeakerDriver speaker_;  // MAX98357A 扬声器底层硬件驱动
    MicDriver mic_;          // INMP441 全向麦克风底层硬件驱动

    TaskHandle_t task_handle_{nullptr};
    bool is_running_{false};
    float current_energy_{0.0f};  // 实时环境连续均方根声压能量 (0.0f ~ 1.0f)

    // ========================================================================
    // SPSC 全时态音频环形预缓冲区参数 (分配在 8MB 外部 PSRAM)
    // ========================================================================
    // 16000Hz * 2秒 * 2字节 = 32000 个采样点 (占据 64KB 连续内存)
    static constexpr size_t RING_BUF_SAMPLES = 32000;
    int16_t *ring_buffer_{nullptr};       // 指向 PSRAM 连续环形内存块的指针
    size_t ring_write_count_{0};          // 生产者 (audioTask) 累积写入的历史采样总数
    size_t ring_read_count_{0};           // 消费者 (readAudioStream) 累积读取的历史采样总数
    std::atomic<bool> is_streaming_{false}; // 当前是否处于 ASR 流式消费状态
    SemaphoreHandle_t stream_sem_{nullptr}; // 跨线程数据就绪同步信号量
    portMUX_TYPE ring_mux_ = portMUX_INITIALIZER_UNLOCKED; // 多核自旋锁，保护读写指针一致性
};
