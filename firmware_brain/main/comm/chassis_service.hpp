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

    /**
     * @brief 向 STM32 底盘下发线速度与角速度指令
     * @param speed_mms 目标线速度 (单位: mm/s，前正后负，例如 200 表示前进，-200 表示后退)
     * @param yaw_mrads 目标角速度 (单位: mrad/s，左正右负，例如 500 表示左转)
     * @return esp_err_t ESP_OK 表示发送成功
     */
    esp_err_t sendVelocityCommand(int16_t speed_mms, int16_t yaw_mrads);

    /**
     * @brief 获取底盘实时姿态角与动力学指标 (全维度)
     */
    float getPitch() const { return latest_pitch_.load(); }
    float getRoll() const { return latest_roll_.load(); }
    float getPitchRate() const { return latest_pitch_rate_.load(); }
    float getAccPitch() const { return latest_acc_pitch_.load(); }
    int16_t getLeftSpeed() const { return latest_left_speed_.load(); }
    int16_t getRightSpeed() const { return latest_right_speed_.load(); }
    int16_t getActualSpeed() const { return (latest_left_speed_.load() + latest_right_speed_.load()) / 2; }
    int16_t getLeftPulse() const { return latest_left_pulse_.load(); }
    int16_t getRightPulse() const { return latest_right_pulse_.load(); }
    int16_t getLeftPwm() const { return latest_left_pwm_.load(); }
    int16_t getRightPwm() const { return latest_right_pwm_.load(); }
    uint16_t getBatteryMv() const { return latest_battery_mv_.load(); }
    uint8_t getStatusFlags() const { return latest_status_flags_.load(); }
    uint32_t getRxPackets() const { return total_rx_packets_.load(); }

    /**
     * @brief 获取 STM32 芯片级硬件存储与身份信息 (Flash/SRAM/UID)
     */
    uint16_t getFlashTotalKb() const { return flash_total_kb_.load(); }
    uint16_t getFlashUsedKb() const { return flash_used_kb_.load(); }
    uint16_t getSramTotalKb() const { return sram_total_kb_.load(); }
    uint16_t getSramFreeKb() const { return sram_free_kb_.load(); }
    void getChipUid(uint32_t uid[3]) const {
        uid[0] = chip_uid_[0].load();
        uid[1] = chip_uid_[1].load();
        uid[2] = chip_uid_[2].load();
    }

    /**
     * @brief 检查底盘通信链路是否处于活跃在线状态
     */
    bool isChassisOnline() const;

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
    std::atomic<uint32_t> total_rx_packets_{0}; // 累计接收完整包计数器

    uint8_t cmd_seq_{0};  // 指令帧流水号 (0~255 循环递增)

    // STM32 芯片级档案状态量 (原子多线程安全)
    std::atomic<uint16_t> flash_total_kb_{256};
    std::atomic<uint16_t> flash_used_kb_{32};
    std::atomic<uint16_t> sram_total_kb_{112};
    std::atomic<uint16_t> sram_free_kb_{60};
    std::atomic<uint32_t> chip_uid_[3]{{0}, {0}, {0}};

    // 底盘遥测全维度状态量 (原子多线程安全)
    std::atomic<float> latest_pitch_{0.0f};
    std::atomic<float> latest_roll_{0.0f};
    std::atomic<float> latest_pitch_rate_{0.0f};
    std::atomic<float> latest_acc_pitch_{0.0f};
    std::atomic<int16_t> latest_left_speed_{0};
    std::atomic<int16_t> latest_right_speed_{0};
    std::atomic<int16_t> latest_left_pulse_{0};
    std::atomic<int16_t> latest_right_pulse_{0};
    std::atomic<int16_t> latest_left_pwm_{0};
    std::atomic<int16_t> latest_right_pwm_{0};
    std::atomic<uint16_t> latest_battery_mv_{12100};
    std::atomic<uint8_t> latest_status_flags_{0};
    std::atomic<uint64_t> last_state_packet_ms_{0};
};
