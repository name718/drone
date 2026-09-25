/**
 * @file attitude.h
 * @brief 自平衡小车六轴姿态融合算法与零偏校准 (互补滤波)
 */
#ifndef ATTITUDE_H
#define ATTITUDE_H

#include <stdbool.h>
#include <stdint.h>

#include "icm42605.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 姿态解算核心数据结构体
 */
typedef struct {
    float pitch;         // 核心俯仰角 (单位: 度 deg，小车直立为 0，前倾为正，后仰为负)
    float pitch_rate;    // 俯仰角速度 (单位: deg/s，扣除零漂后的纯净角速度)
    float roll;          // 横滚角 (单位: deg)
    float acc_pitch;     // 加速度计纯静态角度 (供对比观测滤波效果)
    float gyro_bias_x;   // X 轴陀螺仪静态零偏
    float gyro_bias_y;   // Y 轴陀螺仪静态零偏
    bool is_calibrated;  // 零偏校准是否完成
} Attitude_t;

/**
 * @brief 初始化姿态解算器，并执行陀螺仪静态零偏校准
 * @note  调用时请将小车静止平放在桌面上 1~2 秒！
 */
void attitude_init(void);

/**
 * @brief 100Hz 周期姿态融合滤波更新
 * @param imu_data 传感器六轴物理量
 * @param dt 采样周期 (单位: 秒，100Hz 对应 0.01f)
 */
void attitude_update(const Icm42605Data_t *imu_data, float dt);

/**
 * @brief 获取当前姿态解算结果指针
 */
const Attitude_t *attitude_get(void);

#ifdef __cplusplus
}
#endif

#endif /* ATTITUDE_H */
