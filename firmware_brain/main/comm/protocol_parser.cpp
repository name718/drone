/**
 * @file protocol_parser.cpp
 * @brief 串口通信协议有限状态机解析器实现
 */

#include "protocol_parser.hpp"

#include <cstring>

static uint8_t s_cmdSeq = 0;
static uint8_t s_stateSeq = 0;

ProtocolParser::ProtocolParser()
    : m_state(State::WAIT_HEADER), m_currentHeader(0), m_payloadIndex(0), m_expectedPayloadLen(0) {
    memset(m_payloadBuffer, 0, sizeof(m_payloadBuffer));
}

RobotCmdPacket_t ProtocolParser::buildCmdPacket(int16_t target_speed, int16_t target_yaw,
                                                uint8_t motion_mode) {
    RobotCmdPacket_t packet;
    packet.header = PROTOCOL_FRAME_HEADER_CMD;  // 0xAA
    packet.cmd_id = s_cmdSeq++;
    packet.target_speed = target_speed;
    packet.target_yaw = target_yaw;
    packet.motion_mode = motion_mode;

    // 计算 16-bit 累加和校验 (从 cmd_id 开始累加到 motion_mode)
    uint16_t sum = packet.header;
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&packet) + 1;
    size_t len_without_chk = sizeof(RobotCmdPacket_t) - sizeof(uint16_t) - 1;
    for (size_t i = 0; i < len_without_chk; i++) {
        sum += p[i];
    }
    packet.checksum = sum;
    return packet;
}

RobotStatePacket_t ProtocolParser::buildStatePacket(float pitch_angle, int16_t actual_speed,
                                                    uint16_t battery_mv, uint8_t status_flags) {
    RobotStatePacket_t packet;
    packet.header = PROTOCOL_FRAME_HEADER_STATE;  // 0x55
    packet.state_id = s_stateSeq++;
    packet.pitch_angle = pitch_angle;
    packet.actual_speed = actual_speed;
    packet.battery_mv = battery_mv;
    packet.status_flags = status_flags;

    // 计算 16-bit 累加和校验
    uint16_t sum = packet.header;
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&packet) + 1;
    size_t len_without_chk = sizeof(RobotStatePacket_t) - sizeof(uint16_t) - 1;
    for (size_t i = 0; i < len_without_chk; i++) {
        sum += p[i];
    }
    packet.checksum = sum;
    return packet;
}

void ProtocolParser::parse(const uint8_t *data, size_t len) {
    if (!data || len == 0)
        return;

    for (size_t i = 0; i < len; i++) {
        processByte(data[i]);
    }
}

void ProtocolParser::processByte(uint8_t byte) {
    switch (m_state) {
        case State::WAIT_HEADER:
            if (byte == PROTOCOL_FRAME_HEADER_CMD) {
                m_currentHeader = byte;
                // 控制帧除去 header (1字节) 后的总长度
                m_expectedPayloadLen = sizeof(RobotCmdPacket_t) - 1;
                m_payloadIndex = 0;
                m_state = State::READ_PAYLOAD;
            } else if (byte == PROTOCOL_FRAME_HEADER_STATE) {
                m_currentHeader = byte;
                // 状态帧除去 header (1字节) 后的总长度
                m_expectedPayloadLen = sizeof(RobotStatePacket_t) - 1;
                m_payloadIndex = 0;
                m_state = State::READ_PAYLOAD;
            }
            break;

        case State::READ_PAYLOAD:
            m_payloadBuffer[m_payloadIndex++] = byte;
            if (m_payloadIndex >= m_expectedPayloadLen) {
                // 收满一包数据，开始校验
                if (m_currentHeader == PROTOCOL_FRAME_HEADER_CMD) {
                    RobotCmdPacket_t cmd;
                    cmd.header = m_currentHeader;
                    memcpy(reinterpret_cast<uint8_t *>(&cmd) + 1, m_payloadBuffer,
                           m_expectedPayloadLen);

                    // 计算累加和校验
                    uint16_t sum = cmd.header;
                    const uint8_t *p = reinterpret_cast<const uint8_t *>(&cmd) + 1;
                    size_t len_to_check = sizeof(RobotCmdPacket_t) - sizeof(uint16_t) - 1;
                    for (size_t i = 0; i < len_to_check; i++) {
                        sum += p[i];
                    }

                    if (sum == cmd.checksum && m_cmdCb) {
                        m_cmdCb(cmd);  // 校验成功，触发回调！
                    }
                } else if (m_currentHeader == PROTOCOL_FRAME_HEADER_STATE) {
                    RobotStatePacket_t state;
                    state.header = m_currentHeader;
                    memcpy(reinterpret_cast<uint8_t *>(&state) + 1, m_payloadBuffer,
                           m_expectedPayloadLen);

                    // 计算累加和校验
                    uint16_t sum = state.header;
                    const uint8_t *p = reinterpret_cast<const uint8_t *>(&state) + 1;
                    size_t len_to_check = sizeof(RobotStatePacket_t) - sizeof(uint16_t) - 1;
                    for (size_t i = 0; i < len_to_check; i++) {
                        sum += p[i];
                    }

                    if (sum == state.checksum && m_stateCb) {
                        m_stateCb(state);  // 校验成功，触发回调！
                    }
                }

                // 状态机自动复位，等待下一包帧头 (自愈能力)
                m_state = State::WAIT_HEADER;
            }
            break;
    }
}
