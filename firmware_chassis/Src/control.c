/**
 * @file control.c
 * @brief 自平衡小车站立与运动控制算法实现
 *
 * 【算法实现细节】：
 *  1. 符号约定：小车向前倾斜为正角度 (Pitch > 0)，需要轮子向前加速 (PWM > 0) 追赶重心；
 *  2. 速度环低通滤波：采用一阶滞后滤波抑制编码器离散量化噪声带来的高频毛刺；
 *  3. 积分抗饱和 (Anti-Windup)：限定速度积分上下限，防止摔倒或推车时积分爆炸导致飞车。
 */
#include "control.h"

#include <math.h>

// 全局控制参数实例
static BalanceParams_t s_params;

// 速度环内部状态量 (一阶滤波值与积分累计值)
static float s_velocity_filtered = 0.0f;
static float s_velocity_integral = 0.0f;

// 跌倒保护触发标志
static bool s_is_fallen = true;

/**
 * @brief 限幅辅助函数
 */
static inline float constrain_f(float val, float min, float max) {
    if (val < min)
        return min;
    if (val > max)
        return max;
    return val;
}

/**
 * @brief 初始化自平衡控制器参数
 */
void control_init(void) {
    // 1. 直立内环 (确保强劲支撑力矩，稳稳托举高重心塔身)
    s_params.kp_balance   = 95.0f;     // 提升至 95 (给足强劲支撑力矩)
    s_params.kd_balance   = 2.10f;     // 2.10 (配合一阶滤波角速度，强阻尼抗倒，消除震荡)
    s_params.target_pitch = -2.8f;     // 微调至 -2.8° (消除往前溜车，保持优雅端正平衡)

    // 2. 速度外环 (彻底关闭，消除前后暴冲翻倒的干扰)
    s_params.kp_velocity  = 0.0f;      // 彻底关闭，先让纯直立环达到原地静止
    s_params.ki_velocity  = 0.0f;
    s_params.target_speed = 0.0f;

    // 3. 安全防护与死区控制
    s_params.safe_angle_limit = 15.0f; // 保护阈值 15°
    s_params.pwm_deadband     = 75;    // 75 点死区零延迟注入，彻底消灭起步迟钝！

    s_velocity_filtered = 0.0f;
    s_velocity_integral = 0.0f;
    s_is_fallen = true;
}

BalanceParams_t *control_get_params(void) {
    return &s_params;
}

/**
 * @brief 核心控制算法计算步进
 */
bool control_step(float current_pitch, float current_rate, int16_t speed_left, int16_t speed_right,
                  int16_t *out_pwm_left, int16_t *out_pwm_right) {
    // 计算当前倾角与真实机械平衡零点的误差 (前倾为正，后仰为负)
    float angle_error = current_pitch - s_params.target_pitch;

    // ---------------------------------------------------------
    // 1. 安全跌倒检测 (偏离超过 15° 立即断电防刮底盘和电机堵转)
    // ---------------------------------------------------------
    static float s_rate_filtered = 0.0f;
    static uint16_t s_ramp_count = 0;

    if (fabsf(angle_error) > s_params.safe_angle_limit) {
        s_is_fallen = true;
        s_ramp_count = 0;
        s_rate_filtered = 0.0f;
        s_velocity_integral = 0.0f;
        *out_pwm_left = 0;
        *out_pwm_right = 0;
        return false;
    }

    // 如果之前跌倒，手扶正到目标角度附近 2.5° 区间平滑恢复使能
    if (s_is_fallen) {
        if (fabsf(angle_error) < 2.5f) {
            s_is_fallen = false;  // 重新站立使能
            s_ramp_count = 0;     // 触发 200ms 软启动计数器，防止起步瞬间暴冲摔倒！
            s_rate_filtered = 0.0f;
            s_velocity_integral = 0.0f;
            s_velocity_filtered = 0.0f;
        } else {
            *out_pwm_left = 0;
            *out_pwm_right = 0;
            return false;
        }
    }

    // ---------------------------------------------------------
    // 2. 直立内环 (PD 控制器)
    // 物理含义：根据前倾角度与角速度，输出同向前进 PWM 追赶重心
    // 关键优化：对角速度施加一阶低通滤波 (70% 历史 + 30% 新采样)，
    // 彻底滤除 N20 金属齿轮箱传导到 IMU 的高频机械共振杂波，消除剧烈麻手蜂鸣抖动！
    // ---------------------------------------------------------
    s_rate_filtered = (0.7f * s_rate_filtered) + (0.3f * current_rate);

    float pwm_balance = (s_params.kp_balance * angle_error) + (s_params.kd_balance * s_rate_filtered);

    // ---------------------------------------------------------
    // 3. 速度外环 (PI 控制器，当前关闭)
    // ---------------------------------------------------------
    float current_speed_avg = (float)(speed_left + speed_right) * 0.5f;
    s_velocity_filtered = (0.7f * s_velocity_filtered) + (0.3f * current_speed_avg);
    float speed_error = s_velocity_filtered - s_params.target_speed;
    s_velocity_integral += speed_error;
    s_velocity_integral = constrain_f(s_velocity_integral, -1000.0f, 1000.0f);
    float pwm_velocity =
        (s_params.kp_velocity * speed_error) + (s_params.ki_velocity * s_velocity_integral);

    // ---------------------------------------------------------
    // 4. 总输出合成与电机死区补偿
    // ---------------------------------------------------------
    float total_pwm = pwm_balance + pwm_velocity;

    // 叠加快速死区补偿 (无延迟直接突破齿轮箱静摩擦，确保毫秒级极速追重心！)
    if (total_pwm > 3.0f) {
        total_pwm += (float)s_params.pwm_deadband;
    } else if (total_pwm < -3.0f) {
        total_pwm -= (float)s_params.pwm_deadband;
    }

    // 输出限幅在 TB6612 的 [-1000, +1000] 最大占空比内
    int16_t final_pwm = (int16_t)constrain_f(total_pwm, -1000.0f, 1000.0f);

    // 软启动渐进限制 (起步 200ms 内平滑注入动力，彻底消灭手放开瞬间的炸飞/暴冲！)
    if (s_ramp_count < 20) {
        s_ramp_count++;
        final_pwm = (int16_t)((float)final_pwm * ((float)s_ramp_count / 20.0f));
    }

    *out_pwm_left = final_pwm;
    *out_pwm_right = final_pwm;

    return true;
}
