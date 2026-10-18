/**
 * @file gimbal_service.hpp
 * @brief 机器人二自由度头部云台业务服务头文件
 *
 * 【架构职责】：
 *  默认远程模式保留lookAt/nod/shake/reset业务接口，把硬件PWM、平滑和手势交给C8。
 *  旧接线兼容分支仅用于恢复历史硬件；新接线下不初始化ESP LEDC。
 */
#pragma once

#include <atomic>

#include "config/board_config.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gimbal/servo_driver.hpp"

// 拟人动作手势类型枚举
enum class GimbalGesture {
    NONE,    // 无手势，执行自由凝视或静止
    NOD,     // 点头动作 (认同、打招呼)
    SHAKE,   // 摇头动作 (拒绝、不知道)
    RESET    // 回正居中
};

class GimbalService {
public:
    static GimbalService &getInstance();

    /**
     * @brief 初始化舵机底层硬件
     */
    esp_err_t init();

    /**
     * @brief 启动头部通信并请求使能舵机；C8本地执行50Hz控制
     */
    esp_err_t start();

    /**
     * @brief 设置头部平滑凝视目标角度 (由后台任务负责平滑逼近)
     * @param pan 水平角，单位度；远程模式按HeadLimits裁剪，默认9~52度
     * @param tilt 俯仰角，单位度；远程模式默认0~25度
     */
    void lookAt(float pan, float tilt);

    /**
     * @brief 触发拟人点头动作 (非阻塞，由内部状态机自动推进)
     */
    void nod();

    /**
     * @brief 触发拟人摇头动作 (非阻塞)
     */
    void shake();

    /**
     * @brief 快速回正居中
     */
    void reset();

    /**
     * @brief 获取C8报告的PWM平滑输出角度(Pan)，不是带传感器的实测角度；须同时检查链路在线
     */
    float getCurrentPan() const;

    /**
     * @brief 获取C8报告的PWM平滑输出角度(Tilt)，不是舵机位置反馈
     */
    float getCurrentTilt() const;

    /**
     * @brief 获取云台当前目标设定水平角度 (Pan, 单位: 度)
     */
    float getTargetPan() const;

    /**
     * @brief 获取云台当前目标设定俯仰角度 (Tilt, 单位: 度)
     */
    float getTargetTilt() const;

private:
    GimbalService();
    ~GimbalService() = default;

    GimbalService(const GimbalService &) = delete;
    GimbalService &operator=(const GimbalService &) = delete;

    // 50Hz 平滑插值任务主循环
    static void gimbalTask(void *param);

    // 动作手势帧推进
    void updateGesture();

    ServoDriver driver_;          // 硬件 PWM 舵机驱动
    TaskHandle_t task_handle_{nullptr};
    bool is_running_{false};

    // 平滑插值目标与当前实际输出值 (使用单点信任源中的实机校准角度，原子线程安全)
    std::atomic<float> target_pan_{Config::Gimbal::DEFAULT_PAN_ANGLE};
    std::atomic<float> target_tilt_{Config::Gimbal::DEFAULT_TILT_ANGLE};
    std::atomic<float> current_pan_{Config::Gimbal::DEFAULT_PAN_ANGLE};
    std::atomic<float> current_tilt_{Config::Gimbal::DEFAULT_TILT_ANGLE};

    // 手势状态机
    GimbalGesture active_gesture_{GimbalGesture::NONE};
    int gesture_step_{0};
    uint32_t gesture_tick_count_{0};
};
