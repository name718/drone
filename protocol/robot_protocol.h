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
 * @brief 大脑 -> 底盘 控制指令帧 (Control Command)
 */
typedef struct {
    uint8_t header;        // 0xAA
    uint8_t cmd_id;        // 指令流水号/计数器
    int16_t target_speed;  // 目标线速度 (mm/s, 前正后负)
    int16_t target_yaw;    // 目标角速度 (mrad/s, 左正右负)
    uint8_t motion_mode;   // 0:待机/锁死, 1:自平衡, 2:紧急停机
    uint16_t checksum;     // 累加和校验
} RobotCmdPacket_t;
/**
 * @brief 底盘 -> 大脑 状态遥测帧 (Telemetry State)
 */
typedef struct {
    uint8_t header;        // 0x55
    uint8_t state_id;      // 状态包计数器
    float pitch_angle;     // 俯仰角 (度)
    int16_t actual_speed;  // 实际测得车速 (mm/s)
    uint16_t battery_mv;   // 电池实时电压 (mV)
    uint8_t status_flags;  // 状态标志位: bit0=平衡正常,bit1=跌倒报警, bit2=低电量
    uint16_t checksum;     // 累加和校验
} RobotStatePacket_t;
#pragma pack(pop)

#ifdef __cplusplus
}

#endif

#endif  // ROBOT_PROTOCOL_H
