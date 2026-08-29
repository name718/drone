/**
 * @file uart_comm.cpp
 * @brief ESP32-S3 UART2 高速串口通信驱动实现
 */

#include "uart_comm.hpp"
#include "esp_log.h"

static const char* TAG = "UART_COMM";

UartComm::UartComm(int tx_pin, int rx_pin, int baud_rate, uart_port_t uart_port)
    : m_txPin(tx_pin),
      m_rxPin(rx_pin),
      m_baudRate(baud_rate),
      m_uartPort(uart_port) {
}

UartComm::~UartComm() {
    uart_driver_delete(m_uartPort);
}

esp_err_t UartComm::init() {
    ESP_LOGI(TAG, "初始化 UART%d 控制器 (TX: GPIO %d, RX: GPIO %d, 波特率: %d)...",
             m_uartPort, m_txPin, m_rxPin, m_baudRate);

    // 1. 配置 UART 基础通信参数 (零初始化避免 missing-field-initializers)
    uart_config_t uart_config = {};
    uart_config.baud_rate = m_baudRate;
    uart_config.data_bits = UART_DATA_8_BITS;          // 8 位数据位
    uart_config.parity = UART_PARITY_DISABLE;          // 无奇偶校验
    uart_config.stop_bits = UART_STOP_BITS_1;          // 1 位停止位
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;  // 无硬件流控
    uart_config.source_clk = UART_SCLK_DEFAULT;

    esp_err_t ret = uart_param_config(m_uartPort, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART 参数配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. 将 TX / RX 映射至指定 GPIO 引脚 (GPIO Matrix 矩阵映射)
    ret = uart_set_pin(m_uartPort, m_txPin, m_rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART 引脚映射失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 3. 安装驱动并开辟 512 字节中断接收环形队列 (RX RingBuffer)
    const int RX_BUF_SIZE = 512;
    ret = uart_driver_install(m_uartPort, RX_BUF_SIZE, 0, 0, nullptr, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART 驱动安装失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "✅ UART%d 通信驱动初始化成功！", m_uartPort);
    return ESP_OK;
}

int UartComm::send(const void* data, size_t len) {
    if (!data || len == 0) return 0;
    return uart_write_bytes(m_uartPort, data, len);
}

int UartComm::read(uint8_t* out_buf, size_t max_len, uint32_t timeout_ms) {
    if (!out_buf || max_len == 0) return 0;
    return uart_read_bytes(m_uartPort, out_buf, max_len, pdMS_TO_TICKS(timeout_ms));
}
