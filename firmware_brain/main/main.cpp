/**
 * @file main.cpp
 * @brief 机器人大脑固件唯一主入口
 *
 * 【企业级架构规范】：
 *  1. 遵循“极简入口”设计哲学，主函数不包含任何硬件底层操作与死循环业务逻辑；
 *  2. 只保留系统的启动装配和生命周期调度，真正实现高内聚、低耦合；
 *  3. 将控制权完全委托给全局调度中枢 RobotBrain。
 */
#include "core/robot_brain.hpp"
#include "esp_log.h"

static const char *TAG = "主程序";

/**
 * @brief ESP-IDF 系统级主入口函数
 * @note 使用 extern "C" 避免 C++ 符号重命名 (Name Mangling)，确保系统启动加载器正确识别
 */
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=========================================");
    ESP_LOGI(TAG, "🤖 桌面平衡机器人 · ESP32-S3 大脑固件启动");
    ESP_LOGI(TAG, "=========================================");

    // 1. 获取全局单例大脑中枢引用
    auto &brain = RobotBrain::getInstance();

    // 2. 执行系统总初始化 (硬件自检 + 各服务初始化)
    if (brain.init() == ESP_OK) {
        // 3. 成功后拉起后台各业务服务任务 (包括底盘监听透传线程)
        brain.start();
    } else {
        // 初始化失败时输出最高等级错误告警
        ESP_LOGE(TAG, "❌ 机器人大脑系统初始化失败，请检查硬件链路！");
    }

    // app_main 执行完毕后会自动退出并被 FreeRTOS 回收，
    // 之前由各服务创建的后台独立线程将继续在各自的核心上平稳运行。
}
