/**
 * @file control.h
 * @brief 自平衡小车站立与运动控制算法头文件
 *
 * 【系统架构定位】：属于底盘运动控制算法层 (Motion Control Algorithm Layer)。
 * 【核心控制模型】：串级双闭环 PID (直立 PD 环 + 速度 PI 环 + 转向环 + 跌倒安全保护)。
 * 【核心职责】：
 *  1. 直立内环 (PD)：抵抗重力倾覆力矩，维持小车车身在机械中值竖直站立；
 *  2. 速度外环 (PI)：融合编码器脉冲，消除水平单向漂移，实现定点静止悬停与巡航速度追踪；
 *  3. 安全跌倒保护：倾角超过 ±35° 时强行关断电机驱动，防止小车摔倒后高速空转打齿；
 *  4. 死区补偿：克服 TB6612 驱动芯片与 N20 减速箱的静摩擦力，使微小姿态修正顺滑无震荡。
 */
#ifndef CONTROL_H
#define CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 自平衡机器人 PID 核心参数结构体
 */
typedef struct {
    // --- 1. 直立内环 (PD 控制) ---
    float kp_balance;    // 直立比例系数 (提供对抗重力倾覆的基本弹簧刚度)
    float kd_balance;    // 直立微分系数 (提供阻尼力矩，抑制快速晃动)
    float target_pitch;  // 机械平衡零点角度 (通常在 0° 附近，根据整车重心微调)

    // --- 2. 速度外环 (PI 控制) ---
    float kp_velocity;   // 速度比例系数 (根据车速误差施加回退修正)
    float ki_velocity;   // 速度积分系数 (消除稳态静差，防止小车向单侧缓慢滑移)
    float target_speed;  // 目标线速度 (暂设为 0，即定点静止站立)

    // --- 3. 安全防护参数 ---
    float safe_angle_limit;  // 跌倒保护极限倾角 (单位: 度，默认 35.0°)
    int16_t pwm_deadband;    // TB6612 启动死区补偿 PWM 值 (通常为 80~120)
} BalanceParams_t;

/**
 * @brief 控制器初始化函数
 * @note 装载出厂推荐 PID 基准参数与保护阈值
 */
void control_init(void);

/**
 * @brief 获取控制器当前参数指针 (用于后续调参或遥测)
 */
BalanceParams_t *control_get_params(void);

/**
 * @brief 核心控制周期步进计算函数 (在 100Hz 定时任务中调用)
 * @param current_pitch    当前姿态解算俯仰角 (度)
 * @param current_rate     当前纯净俯仰角速度 (deg/s)
 * @param speed_left       左轮 10ms 编码器增量脉冲
 * @param speed_right      右轮 10ms 编码器增量脉冲
 * @param out_pwm_left     输出计算得到的左轮 PWM (-1000 ~ +1000)
 * @param out_pwm_right    输出计算得到的右轮 PWM (-1000 ~ +1000)
 * @return bool true: 处于正常自平衡态; false: 触发跌倒保护停机
 */
bool control_step(float current_pitch, float current_rate, int16_t speed_left, int16_t speed_right,
                  int16_t *out_pwm_left, int16_t *out_pwm_right);

#ifdef __cplusplus
}
#endif

#endif /* CONTROL_H */
