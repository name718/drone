/**
 * @file motor.h
 * @brief TB6612 双路电机驱动 (TIM3 20kHz 硬件静音 PWM + GPIO 方向控制)
 */
#ifndef MOTOR_H
#define MOTOR_H

#include <stdint.h>

#define MOTOR_MAX_PWM 1000  // 最大 PWM 限制值 (对应 100% 占空比)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 TB6612 电机硬件 (GPIO 与 TIM3 PWM 20kHz)
 */
void motor_init(void);

/**
 * @brief 设置左右电机转速与方向
 * @param left_speed  左轮速度 (-1000 ~ +1000，正数向前，负数向后，0 停止)
 * @param right_speed 右轮速度 (-1000 ~ +1000，正数向前，负数向后，0 停止)
 */
void motor_set_speed(int16_t left_speed, int16_t right_speed);

/**
 * @brief 紧急刹车并禁用驱动
 */
void motor_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_H */
