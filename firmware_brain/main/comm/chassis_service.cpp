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
#include <cstring>
#include <vector>

#include "esp_log.h"
#include "esp_timer.h"
extern "C" {
#include "robot_protocol.h"
}

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
 * @brief 判断底盘通信链路是否处于活跃在线状态 (1000ms 超时心跳检测)
 */
bool ChassisService::isChassisOnline() const {
    uint64_t last = last_state_packet_ms_.load();
    if (last == 0) {
        return false;
    }
    uint64_t now_ms = esp_timer_get_time() / 1000ULL;
    return (now_ms - last) < 1000;
}

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
 * @brief 核心工作循环：负责从 STM32 抓取数据并解析遥测帧
 */
void ChassisService::runTask() {
    // 分配临时接收缓冲区 (1KB)
    std::vector<uint8_t> rx_buffer(1024);
    // 帧重组滑动窗口缓冲区
    std::vector<uint8_t> frame_buf;
    frame_buf.reserve(64);

    ESP_LOGI(TAG, "跨芯片遥测解析通道已激活！正在监听 STM32 状态遥测帧 (0x55)...");

    while (is_running_) {
        // 以 20ms 为单次阻塞超时从底层串口读取数据
        int bytes_read = uart_.read(rx_buffer.data(), rx_buffer.size(), 20);

        if (bytes_read > 0) {
            total_rx_bytes_ += bytes_read;

            // 遍历接收到的每一个字节，进入滑动窗口协议解析器
            for (int i = 0; i < bytes_read; i++) {
                uint8_t b = rx_buffer[i];

                if (frame_buf.empty()) {
                    // 等待帧头 0x55
                    if (b == PROTOCOL_FRAME_HEADER_STATE) {
                        frame_buf.push_back(b);
                    }
                } else {
                    frame_buf.push_back(b);

                    // 检查是否已达到完整帧长度
                    if (frame_buf.size() == sizeof(RobotStatePacket_t)) {
                        RobotStatePacket_t pkt;
                        std::memcpy(&pkt, frame_buf.data(), sizeof(RobotStatePacket_t));

                        // 验证累加和校验码 (Checksum)
                        uint16_t sum = 0;
                        size_t payload_len = sizeof(RobotStatePacket_t) - sizeof(uint16_t);
                        for (size_t k = 0; k < payload_len; k++) {
                            sum += frame_buf[k];
                        }

                        if (sum == pkt.checksum) {
                            // 校验成功，原子写入最新 STM32 芯片档案与全维度底盘遥测数据
                            flash_total_kb_.store(pkt.flash_total_kb);
                            flash_used_kb_.store(pkt.flash_used_kb);
                            sram_total_kb_.store(pkt.sram_total_kb);
                            sram_free_kb_.store(pkt.sram_free_kb);
                            chip_uid_[0].store(pkt.chip_uid[0]);
                            chip_uid_[1].store(pkt.chip_uid[1]);
                            chip_uid_[2].store(pkt.chip_uid[2]);

                            latest_pitch_.store(pkt.pitch_angle);
                            latest_roll_.store(pkt.roll_angle);
                            latest_pitch_rate_.store(pkt.pitch_rate);
                            latest_acc_pitch_.store(pkt.acc_pitch);
                            latest_left_speed_.store(pkt.left_speed);
                            latest_right_speed_.store(pkt.right_speed);
                            latest_left_pulse_.store(pkt.left_pulse);
                            latest_right_pulse_.store(pkt.right_pulse);
                            latest_left_pwm_.store(pkt.left_pwm);
                            latest_right_pwm_.store(pkt.right_pwm);
                            latest_battery_mv_.store(pkt.battery_mv);
                            latest_status_flags_.store(pkt.status_flags);
                            last_state_packet_ms_.store(esp_timer_get_time() / 1000ULL);
                            total_rx_packets_++;

                            frame_buf.clear();
                        } else {
                            // 校验失败：滑动窗口丢弃第一个字节，寻找下一个帧头
                            frame_buf.erase(frame_buf.begin());
                            while (!frame_buf.empty() && frame_buf[0] != PROTOCOL_FRAME_HEADER_STATE) {
                                frame_buf.erase(frame_buf.begin());
                            }
                        }
                    }
                }
            }
        }
    }

    // 如果 is_running_ 被设为 false，优雅清理自身任务
    vTaskDelete(nullptr);
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

    ESP_LOGI(TAG, "下发底盘指令 -> 速度: %d mm/s | 转向: %d mrad/s (流水号: %u)", speed_mms,
             yaw_mrads, packet.cmd_id);
    return ESP_OK;
}
