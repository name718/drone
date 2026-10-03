/**
 * @file robot_brain.cpp
 * @brief 系统调度中枢实现文件
 *
 * 【功能说明】：
 *  实现了开机全方位硬件诊断输出，以及各子系统服务的有序启动逻辑。
 */
#include "core/robot_brain.hpp"

#include "comm/chassis_service.hpp"

// 引入 ESP-IDF 硬件检测与内存管理 API
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "大脑中枢";

/**
 * @brief 单例静态获取函数实现
 */
RobotBrain &RobotBrain::getInstance() {
    static RobotBrain instance;
    return instance;
}

RobotBrain::RobotBrain() = default;

/**
 * @brief 硬件健康体检与资源自检打印
 */
void RobotBrain::printSystemDiagnostics() {
    // 1. 获取芯片硬件架构与核心信息
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    // 2. 获取板载 SPI Flash 实际容量
    uint32_t flash_size = 0;
    esp_flash_get_size(nullptr, &flash_size);

    // 3. 规范化打印开机诊断报告
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "       🧠 机器人大脑系统硬件体检诊断报告         ");
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, " 芯片型号   : ESP32-S3 (版本 %d, %d 核心)", chip_info.revision,
             chip_info.cores);
    ESP_LOGI(TAG, " Flash 容量 : %lu MB", (unsigned long)(flash_size / (1024 * 1024)));
    ESP_LOGI(TAG, " 内部 SRAM  : %lu KB 剩余",
             (unsigned long)(esp_get_free_internal_heap_size() / 1024));
    ESP_LOGI(TAG, " 八线 PSRAM : %lu KB 剩余",
             (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    ESP_LOGI(TAG, "=================================================");
}

/**
 * @brief 系统总初始化流程
 */
esp_err_t RobotBrain::init() {
    // 第一步：开机体检并打印诊断信息
    printSystemDiagnostics();

    ESP_LOGI(TAG, "正在按依赖顺序引导各子系统服务...");

    // 第二步：初始化底盘通信服务 (拉起 GPIO 7/8 跨芯片串口硬件)
    esp_err_t err = ChassisService::getInstance().init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "底盘通信服务 (ChassisService) 初始化失败！");
        return err;
    }

    ESP_LOGI(TAG, "所有核心子系统服务已成功就绪。");
    return ESP_OK;
}

/**
 * @brief 系统启动流程
 */
esp_err_t RobotBrain::start() {
    ESP_LOGI(TAG, "正在启动后台服务任务队列...");

    // 启动底盘服务监听线程 (Core 1)
    esp_err_t err = ChassisService::getInstance().start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "启动底盘服务任务失败！");
        return err;
    }

    ESP_LOGI(TAG, "🎉 大脑系统启动完毕，全面进入运行态！");
    return ESP_OK;
}
