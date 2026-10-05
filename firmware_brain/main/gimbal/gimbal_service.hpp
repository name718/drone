/**
 * @file gimbal_service.hpp
 * @brief 机器人二自由度头部云台业务服务头文件
 *
 * 【架构职责】：
 *  1. 单例纳管舵机硬件驱动与平滑插值滤波算法；
 *  2. 托管专属于 Core 1 的 50Hz (20ms 周期) 独立运动学解算线程；
 *  3. 提供低冲击平滑加减速算法，彻底避免舵机瞬间大电流拉垮电源及塑料齿轮打齿；
 *  4. 提供丰富的拟人化肢体语言接口：点头 (Nod)、摇头 (Shake)、回正 (Reset)、指定凝视 (LookAt)。
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
     * @brief 启动 Core 1 上的 50Hz 平滑插值计算任务
     */
    esp_err_t start();

    /**
     * @brief 设置头部平滑凝视目标角度 (由后台任务负责平滑逼近)
     * @param pan 水平偏航角 (0~180度)
     * @param tilt 俯仰角 (0~180度)
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
     * @brief 获取云台当前物理输出的实时水平角度 (Pan, 单位: 度)
     */
    float getCurrentPan() const { return current_pan_.load(); }

    /**
     * @brief 获取云台当前物理输出的实时俯仰角度 (Tilt, 单位: 度)
     */
    float getCurrentTilt() const { return current_tilt_.load(); }

    /**
     * @brief 获取云台当前目标设定水平角度 (Pan, 单位: 度)
     */
    float getTargetPan() const { return target_pan_.load(); }

    /**
     * @brief 获取云台当前目标设定俯仰角度 (Tilt, 单位: 度)
     */
    float getTargetTilt() const { return target_tilt_.load(); }

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
