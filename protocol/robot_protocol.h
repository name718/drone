#ifndef ROBOT_PROTOCOL_H
#define ROBOT_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

/**
 * @brief 帧头定义
 */
#define PROTOCOL_FRAME_HEADER_CMD 0xAA    // 大脑发往底盘
#define PROTOCOL_FRAME_HEADER_STATE 0x55  // 底盘发往大脑

/**
 * @brief 大脑 -> 底盘 控制指令帧 (Control Command, 8字节)
 */
typedef struct {
    uint8_t header;        // 0xAA 固定帧头
    uint8_t cmd_id;        // 指令流水号/计数器 (0~255)
    int16_t target_speed;  // 目标线速度 (mm/s, 前正后负)
    int16_t target_yaw;    // 目标角速度 (mrad/s, 左正右负)
    uint8_t motion_mode;   // 0:待机/刹车, 1:使能运行, 2:紧急停机
    uint16_t checksum;     // 累加和校验 (从 header 到 checksum 前所有字节之和)
} RobotCmdPacket_t;

/**
 * @brief 底盘 -> 大脑 全维度状态遥测帧 (Telemetry State, 55字节)
 *
 * 【设计说明】：
 *  采用单字节紧凑对齐，包含：
 *  1. STM32 芯片级档案：96-bit 硬件 UID、Flash 存储容量及固件占用、SRAM 内存容量及可用堆空间；
 *  2. 六轴姿态动力学全量物理量 (Pitch, Roll, Rate, Acc)；
 *  3. 双轮差速正交编码器测速与电机实际 PWM 输出；
 *  4. 动力电池电压及系统运行安全健康位。
 */
typedef struct {
    uint8_t header;          // 0x55 固定帧头
    uint8_t state_id;        // 状态包流水号/计数器 (0~255)

    // --- STM32 芯片硬件档案与存储空间 (芯片级底层参数) ---
    uint16_t flash_total_kb; // 片上 Flash ("硬盘") 总容量 (单位: KB, 硬件寄存器直读)
    uint16_t flash_used_kb;  // 固件已占 Flash 容量 (单位: KB)
    uint16_t sram_total_kb;  // 片上 SRAM 内存总容量 (单位: KB, 112KB)
    uint16_t sram_free_kb;   // FreeRTOS 实时可用堆内存 (单位: KB)
    uint32_t chip_uid[3];    // 96-Bit 硬件唯一序列号 UID (0x1FFF7590 直读)

    // --- 六轴姿态解算全量物理量 ---
    float pitch_angle;       // 车身滤波俯仰角 (单位: 度 deg)
    float roll_angle;        // 车身横滚角 (单位: 度 deg)
    float pitch_rate;        // 俯仰角速度 (单位: deg/s, 扣除零漂)
    float acc_pitch;         // 加速度计纯静态俯仰角 (单位: 度 deg)

    // --- 双轮差速动力学与电机驱动 ---
    int16_t left_speed;      // 左轮实际线速度 (单位: mm/s)
    int16_t right_speed;     // 右轮实际线速度 (单位: mm/s)
    int16_t left_pulse;      // 左轮 10ms 编码器增量脉冲 (TIM2 硬件正交计数)
    int16_t right_pulse;     // 右轮 10ms 编码器增量脉冲 (TIM1 硬件正交计数)
    int16_t left_pwm;        // 左轮当前驱动 PWM 占空比输出
    int16_t right_pwm;       // 右轮当前驱动 PWM 占空比输出

    // --- 动力电源与机体健康 ---
    uint16_t battery_mv;     // 动力电池实时电压 (单位: mV)
    uint8_t status_flags;    // 状态标志位: bit0=正常在线, bit1=跌倒报警, bit2=IMU零偏已就绪, bit3=电机运转中
    uint16_t checksum;       // 16位累加和校验 (从 header 开始至 checksum 前所有字节之和)
} RobotStatePacket_t;

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif  // ROBOT_PROTOCOL_H
