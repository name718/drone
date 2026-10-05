/**
 * @file servo_driver.hpp
 * @brief ESP32-S3 硬件 LEDC PWM 舵机底层驱动头文件
 *
 * 【硬件工作原理与技术指标】：
 *  1. SG90 属于 50Hz 模拟舵机，周期 T = 20ms (20000us)；
 *  2. 高电平脉宽控制角度：
 *     - 0.5ms (500us)   -> 0 度极限位置
 *     - 1.5ms (1500us)  -> 90 度居中回正位置
 *     - 2.5ms (2500us)  -> 180 度极限位置
 *  3. 我们采用 ESP32-S3 内部硬件 LEDC 定时器，分辨率设为 14-Bit (0 ~ 16383)：
 *     - 20ms 对应 16384 个计数脉冲；
 *     - 每度微调精细度极高，硬件生成 PWM 占空比，绝不占用 CPU 运算时间。
 */
#pragma once

#include <cstdint>
#include "esp_err.h"
#include "driver/ledc.h"

class ServoDriver {
public:
    ServoDriver();
    ~ServoDriver();

    /**
     * @brief 初始化硬件 LEDC 定时器与两个舵机 PWM 输出通道
     * @param pin_pan 水平舵机 (Pan) 绑定的 GPIO 引脚号
     * @param pin_tilt 俯仰舵机 (Tilt) 绑定的 GPIO 引脚号
     * @return esp_err_t ESP_OK 表示初始化成功
     */
    esp_err_t init(int pin_pan, int pin_tilt);

    /**
     * @brief 写入水平偏航舵机角度
     * @param angle 0.0f ~ 180.0f 度
     */
    void setPanAngle(float angle);

    /**
     * @brief 写入垂直俯仰舵机角度
     * @param angle 0.0f ~ 180.0f 度
     */
    void setTiltAngle(float angle);

private:
    /**
     * @brief 将目标角度 (0~180度) 换算为 LEDC 硬件 14-bit 占空比数值
     * @param angle 角度值
     * @return uint32_t 14 位占空比寄存器值
     */
    uint32_t angleToDuty(float angle);

    ledc_channel_t channel_pan_{LEDC_CHANNEL_0};   // 水平通道
    ledc_channel_t channel_tilt_{LEDC_CHANNEL_1};  // 俯仰通道
    ledc_mode_t speed_mode_{LEDC_LOW_SPEED_MODE};  // S3 芯片硬件低速模式
    bool is_initialized_{false};
};
