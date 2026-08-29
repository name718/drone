#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/uart.h"
#include "esp_err.h"

/**
 * @brief ESP32-S3 UART2 高速串口通信驱动类 (带 DMA / 环形接收队列)
 */
class UartComm {
public:
    /**
     * @brief 构造函数：指定串口端口、引脚及波特率
     * @param tx_pin 发送引脚 TX (默认 GPIO 1)
     * @param rx_pin 接收引脚 RX (默认 GPIO 2)
     * @param baud_rate 波特率 (默认 115200)
     * @param uart_port UART 硬件控制器编号 (默认 UART_NUM_2)
     */
    UartComm(int tx_pin = 1, int rx_pin = 2, int baud_rate = 115200,
             uart_port_t uart_port = UART_NUM_2);
    ~UartComm();

    /**
     * @brief 初始化 UART 硬件并分配 512 字节中断接收环形队列
     */
    esp_err_t init();

    /**
     * @brief 发送原始字节流
     */
    int send(const void *data, size_t len);

    /**
     * @brief 从串口环形队列中非阻塞读取数据
     */
    int read(uint8_t *out_buf, size_t max_len, uint32_t timeout_ms = 10);

private:
    int m_txPin;
    int m_rxPin;
    int m_baudRate;
    uart_port_t m_uartPort;
};
