/**
 * @file attitude.c
 * @brief 自平衡小车六轴姿态融合算法与零偏校准实现
 */

#include "attitude.h"

#include <math.h>

#include "bsp.h"
#include "log.h"

#define RAD_TO_DEG (57.2957795f)  // 180.0f / 3.14159265f
#define FILTER_ALPHA (0.98f)  // 互补滤波系数 (98% 信任陀螺仪高频动态，2% 缓慢吸收加速度计绝对重力)

static Attitude_t s_attitude = {0};

void attitude_init(void) {
    LOG_I("ATT", "Calibrating Gyroscope Bias... Keep Robot Still!");

    // 1. 采集 100 次静止数据，计算陀螺仪的自然静态漂移均值 (零偏)
    float sum_gx = 0.0f;
    float sum_gy = 0.0f;
    Icm42605Data_t raw;

    const int CALIB_SAMPLES = 100;
    for (int i = 0; i < CALIB_SAMPLES; i++) {
        icm42605_read_data(&raw);
        sum_gx += raw.gx;
        sum_gy += raw.gy;
        delay_ms(10);  // 间隔 10ms 采样，总耗时 1.0 秒
    }

    s_attitude.gyro_bias_x = sum_gx / (float)CALIB_SAMPLES;
    s_attitude.gyro_bias_y = sum_gy / (float)CALIB_SAMPLES;
    s_attitude.is_calibrated = true;

    LOG_I("ATT", "Gyro Calibrated! Offset X: %+.2f, Offset Y: %+.2f dps", s_attitude.gyro_bias_x,
          s_attitude.gyro_bias_y);

    // 2. 利用初次加速度计读数，直接初始化初始 Pitch 角度 (消除开机大角度跳跃)
    icm42605_read_data(&raw);
    s_attitude.acc_pitch = atan2f(-raw.ay, raw.az) * RAD_TO_DEG;
    s_attitude.pitch = s_attitude.acc_pitch;
}

void attitude_update(const Icm42605Data_t *imu_data, float dt) {
    if (!s_attitude.is_calibrated || !imu_data)
        return;

    // 1. 扣除陀螺仪零漂，得到绝对纯净的实时角速度
    // 注意：如果是绕 X 轴俯仰则使用 gx，如果是绕 Y 轴俯仰则使用 gy
    float gyro_pitch_rate = imu_data->gx - s_attitude.gyro_bias_x;
    s_attitude.pitch_rate = gyro_pitch_rate;

    // 2. 由加速度计计算当下的静态重力倾角
    // atan2f(-ay, az) 对应前后倾角，小车水平时 ay=0, az=1.0g -> angle = 0度
    float acc_angle = atan2f(-imu_data->ay, imu_data->az) * RAD_TO_DEG;
    s_attitude.acc_pitch = acc_angle;

    // 3. 一阶互补滤波核心方程式
    // 新角度 = 0.98 * (旧角度 + 陀螺仪角速度 * dt) + 0.02 * (加速度计重力倾角)
    s_attitude.pitch = FILTER_ALPHA * (s_attitude.pitch + gyro_pitch_rate * dt) +
                       (1.0f - FILTER_ALPHA) * acc_angle;
}

const Attitude_t *attitude_get(void) {
    return &s_attitude;
}
