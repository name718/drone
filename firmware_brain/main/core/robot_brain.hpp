/**
 * @file robot_brain.hpp
 * @brief 系统调度中枢头文件
 *
 * 【系统架构定位】：属于核心中枢层 (Core System Hub)。
 * 【核心设计模式】：Meyers 单例模式 (Singleton) + 外观模式 (Facade)。
 * 【核心职责】：
 *  1. 作为整个机器人大脑的“总司令部”，统一管理所有底层服务与硬件生命周期；
 *  2. 开机时自动执行硬件体检：自检芯片型号、主频、Flash容量、SRAM与PSRAM内存；
 *  3. 按严格的拓扑依赖顺序初始化各个业务服务 (底盘通信、未来加入的视觉/语音/网络)；
 *  4. 为 main.cpp 提供极简的入口调用，将系统级启动复杂度完全隔离在内部。
 */
#pragma once

#include "esp_err.h"

class RobotBrain {
public:
    /**
     * @brief 获取系统大脑的全局唯一单例引用 (线程安全)
     * @return RobotBrain& 引用
     */
    static RobotBrain &getInstance();

    /**
     * @brief 机器人大脑系统总初始化
     * @note 依次执行：硬件体检诊断 -> 底盘通信服务初始化 -> 其它外设初始化
     * @return esp_err_t ESP_OK 表示系统全部就绪
     */
    esp_err_t init();

    /**
     * @brief 启动所有后台子系统服务与调度线程
     * @return esp_err_t ESP_OK 表示所有服务均成功拉起
     */
    esp_err_t start();

private:
    // 构造与析构私有化，禁止外部随意实例化
    RobotBrain();
    ~RobotBrain() = default;

    // 禁用拷贝构造和赋值运算符
    RobotBrain(const RobotBrain &) = delete;
    RobotBrain &operator=(const RobotBrain &) = delete;

    /**
     * @brief 打印系统开机硬件诊断报告 (Flash / PSRAM / SRAM / 芯片信息)
     */
    void printSystemDiagnostics();
};
