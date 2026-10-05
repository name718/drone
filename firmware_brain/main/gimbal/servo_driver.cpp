/**
 * @file servo_driver.cpp
 * @brief ESP32-S3 硬件 LEDC PWM 舵机底层驱动实现
 */
#include "gimbal/servo_driver.hpp"

#include <algorithm>
#include "config/board_config.hpp"
#include "esp_log.h"

static const char *TAG = "舵机驱动";

ServoDriver::ServoDriver() = default;

ServoDriver::~ServoDriver() {
    if (is_initialized_) {
        ledc_stop(speed_mode_, channel_pan_, 0);
        ledc_stop(speed_mode_, channel_tilt_, 0);
    }
}

uint32_t ServoDriver::angleToDuty(float angle) {
    // 1. 将角度安全截断在 [0.0, 180.0] 物理安全区间，严防堵转打齿
    float clamped_angle = std::clamp(angle, 0.0f, 180.0f);

    // 2. 线性插值映射计算目标脉冲宽度 (微秒 us)
    // 0度 -> 500us, 180度 -> 2500us
    float pulse_us = Config::Gimbal::MIN_PULSE_US +
                     (clamped_angle / 180.0f) * (Config::Gimbal::MAX_PULSE_US - Config::Gimbal::MIN_PULSE_US);

    // 3. 将微秒脉宽换算为 14-Bit (16384级分辨率) 的硬件寄存器数值
    // 20ms = 20000us -> 对应满量程 16384
    uint32_t duty = static_cast<uint32_t>((static_cast<uint64_t>(pulse_us) * 16384ULL) / 20000ULL);
    return duty;
}

esp_err_t ServoDriver::init(int pin_pan, int pin_tilt) {
    ESP_LOGI(TAG, "正在初始化云台硬件 LEDC 定时器 (PWM 50Hz, 14-Bit)...");

    // 1. 配置硬件定时器 Timer 0
    // 【语法注意】：先用 {} 零初始化结构体，避免未初始化字段触发编译器警告
    ledc_timer_config_t timer_conf = {};
    timer_conf.speed_mode = speed_mode_;
    timer_conf.duty_resolution = LEDC_TIMER_14_BIT;  // 14位高精度
    timer_conf.timer_num = LEDC_TIMER_0;
    timer_conf.freq_hz = Config::Gimbal::PWM_FREQ_HZ;  // 50Hz
    timer_conf.clk_cfg = LEDC_AUTO_CLK;

    esp_err_t err = ledc_timer_config(&timer_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "配置 LEDC 定时器失败: %s", esp_err_to_name(err));
        return err;
    }

    // 2. 配置水平偏航舵机通道 (Channel 0 -> pin_pan)
    ledc_channel_config_t pan_conf = {};
    pan_conf.gpio_num = pin_pan;
    pan_conf.speed_mode = speed_mode_;
    pan_conf.channel = channel_pan_;
    pan_conf.timer_sel = LEDC_TIMER_0;
    pan_conf.duty = angleToDuty(Config::Gimbal::DEFAULT_PAN_ANGLE);  // 开机默认 90 度回正
    pan_conf.hpoint = 0;

    err = ledc_channel_config(&pan_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "配置水平舵机通道失败: %s", esp_err_to_name(err));
        return err;
    }

    // 3. 配置俯仰舵机通道 (Channel 1 -> pin_tilt)
    ledc_channel_config_t tilt_conf = {};
    tilt_conf.gpio_num = pin_tilt;
    tilt_conf.speed_mode = speed_mode_;
    tilt_conf.channel = channel_tilt_;
    tilt_conf.timer_sel = LEDC_TIMER_0;
    tilt_conf.duty = angleToDuty(Config::Gimbal::DEFAULT_TILT_ANGLE);  // 开机默认 90 度回正
    tilt_conf.hpoint = 0;

    err = ledc_channel_config(&tilt_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "配置俯仰舵机通道失败: %s", esp_err_to_name(err));
        return err;
    }

    is_initialized_ = true;
    ESP_LOGI(TAG, "云台舵机硬件初始化完成 (Pan=GPIO%d, Tilt=GPIO%d, 均归位90°)", pin_pan, pin_tilt);
    return ESP_OK;
}

void ServoDriver::setPanAngle(float angle) {
    if (!is_initialized_) return;
    uint32_t duty = angleToDuty(angle);
    ledc_set_duty(speed_mode_, channel_pan_, duty);
    ledc_update_duty(speed_mode_, channel_pan_);
}

void ServoDriver::setTiltAngle(float angle) {
    if (!is_initialized_) return;
    uint32_t duty = angleToDuty(angle);
    ledc_set_duty(speed_mode_, channel_tilt_, duty);
    ledc_update_duty(speed_mode_, channel_tilt_);
}
