#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

/**
 * @brief 初始化 TIM2 (左轮) 与 TIM1 (右轮) 硬件正交编码器接口 (4倍频)
 */
void encoder_init(void);

/**
 * @brief 读取并清除/计算两轮在控制周期内的脉冲增量 (Delta)
 * @param left_speed 输出左轮脉冲变化量 (速度)
 * @param right_speed 输出右轮脉冲变化量 (速度)
 */
void encoder_get_speed(int16_t *left_speed, int16_t *right_speed);

#endif  // ENCODER_H
