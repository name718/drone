/**
 * @file chassis_service.hpp
 * @brief 底盘通信业务服务头文件
 *
 * 【系统架构定位】：属于业务服务层 (Service Layer)。
 * 【核心设计模式】：Meyers 单例模式 (Singleton)。
 * 【核心职责】：
 *  1. 全局单例纳管底盘通信生命周期，防止多线程竞争或重复打开底层串口；
 *  2. 托管独立的 FreeRTOS 任务，绑定在指定 CPU 核心 (Core 1) 上运行；
 *  3. 提供底层串口数据透传给电脑控制台 (VOFA+/串口监视器) 的能力；
 *  4. 维护通信链路健康度统计 (累计接收字节数、错误统计等)；
 *  5. 后续平滑扩展二进制协议解析 (RobotStatePacket) 与指令下发队列。
 */
#pragma once

#include <atomic>

#include "comm/uart_comm.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class ChassisService {
public:
    /**
     * @brief 获取底盘服务的全局唯一单例对象 (线程安全)
     * @return ChassisService& 引用
     */
    static ChassisService &getInstance();

    /**
     * @brief 初始化底盘服务所依赖的底层硬件 (调用 UartComm::init)
     * @return esp_err_t ESP_OK 表示初始化成功
     */
    esp_err_t init();

    /**
     * @brief 启动底盘数据监听与透传任务
     * @note 会创建专属的 FreeRTOS 任务并绑定到 Core 1
     * @return esp_err_t ESP_OK 表示任务启动成功
     */
    esp_err_t start();

    /**
     * @brief 获取从 STM32 底盘累计接收到的总有效字节数
     * @return uint64_t 字节数 (原子读取，多线程安全)
     */
    uint64_t getTotalRxBytes() const { return total_rx_bytes_.load(); }

private:
    // 构造与析构私有化，确保单例唯一性
    ChassisService();
    ~ChassisService();

    // 禁用拷贝构造和赋值运算符，防止对象被意外克隆
    ChassisService(const ChassisService &) = delete;
    ChassisService &operator=(const ChassisService &) = delete;

    /**
     * @brief FreeRTOS 任务的静态适配器入口函数
     * @param param 传入的 this 指针
     */
    static void taskEntry(void *param);

    /**
     * @brief 实际在 FreeRTOS 线程内部死循环运行的任务主体
     */
    void runTask();

    // --- 成员变量 ---
    UartComm uart_;                            // 底层串口驱动对象
    TaskHandle_t task_handle_{nullptr};        // FreeRTOS 任务句柄
    std::atomic<bool> is_running_{false};      // 任务运行状态标志位
    std::atomic<uint64_t> total_rx_bytes_{0};  // 累计接收字节计数器
};
