
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

/**
 * @brief 识别出的语音指令枚举
 */
enum class VoiceCommand {
    NONE = 0,         // 无有效指令 (静音/环境底噪)
    WAKEUP,           // 语音唤醒 (检测到人类主动发声)
    COMMAND_FORWARD,  // 指令：向前走
    COMMAND_BACK,     // 指令：向后退
    COMMAND_HAPPY,    // 指令：笑一个 / 开心
};

/**
 * @brief 机器人端侧语音识别与指令引擎类
 */
class SpeechEngine {
public:
    // 等价的传统 C 风格写法
    // typedef std::function<void(VoiceCommand)> CommandCallback;
    using CommandCallback = std::function<void(VoiceCommand)>;

    SpeechEngine(uint32_t sample_rate = 16000);
    ~SpeechEngine() = default;

    /**
     * @brief 注册识别出指令时的回调函数
     */
    void setOnCommand(CommandCallback cb) { m_cmdCb = cb; }

    /**
     * @brief 核心音频流喂入与处理函数 (每次输入一帧 16-bit PCM 采样点，如512 点)
     * @param pcm_data 16位 PCM 采样点数组
     * @param samples 采样点数
     */
    void feedAudio(const int16_t *pcm_data, size_t samples);

    /**
     * @brief 当前是否处于被唤醒激活状态
     */
    bool isAwake() const { return m_isAwake; }

private:
    uint32_t m_sampleRate;
    bool m_isAwake;
    uint32_t m_wakeExpireTimeMs;  // 唤醒超时自动休眠时间戳
    CommandCallback m_cmdCb;

    // --- VAD (Voice Activity Detection) 状态参数 ---
    float m_energyHistory[8];
    size_t m_historyIdx;

    // 内部端点检测与特征计算
    void processVAD(float current_volume);
};
