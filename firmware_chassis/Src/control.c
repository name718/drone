#include "control.h"

#include <math.h>

// 全局控制参数实例
static BalanceParams_t s_params;

// 速度外环内部状态量 (一阶低通滤波速度与自适应积分)
static float s_velocity_filtered = 0.0f;
static float s_velocity_integral = 0.0f;

// 陀螺仪角速度一阶低通滤波
static float s_rate_filtered = 0.0f;

// 软启动渐进计数器
static uint16_t s_ramp_count = 0;

// 跌倒保护触发标志
static bool s_is_fallen = true;

/**
 * @brief 浮点限幅辅助函数
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
/**
 * @brief 初始化自平衡控制器参数
 */
void control_init(void) {
    // 1. 直立内环 (专为高重心结构调校：正向反馈纠正，适中刚度避免过冲)
    s_params.kp_balance   = 48.0f;     // 直立比例刚度 (平稳支撑)
    s_params.kd_balance   = 1.10f;     // 直立微分阻尼 (抑制高重心摆动惯性)
    s_params.target_pitch = 0.28f;      // 精确物理重心零点 (实测 0.0° 前冲与 0.56° 后冲之间的真实力矩中位点)

    // 2. 速度外环 (50ms 经典 20Hz 分频采样，轻柔阻尼定点悬停)
    s_params.kp_velocity  = 0.50f;     // 柔和速度比例
    s_params.ki_velocity  = 0.002f;    // 缓慢积分抗漂移
    s_params.target_speed = 0.0f;

    // 3. 航向转向环
    s_params.kp_yaw       = 0.25f;

    // 4. 安全防护与死区控制
    s_params.safe_angle_limit = 25.0f; // 彻底放宽至 25.0°，杜绝自杀式停机
    s_params.pwm_deadband     = 25;    // 降至 25 点微死区，杜绝弹射冲刺

    s_velocity_filtered = 0.0f;
    s_velocity_integral = 0.0f;
    s_rate_filtered = 0.0f;
    s_ramp_count = 0;
    s_is_fallen = true;
}

BalanceParams_t *control_get_params(void) {
    return &s_params;
}

bool control_is_fallen(void) {
    return s_is_fallen;
}

/**
 * @brief 核心控制算法计算步进 (100Hz 周期调用)
 */
