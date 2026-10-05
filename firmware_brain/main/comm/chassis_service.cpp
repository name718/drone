/**
 * @file chassis_service.cpp
 * @brief 底盘通信业务服务实现文件
 *
 * 【功能说明】：
 *  实现底盘服务的初始化、任务创建、跨芯片数据高速读取与流式透传。
 *  通过 std::vector 动态分配环形缓存，利用 FreeRTOS 阻塞式超时等待机制，
 *  在无数据时不占用 CPU 算力，有数据时极速处理。
 */

#include "comm/chassis_service.hpp"

#include <cstdio>
#include <vector>

#include "esp_log.h"

static const char *TAG = "底盘服务";

/**
 * @brief 单例静态获取函数实现 (C++11 保证静态局部变量线程安全)
 */
ChassisService &ChassisService::getInstance() {
    static ChassisService instance;
    return instance;
}

ChassisService::ChassisService() = default;
ChassisService::~ChassisService() = default;

/**
 * @brief 服务初始化：拉起底层串口硬件
 */
esp_err_t ChassisService::init() {
    ESP_LOGI(TAG, "正在初始化底盘服务底层通信硬件...");

    // 初始化封装好的 UartComm 底层驱动
    esp_err_t ret = uart_.init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "底层 UART 初始化失败，错误码: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "底盘通信服务硬件初始化完成。");
    return ESP_OK;
}

/**
 * @brief 服务启动：创建专职监听线程
 */
esp_err_t ChassisService::start() {
    // 防止重复启动造成任务句柄泄露
    if (is_running_) {
        ESP_LOGW(TAG, "底盘服务任务已在运行中，请勿重复调用 start()");
        return ESP_OK;
    }

    is_running_ = true;

    // 创建专属任务并绑定到指定 CPU 核心 (Core 1)
    // 优势：避免与 Core 0 上的 Wi-Fi 协议栈抢占时间片，保证串口通信高实时性
    BaseType_t res =
        xTaskCreatePinnedToCore(taskEntry,                          // 任务函数入口
                                Config::Tasks::CHASSIS_TASK_NAME,   // 任务名称 "ChassisSvc"
                                Config::Tasks::CHASSIS_STACK_SIZE,  // 栈大小 4096 字节
                                this,                               // 传递当前对象指针作为上下文
                                Config::Tasks::CHASSIS_PRIORITY,    // 优先级 10 (中高优先级)
                                &task_handle_,                      // 保存任务句柄
                                Config::Tasks::CHASSIS_CORE_ID      // 绑核运行 (Core 1)
        );

    if (res != pdPASS) {
        ESP_LOGE(TAG, "创建底盘服务任务失败！FreeRTOS 堆内存可能不足");
        is_running_ = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "底盘服务任务创建成功 (运行在 Core %d，优先级 %d)",
             Config::Tasks::CHASSIS_CORE_ID, Config::Tasks::CHASSIS_PRIORITY);
    return ESP_OK;
}

/**
 * @brief FreeRTOS 任务包装函数
 */
void ChassisService::taskEntry(void *param) {
    // 将 void* 转换回类指针，调用面向对象的私有执行函数
    static_cast<ChassisService *>(param)->runTask();
}

/**
 * @brief 核心工作循环：负责从 STM32 抓取数据并透传至控制台
 */
void ChassisService::runTask() {
    // 分配临时接收缓冲区 (4KB)，匹配底层 RingBuffer 尺寸
    std::vector<uint8_t> rx_buffer(Config::ChassisCom::RX_BUF_SIZE);

    ESP_LOGI(TAG, "跨芯片数据透传通道已激活！正在监听来自 STM32 的高速数据流...");

    while (is_running_) {
        // 以 20ms 为单次阻塞超时从底层串口读取数据
        // 机制：如果底盘正在发数据，会立即返回读取到的字节数；如果空闲，则最多让出 CPU20ms

        int bytes_read = uart_.read(rx_buffer.data(), rx_buffer.size() - 1, 20);

        if (bytes_read > 0) {
            // 累加接收到的有效数据量
            total_rx_bytes_ += bytes_read;

            // 【关键透传逻辑】：
            // 使用 fwrite + fflush 直接将原始字节流写入系统 stdout (即 USB-CDC 控制台)
            // 这种做法不破坏任何换行符与逗号分隔符，完美契合 VOFA+ FireWater 波形绘制
            fwrite(rx_buffer.data(), 1, bytes_read, stdout);
            fflush(stdout);
        }
    }

    // 如果 is_running_ 被设为 false，优雅清理自身任务
    vTaskDelete(nullptr);
}

// 引入跨芯片统一通信协议 (使用 extern "C" 告诉 C++ 编译器按 C 语言符号解析)
extern "C" {
#include "robot_protocol.h"
}

esp_err_t ChassisService::sendVelocityCommand(int16_t speed_mms, int16_t yaw_mrads) {
    // 1. 实例化标准控制帧结构体 (强制单字节紧凑对齐，无内存空洞)
    RobotCmdPacket_t packet = {};
    packet.header = PROTOCOL_FRAME_HEADER_CMD;  // 固定帧头: 0xAA
    packet.cmd_id = cmd_seq_++;                 // 帧流水号自增
    packet.target_speed = speed_mms;            // 目标线速度
    packet.target_yaw = yaw_mrads;              // 目标角速度
    packet.motion_mode = 1;                     // 运动模式: 1=自平衡使能运行

    // 2. 计算 16 位累加和校验码 (Checksum)
    // 校验范围从 header 开始，到 checksum 字段之前的所有字节
    uint16_t sum = 0;
    const auto *ptr = reinterpret_cast<const uint8_t *>(&packet);
    size_t payload_len = sizeof(RobotCmdPacket_t) - sizeof(packet.checksum);

    for (size_t i = 0; i < payload_len; i++) {
        sum += ptr[i];
    }
    packet.checksum = sum;

    // 3. 通过底层 UART1 硬件以 460800 波特率直接喷向 STM32
    int written = uart_.write(reinterpret_cast<const uint8_t *>(&packet), sizeof(packet));
    if (written != sizeof(packet)) {
        ESP_LOGE(TAG, "底盘指令发送失败或未写完整 (期望 %u, 实际 %d)", sizeof(packet), written);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "🚀 下发底盘指令 -> 速度: %d mm/s | 转向: %d mrad/s (流水号: %u)", speed_mms,
             yaw_mrads, packet.cmd_id);
    return ESP_OK;
}
