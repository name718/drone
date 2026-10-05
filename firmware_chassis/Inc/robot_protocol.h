/**
 * @file robot_protocol.h
 * @brief 跨芯片统一二进制通信协议 (STM32 下位机底盘 <-> ESP32-S3 上位机大脑)
 *
 * 【帧格式规范】：
 *  1. 控制帧 (大脑 -> 底盘, 0xAA, 9 字节)
 *  2. 遥测帧 (底盘 -> 大脑, 0x55, 13 字节)
 */
#ifndef ROBOT_PROTOCOL_H
#define ROBOT_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

/**
 * @brief 协议固定帧头魔数定义
 */
#define PROTOCOL_FRAME_HEADER_CMD   0xAA  // 大脑发往底盘
#define PROTOCOL_FRAME_HEADER_STATE 0x55  // 底盘发往大脑

/**
 * @brief 大脑 -> 底盘 控制指令帧 (Control Command, 共 9 字节)
 */
typedef struct {
    uint8_t header;        // 固定帧头: 0xAA
    uint8_t cmd_id;        // 指令流水号 (0~255)
    int16_t target_speed;  // 目标线速度 (mm/s, 前正后负，例如 +200)
    int16_t target_yaw;    // 目标角速度 (mrad/s, 左正右负，例如 +500 为左转)
    uint8_t motion_mode;   // 运动模式: 0=待机停机, 1=使能运行, 2=紧急停机
    uint16_t checksum;     // 16位累加和校验码
} RobotCmdPacket_t;

/**
 * @brief 底盘 -> 大脑 状态遥测帧 (Telemetry State, 共 13 字节)
 */
typedef struct {
    uint8_t header;        // 固定帧头: 0x55
    uint8_t state_id;      // 状态包流水号 (0~255)
    float pitch_angle;     // 实时俯仰角 (单位: 度)
    int16_t actual_speed;  // 实际测得车速 (单位: mm/s)
    uint16_t battery_mv;   // 电池实时电压 (单位: mV)
    uint8_t status_flags;  // 状态标志位: bit0=平衡正常, bit1=跌倒报警, bit2=低电量
    uint16_t checksum;     // 16位累加和校验码
} RobotStatePacket_t;

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif  // ROBOT_PROTOCOL_H