bool control_step(float current_pitch, float current_rate, int16_t speed_left, int16_t speed_right,
                  int16_t target_yaw, int16_t *out_pwm_left, int16_t *out_pwm_right) {
    static uint8_t s_speed_divider = 0;
    static float s_velocity_out = 0.0f;

    // ---------------------------------------------------------
    // 1. 物理坐标与误差解算
    // 物理事实：小车前倾时 Pitch 减小；后仰时 Pitch 增大。
    // 因此：前倾偏差 forward_error = target_pitch - current_pitch > 0
    // ---------------------------------------------------------
    float forward_error = s_params.target_pitch - current_pitch;

    // 安全跌倒检测：偏离平衡点超过 25° 立即切断动力停机保护
    if (fabsf(forward_error) > s_params.safe_angle_limit) {
        s_is_fallen = true;
        s_ramp_count = 0;
        s_rate_filtered = 0.0f;
        s_velocity_integral = 0.0f;
        s_velocity_filtered = 0.0f;
        s_velocity_out = 0.0f;
        *out_pwm_left = 0;
        *out_pwm_right = 0;
        return false;
    }

    // 手扶起保护：若之前跌倒，扶正至目标角度 ±3.5° 区间内平滑重新激活
    if (s_is_fallen) {
        if (fabsf(forward_error) < 3.5f) {
            s_is_fallen = false;  // 重新使能
            s_ramp_count = 0;     // 启动 150ms 缓启动防暴冲
            s_rate_filtered = 0.0f;
            s_velocity_integral = 0.0f;
            s_velocity_filtered = 0.0f;
            s_velocity_out = 0.0f;
        } else {
            *out_pwm_left = 0;
            *out_pwm_right = 0;
            return false;
        }
    }

    // ---------------------------------------------------------
    // 2. 直立内环 (修正极性后的 100Hz 纯净 PD 控制器)
    // 前倾角速度为负 (current_rate < 0)，故 - kd * current_rate 为正，协同向前追赶
    // ---------------------------------------------------------
    s_rate_filtered = (0.80f * s_rate_filtered) + (0.20f * current_rate);
    float pwm_balance = (s_params.kp_balance * forward_error) - (s_params.kd_balance * s_rate_filtered);

    // ---------------------------------------------------------
    // 3. 速度外环 (经典 50ms / 20Hz 5分频 PI 控制器)
    // 作用：提供水平阻尼，彻底消灭“放在桌上朝一个方向猛冲”；
    // 积分项自动寻优消除静态物理重心误差，实现原地悬停！
    // ---------------------------------------------------------
    if (++s_speed_divider >= 5) {
        s_speed_divider = 0;
        // 计算左右两轮平均线速度 (编码器脉冲/10ms)
        float current_speed = (float)(speed_left + speed_right) * 0.5f - s_params.target_speed;
        // 一阶低通滤波 (70% 历史 + 30% 新采样)
        s_velocity_filtered = (0.70f * s_velocity_filtered) + (0.30f * current_speed);
        // 积分累加并抗饱和 (允许累积充足偏置以自适应完全消除机械物理重心偏差)
        s_velocity_integral += s_velocity_filtered;
        s_velocity_integral = constrain_f(s_velocity_integral, -4000.0f, 4000.0f);
        // 速度环输出
        s_velocity_out = (s_params.kp_velocity * s_velocity_filtered) + (s_params.ki_velocity * s_velocity_integral);
        // 速度环最大纠偏限幅 (限制在 ±200 PWM，杜绝压制直立环)
        s_velocity_out = constrain_f(s_velocity_out, -200.0f, 200.0f);
    }

    // ---------------------------------------------------------
    // 4. 双环合成与转向差速解算
    // 核心物理定则：速度环必须为负反馈 (pwm_balance - s_velocity_out)！
    // 向前溜车时产生刹车减速力矩，彻底消灭脱缰野马猛冲现象！
    // ---------------------------------------------------------
    float total_pwm = pwm_balance - s_velocity_out;

    float pwm_yaw = (float)target_yaw * s_params.kp_yaw;
    float left_pwm = total_pwm - pwm_yaw;
    float right_pwm = total_pwm + pwm_yaw;

    // ---------------------------------------------------------
    // 5. 柔和死区补偿 (降至 35 点，消除起步弹射猛冲)
    // ---------------------------------------------------------
    if (left_pwm > 1.5f) {
        left_pwm += (float)s_params.pwm_deadband;
    } else if (left_pwm < -1.5f) {
        left_pwm -= (float)s_params.pwm_deadband;
    }

    if (right_pwm > 1.5f) {
        right_pwm += (float)s_params.pwm_deadband;
    } else if (right_pwm < -1.5f) {
        right_pwm -= (float)s_params.pwm_deadband;
    }

    // 限幅在 [-1000, +1000] 最大占空比内
    int16_t final_pwm_left = (int16_t)constrain_f(left_pwm, -1000.0f, 1000.0f);
    int16_t final_pwm_right = (int16_t)constrain_f(right_pwm, -1000.0f, 1000.0f);

    // ---------------------------------------------------------
    // 6. 软启动渐进注入 (扶起瞬间 150ms 渐进平滑过渡)
    // ---------------------------------------------------------
    if (s_ramp_count < 15) {
        s_ramp_count++;
        float ramp_scale = (float)s_ramp_count / 15.0f;
        final_pwm_left = (int16_t)((float)final_pwm_left * ramp_scale);
        final_pwm_right = (int16_t)((float)final_pwm_right * ramp_scale);
    }

    *out_pwm_left = final_pwm_left;
    *out_pwm_right = final_pwm_right;

    return true;
}

// -------------------------------------------------------------
// 二、双轮差速速度闭环 PID 控制器与定距定角闭环走位算法
// -------------------------------------------------------------

static float s_diff_integral_l = 0.0f;
static float s_diff_integral_r = 0.0f;

static float s_step_target_dist = 0.0f;
static float s_step_target_angle = 0.0f;
static float s_step_accum_dist = 0.0f;
static float s_step_start_yaw = 0.0f;
static bool s_step_active = false;
static uint16_t s_step_settle_count = 0;

void control_diff_reset(void) {
    s_diff_integral_l = 0.0f;
    s_diff_integral_r = 0.0f;
    s_step_active = false;
    s_step_settle_count = 0;
}

