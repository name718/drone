/**
 * @file ai_service.hpp
 * @brief 机器人云端大模型 (LLM) 与语音合成 (CosyVoice TTS) 核心服务头文件
 *
 * 【职责】：
 *  1. 接收来自 Web 控制台或语音唤醒的用户对话指令；
 *  2. 调用阿里云百炼大模型进行流式/文本思考推理；
 *  3. 解析模型回复中包含的实体机器人动作标签 ([ACTION:XXX]) 并分发到底盘、云台、表情引擎；
 *  4. 建立 WebSocket 长连接至 CosyVoice 语音合成服务，接收 16kHz PCM 音频流并通过 I2S 实时驱动扬声器；
 *  5. 全流程双工状态实时回传至 Web 控制台。
 */
#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

class AiService {
public:
    static AiService &getInstance();

    /**
     * @brief 初始化 AI 业务服务队列与相关资源
     * @return esp_err_t ESP_OK 表示初始化成功
     */
    esp_err_t init();

    /**
     * @brief 启动 Core 0 上的独立 AI 后台任务
     * @return esp_err_t 启动状态
     */
    esp_err_t start();

    /**
     * @brief 提交用户对话或交互提示词
     * @param prompt 用户文本
     */
    void ask(const std::string &prompt);

    /**
     * @brief 触发麦克风开启语音倾听并向云端 ASR 识别 (声控交互入口)
     */
    void triggerVoiceListen();

    /**
     * @brief 播放系统开机问候语 (网络就绪后由调度中枢调用)
     */
    void playBootVoiceGreeting();

    /**
     * @brief 查询当前大模型是否处于推理、发声或聆听状态
     */
    bool isBusy() const { return is_busy_.load(); }

private:
    AiService();
    ~AiService();

    AiService(const AiService &) = delete;
    AiService &operator=(const AiService &) = delete;

    /**
     * @brief 运行在 Core 0 上的独立 AI 调度工作线程
     */
    static void aiTaskWorker(void *param);

    void processVoiceInteraction();
    void processTextInteraction(const std::string &prompt);

    /**
     * @brief 麦克风录音并向 DashScope ASR 推流，返回识别出的文本
     */
    std::string recordAndTranscribe();

    /**
     * @brief 调用阿里云 DashScope 大模型 HTTP API 完成对话理解
     * @param user_prompt 用户提问内容
     * @return std::string 模型原始回复内容
     */
    std::string callLlm(const std::string &user_prompt);

    /**
     * @brief 调用 DashScope CosyVoice WebSocket 进行流式语音合成并在机载喇叭实时播放
     * @param text_to_speak 需朗读的文本
     */
    void streamTts(const std::string &text_to_speak);

    std::string dispatchActions(const std::string &reply_raw);

    struct ChatHistoryItem {
        std::string role;
        std::string content;
    };
    std::vector<ChatHistoryItem> history_;

    int64_t last_interaction_time_ms_{0};  // 上次有效交互时间戳 (用于多轮免唤醒会话管理)
    std::atomic<bool> is_busy_{false};
    QueueHandle_t command_queue_{nullptr};
    TaskHandle_t task_handle_{nullptr};
};
