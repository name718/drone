/**
 * @file motion_service.cpp
 * @brief 机器人底盘运动控制与跨芯片通信服务实现
 */

#include "motion_service.hpp"
#include <cstring>
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "MOTION_SERVICE";

MotionService::MotionService(int tx_pin, int rx_pin, int baud_rate, uart_port_t uart_port)
    : m_uart(tx_pin, rx_pin, baud_rate, uart_port),
      m_taskHandle(nullptr),
      m_targetSpeed(0),
      m_targetYaw(0),
      m_stopMotionTimeMs(0),
      m_txCount(0),
      m_rxSuccessCount(0) {
    memset(&m_latestState, 0, sizeof(m_latestState));
}

MotionService::~MotionService() {
    if (m_taskHandle) {
        vTaskDelete(m_taskHandle);
    }
}

esp_err_t MotionService::init() {
    ESP_LOGI(TAG, "初始化 UART 通信接口...");
    return m_uart.init();
}

esp_err_t MotionService::start() {
    ESP_LOGI(TAG, "启动 50Hz 底盘运动控制任务 (运行在 CPU Core 1)...");

    // 注册控制与状态帧接收回调
    m_parser.setOnCmdPacket([this](const RobotCmdPacket_t& cmd) {
        m_rxSuccessCount++;
    });

    m_parser.setOnStatePacket([this](const RobotStatePacket_t& state) {
        m_latestState = state;
        m_rxSuccessCount++;
    });

    BaseType_t ret = xTaskCreatePinnedToCore(
        commTaskEntry,
        "motion_task",
        4096,
        this,
        6,
        &m_taskHandle,
        1 // 绑定在 Core 1
    );

    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}

void MotionService::setTargetVelocity(int16_t speed_mms, int16_t yaw_mrads, uint32_t duration_ms) {
    m_targetSpeed = speed_mms;
    m_targetYaw = yaw_mrads;

    if (duration_ms > 0) {
        uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        m_stopMotionTimeMs = now_ms + duration_ms;
    } else {
        m_stopMotionTimeMs = 0;
    }
}

void MotionService::stop() {
    setTargetVelocity(0, 0, 0);
}

void MotionService::commTaskEntry(void* pvParameters) {
    auto* self = static_cast<MotionService*>(pvParameters);
    uint8_t rx_buf[64];

    uint32_t last_stat_time_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    uint32_t stat_tx_start = 0;
    uint32_t stat_rx_start = 0;

    while (true) {
        uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);

        // 运动超时平稳自动减速保护
        if (self->m_stopMotionTimeMs > 0 && now_ms > self->m_stopMotionTimeMs) {
            self->m_targetSpeed = 0;
            self->m_targetYaw = 0;
            self->m_stopMotionTimeMs = 0;
            ESP_LOGI(TAG, "🛑 运动时间到，小车平稳减速刹车。");
        }

        // 1. 50Hz 周期构建控制帧并由 GPIO 1 (TX) 下发
        RobotCmdPacket_t cmd = ProtocolParser::buildCmdPacket(self->m_targetSpeed, self->m_targetYaw, 1);
        self->m_uart.send(&cmd, sizeof(cmd));
        self->m_txCount++;

        // 2. 非阻塞读取 GPIO 2 (RX) 接收队列
        int len = self->m_uart.read(rx_buf, sizeof(rx_buf), 0);
        if (len > 0) {
            self->m_parser.parse(rx_buf, len);
        }

        // 3. 每隔 1 秒统计输出通信状态
        if (now_ms - last_stat_time_ms >= 1000) {
            uint32_t tx_sec = self->m_txCount - stat_tx_start;
            uint32_t rx_sec = self->m_rxSuccessCount - stat_rx_start;
            ESP_LOGI(TAG, "📡 串口状态: 发送 %" PRIu32 " 帧/s | 成功接收 %" PRIu32 " 帧/s | 目标线速: %d mm/s",
                     tx_sec, rx_sec, self->m_targetSpeed);

            last_stat_time_ms = now_ms;
            stat_tx_start = self->m_txCount;
            stat_rx_start = self->m_rxSuccessCount;
        }

        // 严格 20ms 周期 (50Hz)
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
