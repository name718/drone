// 封装 ESP-IDF 的底层 C 语言 UART 驱动，提供安全、面向对象的字节读写接口与状态反馈。

#pragma once
#include <cstddef>
#include <cstdint>

#include "config/board_config.hpp"
#include "esp_err.h"

class UartComm {
public:
    UartComm();
    ~UartComm();

    // 禁用拷贝与赋值 (独占硬件资源)
    UartComm(const UartComm &) = delete;
    UartComm &operator=(const UartComm &) = delete;

    esp_err_t init();
    int read(uint8_t *buffer, size_t max_len, uint32_t timeout_ms);
    int write(const uint8_t *data, size_t len);

private:
    bool initialized_{false};
};
