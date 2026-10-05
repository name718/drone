/**
 * @file interaction_service.hpp
 * @brief 声-画-机多模态拟人自主交互行为引擎头文件
 *
 * 【架构设计与核心职责】：
 *  1. 统一中枢协调器：将眼睛（Display/FaceEngine）、脖子（GimbalService）、
 *     耳朵（AudioService 麦克风能量）、嘴巴（AudioService 扬声器）、底盘（ChassisService）
 *     深度联结为一个有“生命感”的有机整体；
 *  2. 拟人多模态自主行为状态机 (Behavior State Machine)：
 *     - 声音/拍手唤醒联动（Sound Wakeup）：耳朵听到声音 -> 眼睛瞬间睁大好奇 -> 脖子抬头望向声源 -> 嘴巴清脆回答；
 *     - 发呆打盹挂机机制（Idle & Sleepiness）：长时间无操作自动进入困倦打盹，偶尔自主探头微晃，模拟小动物；
 *     - 情感复合连招（Embodied Emotion Combos）：开心月牙摇摆、受惊警报退避、点头赞同、摇头拒绝；
 *  3. 人机共融设计：用户手动操作具有最高优先级，操作时自动抑制自主行为，避免“人机打架”。
 */
#pragma once

#include <atomic>
#include <cstdint>

#include "audio/audio_service.hpp"
#include "display/display_service.hpp"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gimbal/gimbal_service.hpp"

/**
 * @brief 机器人多模态拟人行为类型枚举
 */
enum class RobotBehavior {
    NORMAL,    // 恢复正常常态 (灵动大眼 + 居中水平)
    WAKE_UP,   // 声音/拍手被唤醒 (睁大眼睛好奇 + 抬头微仰 + 提示短音)
    HAPPY,     // 开心欢喜连招 (月牙微笑 + 活泼晃头 + 双音大调音效)
    ALERT,     // 警觉避险连招 (警惕眼神 + 紧急提示音 + 姿态收缩)
    SLEEP,     // 困倦打盹休眠 (困倦眯眼 + 垂头放平 + 舒缓呼吸)
    NOD,       // 赞同点头连招 (连续点头动作 + 提示音)
    SHAKE      // 否定摇头连招 (连续摇头动作 + 沉闷双音)
};

class InteractionService {
public:
    /**
     * @brief 获取多模态交互引擎的全局单例 (线程安全)
     */
    static InteractionService &getInstance();

    /**
     * @brief 初始化交互引擎
     */
    esp_err_t init();

    /**
     * @brief 启动后台 10Hz 拟人行为决策任务 (绑定 Core 0)
     */
    esp_err_t start();

    /**
     * @brief 设置是否启用自主拟人行为 (生命感模式)
     * @param enable true=开启自主声控唤醒与发呆打盹; false=纯手动遥控
     */
    void setAutonomousMode(bool enable);

    /**
     * @brief 查询当前是否开启自主拟人模式
     */
    bool isAutonomousMode() const { return autonomous_mode_.load(); }

    /**
     * @brief 通知交互引擎“用户正在操控” (刷新活跃时间戳)
     * @note 当从 Web 收到按键、遥控滑条或底盘控制时调用，防止自主行为干扰用户
     */
    void notifyUserActivity();

    /**
     * @brief 主动触发特定的声-画-机复合拟人行为
     * @param behavior 目标行为
     */
    void triggerBehavior(RobotBehavior behavior);

private:
    InteractionService();
    ~InteractionService();

    InteractionService(const InteractionService &) = delete;
    InteractionService &operator=(const InteractionService &) = delete;

    /**
     * @brief FreeRTOS 任务主体 (10Hz 运行在 Core 0)
     */
    static void interactionTask(void *param);

    TaskHandle_t task_handle_{nullptr};
    std::atomic<bool> is_running_{false};
    std::atomic<bool> autonomous_mode_{true};  // 默认开启自主拟人生命感模式

    // 时间戳与状态跟踪 (毫秒)
    uint64_t last_activity_time_ms_{0};     // 上次外部交互时间戳
    uint64_t last_sound_trigger_ms_{0};     // 上次声音唤醒时间戳 (防重复高频误触)
    uint64_t wakeup_return_normal_ms_{0};   // 声音唤醒后恢复常态的计划时间戳
    uint64_t next_micro_action_ms_{0};      // 下一次发呆微动作时间戳

    bool is_sleeping_{false};               // 是否正处于打盹状态
    bool is_in_sound_reaction_{false};      // 是否正处于声音反应期
};
