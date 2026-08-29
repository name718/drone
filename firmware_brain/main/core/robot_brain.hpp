/**
 * @file robot_brain.hpp
 * @brief 机器人大脑总控中枢 (单例模式 / 统一服务调度)
 */

#pragma once

#include "display_service.hpp"
#include "audio_service.hpp"
#include "motion_service.hpp"

class RobotBrain {
public:
    /**
     * @brief 获取机器人大脑单例对象
     */
    static RobotBrain& getInstance();

    /**
     * @brief 系统硬件自检与所有子系统初始化
     */
    esp_err_t init();

    /**
     * @brief 启动所有多核多任务服务
     */
    esp_err_t start();

    // --- 子服务访问接口 ---
    DisplayService& getDisplay() { return m_display; }
    AudioService&   getAudio()   { return m_audio; }
    MotionService&  getMotion()  { return m_motion; }

private:
    RobotBrain();
    ~RobotBrain() = default;
    RobotBrain(const RobotBrain&) = delete;
    RobotBrain& operator=(const RobotBrain&) = delete;

    DisplayService m_display;
    AudioService   m_audio;
    MotionService  m_motion;

    void printSystemDiagnostics();
    static void interactionTaskEntry(void* pvParameters);
};
