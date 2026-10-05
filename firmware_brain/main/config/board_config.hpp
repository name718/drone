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

constexpr const char *NETWORK_TASK_NAME = "NetworkTask";
constexpr uint32_t NETWORK_STACK_SIZE = 4096;
constexpr UBaseType_t NETWORK_PRIORITY = 6;
constexpr BaseType_t NETWORK_CORE_ID = 0;   // 绑定在 Core 0
}  // namespace Tasks

// ============================================================================
// 6. 网络 Wi-Fi 通信配置
// ============================================================================
namespace Network {
// 你的 Wi-Fi 名称与密码 (支持 2.4GHz Wi-Fi)
constexpr const char *WIFI_SSID = "CMCC-eGK3";  // 你的 Wi-Fi 名字
constexpr const char *WIFI_PASS = "vZwGHMMu";   // 你的 Wi-Fi 密码
constexpr int WIFI_MAX_RETRY = 5;               // 最大重试次数
}  // namespace Network

}  // namespace Config
