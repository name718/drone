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
static float s_last_acc_mag = 1.0f;
static uint8_t s_tap_hold_count = 0;
static uint8_t s_pickup_hold_count = 0;

void attitude_init(void) {
    LOG_I("姿态", "正在校准陀螺仪静态零偏... 请保持小车完全静止！");

    // 1. 采集 100 次静止数据，计算陀螺仪的自然静态漂移均值 (零偏)
    float sum_gx = 0.0f;
    float sum_gy = 0.0f;
    float sum_gz = 0.0f;
    Icm42605Data_t raw;

    const int CALIB_SAMPLES = 100;
    for (int i = 0; i < CALIB_SAMPLES; i++) {
        icm42605_read_data(&raw);
        sum_gx += raw.gx;
        sum_gy += raw.gy;
        sum_gz += raw.gz;
        delay_ms(10);  // 间隔 10ms 采样，总耗时 1.0 秒
    }

    s_attitude.gyro_bias_x = sum_gx / (float)CALIB_SAMPLES;
    s_attitude.gyro_bias_y = sum_gy / (float)CALIB_SAMPLES;
    s_attitude.gyro_bias_z = sum_gz / (float)CALIB_SAMPLES;
    s_attitude.yaw = 0.0f;
    s_attitude.is_calibrated = true;

    LOG_I("姿态", "陀螺仪校准完毕！零偏 X: %+.2f, Y: %+.2f, Z: %+.2f 度/秒",
          s_attitude.gyro_bias_x, s_attitude.gyro_bias_y, s_attitude.gyro_bias_z);

    // 2. 利用初次加速度计读数，直接初始化初始 Pitch 与 Roll 角度 (消除开机大角度跳跃)
    icm42605_read_data(&raw);
    s_attitude.acc_pitch = atan2f(-raw.ay, raw.az) * RAD_TO_DEG;
    s_attitude.pitch = s_attitude.acc_pitch;
    s_attitude.roll = atan2f(raw.ax, raw.az) * RAD_TO_DEG;
    s_attitude.acc_mag = sqrtf(raw.ax * raw.ax + raw.ay * raw.ay + raw.az * raw.az);
    s_last_acc_mag = s_attitude.acc_mag;
    s_attitude.is_tapped = false;
    s_attitude.is_picked_up = false;
}

void attitude_update(const Icm42605Data_t *imu_data, float dt) {
    if (!s_attitude.is_calibrated || !imu_data)
        return;

    // 1. 扣除陀螺仪零漂，得到纯净角速度
    float gyro_pitch_rate = -(imu_data->gx - s_attitude.gyro_bias_x);
    float gyro_roll_rate  = (imu_data->gy - s_attitude.gyro_bias_y);
    float gyro_yaw_rate   = (imu_data->gz - s_attitude.gyro_bias_z);

    s_attitude.pitch_rate = gyro_pitch_rate;

    // 连续航向角积分 (扣除 Z 轴零偏)
    s_attitude.yaw += gyro_yaw_rate * dt;

    // 2. 计算加速度计倾角与矢量模长
    float acc_angle_pitch = atan2f(-imu_data->ay, imu_data->az) * RAD_TO_DEG;
    float acc_angle_roll  = atan2f(imu_data->ax, imu_data->az) * RAD_TO_DEG;
    s_attitude.acc_pitch  = acc_angle_pitch;

    float acc_mag = sqrtf(imu_data->ax * imu_data->ax +
                          imu_data->ay * imu_data->ay +
                          imu_data->az * imu_data->az);
    s_attitude.acc_mag = acc_mag;

    // 3. 互补滤波解算俯仰角 (Pitch) 与横滚角 (Roll)
    s_attitude.pitch = FILTER_ALPHA * (s_attitude.pitch + gyro_pitch_rate * dt) +
                       (1.0f - FILTER_ALPHA) * acc_angle_pitch;

    s_attitude.roll  = FILTER_ALPHA * (s_attitude.roll + gyro_roll_rate * dt) +
                       (1.0f - FILTER_ALPHA) * acc_angle_roll;

    // 4. 【体感感知算法一：敲击桌面/拍头事件检测 (Tap/Knock)】
    // 加速度模长高频冲击变化量 (Jerk) 或绝对偏差跃变，且伴随高频角速度冲量
    float delta_jerk = fabsf(acc_mag - s_last_acc_mag);
    s_last_acc_mag = acc_mag;

    float gyro_total_rate = fabsf(gyro_pitch_rate) + fabsf(gyro_roll_rate) + fabsf(gyro_yaw_rate);
    if ((delta_jerk > 0.40f || fabsf(acc_mag - 1.0f) > 0.50f) && gyro_total_rate > 30.0f) {
        s_tap_hold_count = 15; // 保持 15 个周期 (150ms)，确保 50Hz 遥测帧必能捕捉
    }

    if (s_tap_hold_count > 0) {
        s_tap_hold_count--;
        s_attitude.is_tapped = true;
    } else {
        s_attitude.is_tapped = false;
    }

    // 5. 【体感感知算法二：悬空离地/被抱起事件检测 (Pick-up / Free-fall)】
    // 判定依据：离开桌面瞬间出现失重 (acc_mag < 0.65g) 或离开支承面悬空倾侧 (|pitch| > 22° 或 |roll| > 22°)
    // 且未直接平躺倒地 (|pitch| < 45° 处于空中悬空角度)
    bool is_in_air_attitude = (fabsf(s_attitude.pitch) > 18.0f || fabsf(s_attitude.roll) > 18.0f);
    bool is_freefall = (acc_mag < 0.65f);

    if (is_freefall || is_in_air_attitude) {
        if (s_pickup_hold_count < 25) {
            s_pickup_hold_count++;
        }
    } else {
        if (s_pickup_hold_count > 0) {
            s_pickup_hold_count--;
        }
    }

    s_attitude.is_picked_up = (s_pickup_hold_count >= 8); // 持续 >80ms 确认悬空离地状态
}

const Attitude_t *attitude_get(void) {
    return &s_attitude;
}
