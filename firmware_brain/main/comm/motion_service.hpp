/**
 * @file motion_service.hpp
 * @brief 机器人底盘运动控制与跨芯片通信服务
 */

#pragma once

#include <cstdint>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "uart_comm.hpp"
#include "protocol_parser.hpp"

class MotionService {
public:
    MotionService(int tx_pin = 1, int rx_pin = 2, int baud_rate = 115200, uart_port_t uart_port = UART_NUM_2);
    ~MotionService();

    /**
     * @brief 初始化 UART2 硬件与协议解析器
     */
    esp_err_t init();

    /**
     * @brief 启动 50Hz 周期控制下发与状态监听任务 (Core 1)
     */
    esp_err_t start();

    /**
     * @brief 设定机器人目标运动速度 (线程安全)
     * @param speed_mms 目标线速度 (mm/s, 前正后负)
     * @param yaw_mrads 目标角速度 (mrad/s, 左正右负)
     * @param duration_ms 持续时间 (毫秒, 达到后自动平稳刹车归零; 0 表示持续运动)
     */
    void setTargetVelocity(int16_t speed_mms, int16_t yaw_mrads, uint32_t duration_ms = 0);

    /**
     * @brief 紧急制动停车
     */
    void stop();

    /**
     * @brief 获取最新从底盘接收到的状态帧数据
     */
    RobotStatePacket_t getLatestState() const { return m_latestState; }

    /**
     * @brief 获取当前通信统计数据
     */
    uint32_t getTxCount() const { return m_txCount; }
    uint32_t getRxCount() const { return m_rxSuccessCount; }

private:
    UartComm        m_uart;
    ProtocolParser  m_parser;
    TaskHandle_t    m_taskHandle;

    int16_t         m_targetSpeed;
    int16_t         m_targetYaw;
    uint32_t        m_stopMotionTimeMs;

    RobotStatePacket_t m_latestState;
    uint32_t        m_txCount;
    uint32_t        m_rxSuccessCount;

    static void commTaskEntry(void* pvParameters);
};
