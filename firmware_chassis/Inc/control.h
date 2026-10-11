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

    // --- 2. 速度外环 (PI 级联角度调制) ---
    float kp_velocity;   // 速度比例系数 (单位: 度 / 编码器脉冲，通过倾角抑制速度漂移)
    float ki_velocity;   // 速度积分系数 (自适应重心跟踪，消除单向溜车)
    float target_speed;  // 目标速度 (脉冲/10ms，默认为 0 静止站立)

    // --- 3. 航向转向环 ---
    float kp_yaw;        // 航向角速度/转向控制系数

    // --- 4. 安全防护参数 ---
    float safe_angle_limit;  // 跌倒保护极限倾角 (单位: 度，低底盘收紧至 9.5° 防托底)
    int16_t pwm_deadband;    // TB6612 启动死区补偿 PWM 值 (通常为 70~85)
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
 * @brief 查询当前小车是否处于跌倒保护状态
 */
bool control_is_fallen(void);

/**
 * @brief 核心控制周期步进计算函数 (在 100Hz 定时任务中调用)
 * @param current_pitch    当前姿态解算俯仰角 (度)
 * @param current_rate     当前纯净俯仰角速度 (deg/s)
 * @param speed_left       左轮 10ms 编码器增量脉冲
 * @param speed_right      右轮 10ms 编码器增量脉冲
 * @param target_yaw       目标转向角速度 (mrad/s, 0=直行保持)
 * @param out_pwm_left     输出计算得到的左轮 PWM (-1000 ~ +1000)
 * @param out_pwm_right    输出计算得到的右轮 PWM (-1000 ~ +1000)
 * @return bool true: 处于正常自平衡态; false: 触发跌倒保护停机
 */
bool control_step(float current_pitch, float current_rate, int16_t speed_left, int16_t speed_right,
                  int16_t target_yaw, int16_t *out_pwm_left, int16_t *out_pwm_right);

/**
 * @brief 双轮差速速度闭环 PID 控制周期计算 (100Hz 周期调用)
 * @param target_speed_mms 目标线速度 (mm/s, 前正后负)
 * @param target_yaw_mrads 目标角速度 (mrad/s, 左正右负)
 * @param speed_left       左轮实际脉冲数 (10ms采样)
 * @param speed_right      右轮实际脉冲数 (10ms采样)
 * @param out_pwm_left     输出左轮 PWM (-1000 ~ +1000)
 * @param out_pwm_right    输出右轮 PWM (-1000 ~ +1000)
 */
void control_diff_pid_step(int16_t target_speed_mms, int16_t target_yaw_mrads,
                           int16_t speed_left, int16_t speed_right,
                           int16_t *out_pwm_left, int16_t *out_pwm_right);

/**
 * @brief 启动定距/定角闭环走位动作
 * @param dist_mm          目标直线位移 (mm, 正前负后, 0 表示仅自转)
 * @param angle_deci_deg   目标自转角度 (0.1度, 正左负右, 0 表示仅直行)
 * @param start_yaw        当前初始航向角 (度)
 */
void control_step_move_start(int16_t dist_mm, int16_t angle_deci_deg, float start_yaw);

/**
 * @brief 定距/定角闭环走位周期更新 (100Hz 周期调用)
 * @param speed_left       左轮实际脉冲数
 * @param speed_right      右轮实际脉冲数
 * @param current_yaw      当前实时航向角 (度)
 * @param out_pwm_left     输出左轮 PWM
 * @param out_pwm_right    输出右轮 PWM
 * @return bool true: 已精准到达目标位置; false: 正在闭环巡航中
 */
bool control_step_move_update(int16_t speed_left, int16_t speed_right, float current_yaw,
                              int16_t *out_pwm_left, int16_t *out_pwm_right);

/**
 * @brief 重置差速 PID 积分器与状态
 */
void control_diff_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* CONTROL_H */
