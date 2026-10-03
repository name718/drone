// 职责：整个大脑所有外设引脚、通信参数、任务栈大小与优先级的唯一信任源，避免参数硬编码分散。
#pragma once
#include <stdint.h>

#include "driver/uart.h"

namespace Config {

// --- 底盘串口通信配置 (跨芯片物理链路) ---
namespace ChassisCom {
constexpr uart_port_t PORT = UART_NUM_1;
constexpr int PIN_TX = 7;               // ESP32-S3 TX -> STM32 PA3 (RX)
constexpr int PIN_RX = 8;               // ESP32-S3 RX -> STM32 PA2 (TX)
constexpr uint32_t BAUD_RATE = 460800;  // 高速波特率
constexpr size_t RX_BUF_SIZE = 4096;    // 4KB 环形缓冲区，防高速溢出
}  // namespace ChassisCom

// --- FreeRTOS 任务调度参数 ---
namespace Tasks {
constexpr const char *CHASSIS_TASK_NAME = "ChassisSvc";
constexpr uint32_t CHASSIS_STACK_SIZE = 4096;
constexpr UBaseType_t CHASSIS_PRIORITY = 10;
constexpr BaseType_t CHASSIS_CORE_ID = 1;  // 绑定在 Core 1，预留 Core 0 给后续 Wi-Fi / 网络
}  // namespace Tasks

}  // namespace Config