void control_diff_pid_step(int16_t target_speed_mms, int16_t target_yaw_mrads,
                           int16_t speed_left, int16_t speed_right,
                           int16_t *out_pwm_left, int16_t *out_pwm_right) {
    if (target_speed_mms == 0 && target_yaw_mrads == 0) {
        s_diff_integral_l = 0.0f;
        s_diff_integral_r = 0.0f;
        *out_pwm_left = 0;
        *out_pwm_right = 0;
        return;
    }

    // 1. 差速运动学模型解算: 车轮间距 W = 90mm
    // 转向线速度差值: delta_v = yaw_rate * (W / 2) = target_yaw_mrads * 0.045 mm/s
    float delta_v = (float)target_yaw_mrads * 0.045f;
    float target_v_l = (float)target_speed_mms - delta_v;
    float target_v_r = (float)target_speed_mms + delta_v;

    // 2. 目标线速度换算为 10ms 编码器增量脉冲 (1 脉冲/10ms ≈ 8.1 mm/s)
    float target_pulse_l = target_v_l / 8.1f;
    float target_pulse_r = target_v_r / 8.1f;

    // 3. 速度误差计算
    float err_l = target_pulse_l - (float)speed_left;
    float err_r = target_pulse_r - (float)speed_right;

    // 积分项累加并限幅 (Anti-windup)
    s_diff_integral_l = constrain_f(s_diff_integral_l + err_l, -300.0f, 300.0f);
    s_diff_integral_r = constrain_f(s_diff_integral_r + err_r, -300.0f, 300.0f);

    // 4. 前馈 + PI 闭环输出
    // 前馈快速响应: 比例增益 ~12.0f + 软死区 65 点
    float ff_l = target_pulse_l * 12.0f;
    float ff_r = target_pulse_r * 12.0f;
    if (target_pulse_l > 0.1f) ff_l += 65.0f;
    else if (target_pulse_l < -0.1f) ff_l -= 65.0f;

    if (target_pulse_r > 0.1f) ff_r += 65.0f;
    else if (target_pulse_r < -0.1f) ff_r -= 65.0f;

    const float KP_DIFF = 14.0f;
    const float KI_DIFF = 1.2f;

    float pwm_l = ff_l + (KP_DIFF * err_l) + (KI_DIFF * s_diff_integral_l);
    float pwm_r = ff_r + (KP_DIFF * err_r) + (KI_DIFF * s_diff_integral_r);

    *out_pwm_left = (int16_t)constrain_f(pwm_l, -1000.0f, 1000.0f);
    *out_pwm_right = (int16_t)constrain_f(pwm_r, -1000.0f, 1000.0f);
}

void control_step_move_start(int16_t dist_mm, int16_t angle_deci_deg, float start_yaw) {
    s_step_target_dist = (float)dist_mm;
    s_step_target_angle = (float)angle_deci_deg * 0.1f; // 0.1度 -> 度
    s_step_accum_dist = 0.0f;
    s_step_start_yaw = start_yaw;
    s_step_active = true;
    s_step_settle_count = 0;
    control_diff_reset();
    s_step_active = true;
}

bool control_step_move_update(int16_t speed_left, int16_t speed_right, float current_yaw,
                              int16_t *out_pwm_left, int16_t *out_pwm_right) {
    if (!s_step_active) {
        *out_pwm_left = 0;
        *out_pwm_right = 0;
        return true;
    }

    // 1. 积分累加当前行驶位移与自转角度
    // 脉冲数换算为毫米 (1 脉冲/10ms * 8.1 mm/s * 0.01s = 0.081 mm/脉冲)
    float delta_dist = ((float)speed_left + (float)speed_right) * 0.5f * 0.081f;
    s_step_accum_dist += delta_dist;

    float delta_yaw = current_yaw - s_step_start_yaw;

    // 2. 误差解算
    float dist_err = s_step_target_dist - s_step_accum_dist;
    float yaw_err = s_step_target_angle - delta_yaw;

    // 3. 判断是否到达目标范围 (距离误差 < 4mm, 角度误差 < 2.0°)
    if (fabsf(dist_err) < 4.0f && fabsf(yaw_err) < 2.0f) {
        s_step_settle_count++;
        if (s_step_settle_count > 10) { // 保持稳定 100ms
            s_step_active = false;
            *out_pwm_left = 0;
            *out_pwm_right = 0;
            return true;
        }
    } else {
        s_step_settle_count = 0;
    }

    // 4. 生成带限幅与平滑降速的目标速度与角速度
    float cmd_v = constrain_f(dist_err * 2.5f, -160.0f, 160.0f);
    float cmd_w = constrain_f(yaw_err * 22.0f, -600.0f, 600.0f);

    control_diff_pid_step((int16_t)cmd_v, (int16_t)cmd_w, speed_left, speed_right,
                          out_pwm_left, out_pwm_right);
    return false;
}
