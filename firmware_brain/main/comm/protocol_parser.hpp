#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>

extern "C" {
#include "robot_protocol.h"
}

/**
 * @brief 串口通信协议有限状态机解析器类
 */
class ProtocolParser {
public:
    using CmdPacketCallback = std::function<void(const RobotCmdPacket_t&)>;
    using StatePacketCallback = std::function<void(const RobotStatePacket_t&)>;

    ProtocolParser();

    /**
     * @brief 注册控制帧接收回调 (0xAA)
     */
    void setOnCmdPacket(CmdPacketCallback cb) { m_cmdCb = cb; }

    /**
     * @brief 注册状态帧接收回调 (0x55)
     */
    void setOnStatePacket(StatePacketCallback cb) { m_stateCb = cb; }

    /**
     * @brief 核心解析函数：将收到的流式字节喂入状态机
     */
    void parse(const uint8_t* data, size_t len);

    /**
     * @brief 辅助打包工具：构建一个带校验和的控制指令数据包
     * @param target_speed 目标线速度 (mm/s)
     * @param target_yaw 目标角速度 (mrad/s)
     * @param motion_mode 运行模式 (0:待机, 1:自平衡, 2:紧急停机)
     */
    static RobotCmdPacket_t buildCmdPacket(int16_t target_speed, int16_t target_yaw, uint8_t motion_mode = 1);

    /**
     * @brief 辅助打包工具：构建一个带校验和的底盘状态数据包
     */
    static RobotStatePacket_t buildStatePacket(float pitch_angle, int16_t actual_speed, uint16_t battery_mv, uint8_t status_flags = 1);

private:
    enum class State {
        WAIT_HEADER,    // 等待帧头 (0xAA 或 0x55)
        READ_PAYLOAD,   // 读取载荷数据与校验和
    };

    State   m_state;
    uint8_t m_currentHeader;
    uint8_t m_payloadBuffer[32];
    size_t  m_payloadIndex;
    size_t  m_expectedPayloadLen;

    CmdPacketCallback   m_cmdCb;
    StatePacketCallback m_stateCb;

    void processByte(uint8_t byte);
};
