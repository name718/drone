
#include "comm/uart_comm.hpp"

#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "UartComm";

UartComm::UartComm() = default;

UartComm::~UartComm() {
    if (initialized_) {
        uart_driver_delete(Config::ChassisCom::PORT);
    }
}

esp_err_t UartComm::init() {
    if (initialized_) {
        return ESP_OK;
    }

    // 采用结构体零初始化，安全规避 -Wmissing-field-initializers 编译报错
    uart_config_t uart_config = {};
    uart_config.baud_rate = static_cast<int>(Config::ChassisCom::BAUD_RATE);
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_DEFAULT;

    // 1. 安装驱动并配置 RingBuffer
    esp_err_t ret = uart_driver_install(Config::ChassisCom::PORT,
                                        Config::ChassisCom::RX_BUF_SIZE * 2, 0, 0, nullptr, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "安装底层 UART 驱动失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. 配置参数
    ESP_ERROR_CHECK(uart_param_config(Config::ChassisCom::PORT, &uart_config));

    // 3. 分配引脚
    ESP_ERROR_CHECK(uart_set_pin(Config::ChassisCom::PORT, Config::ChassisCom::PIN_TX,
                                 Config::ChassisCom::PIN_RX, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));

    initialized_ = true;
    ESP_LOGI(TAG, "底盘硬件串口配置就绪 [Port: %d, TX: %d, RX: %d, 波特率: %lu]",
             Config::ChassisCom::PORT, Config::ChassisCom::PIN_TX, Config::ChassisCom::PIN_RX,
             Config::ChassisCom::BAUD_RATE);
    return ESP_OK;
}

int UartComm::read(uint8_t *buffer, size_t max_len, uint32_t timeout_ms) {
    if (!initialized_ || !buffer)
        return -1;
    return uart_read_bytes(Config::ChassisCom::PORT, buffer, max_len, pdMS_TO_TICKS(timeout_ms));
}

int UartComm::write(const uint8_t *data, size_t len) {
    if (!initialized_ || !data)
        return -1;
    return uart_write_bytes(Config::ChassisCom::PORT, (const char *)data, len);
}
