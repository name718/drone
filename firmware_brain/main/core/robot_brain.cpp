/**
 * @file robot_brain.cpp
 * @brief 系统调度中枢实现文件
 */
#include "core/robot_brain.hpp"

#include <string>

#include "audio/audio_service.hpp"
#include "comm/chassis_service.hpp"
#include "config/board_config.hpp"
#include "display/display_service.hpp"
#include "gimbal/gimbal_service.hpp"
#include "network/web_server.hpp"
#include "network/wifi_manager.hpp"

// ESP-IDF 硬件检测与系统库
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "大脑中枢";

RobotBrain &RobotBrain::getInstance() {
    static RobotBrain instance;
    return instance;
}

RobotBrain::RobotBrain() = default;

void RobotBrain::printSystemDiagnostics() {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    uint32_t flash_size = 0;
    esp_flash_get_size(nullptr, &flash_size);

    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "       🧠 机器人大脑系统硬件体检诊断报告         ");
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, " 芯片型号   : ESP32-S3 (版本 %d, %d 核心)", chip_info.revision, chip_info.cores);
    ESP_LOGI(TAG, " Flash 容量 : %lu MB", (unsigned long)(flash_size / (1024 * 1024)));
    ESP_LOGI(TAG, " 内部 SRAM  : %lu KB 剩余",
             (unsigned long)(esp_get_free_internal_heap_size() / 1024));
    ESP_LOGI(TAG, " 八线 PSRAM : %lu KB 剩余",
             (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    ESP_LOGI(TAG, "=================================================");
}

esp_err_t RobotBrain::init() {
    // 1. 开机体检
    printSystemDiagnostics();

    ESP_LOGI(TAG, "正在初始化各子系统服务...");

    // 2. 初始化底盘串口通信硬件 (Core 1)
    esp_err_t err = ChassisService::getInstance().init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "底盘通信服务初始化失败！");
        return err;
    }

    // 3. 初始化 Wi-Fi 协议栈底层 (Core 0)
    err = WifiManager::getInstance().init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi 管理器初始化失败！");
        return err;
    }

    // 4. 初始化视觉表情服务 (Core 1)
    DisplayService::getInstance().init();

    // 5. 初始化二自由度头部云台硬件 (Core 1)
    GimbalService::getInstance().init();

    // 6. 初始化音频声学子系统 (MAX98357A 功放与 INMP441 麦克风)
    AudioService::getInstance().init();

    ESP_LOGI(TAG, "✅ 基础服务初始化全部就绪！");
    return ESP_OK;
}

esp_err_t RobotBrain::start() {
    ESP_LOGI(TAG, "正在启动后台服务线程...");

    // 1. 启动底盘数据接收任务 (Core 1)
    ChassisService::getInstance().start();

    // 2. 创建独立的网络调度任务 (Core 0，负责等待 Wi-Fi 并拉起 MQTT)
    xTaskCreatePinnedToCore(networkTask, Config::Tasks::NETWORK_TASK_NAME,
                            Config::Tasks::NETWORK_STACK_SIZE, this,
                            Config::Tasks::NETWORK_PRIORITY,  // 优先级 6
                            nullptr,
                            Config::Tasks::NETWORK_CORE_ID  // 绑定在 Core 0
    );

    // 3. 启动视觉服务独立渲染线程 (Core 1 @ 33 FPS)
    DisplayService::getInstance().start();

    // 4. 启动头部云台 50Hz 独立控制线程 (Core 1)
    GimbalService::getInstance().start();

    // 5. 启动音频环境监测服务 (Core 0)
    AudioService::getInstance().start();

    // 6. 播放标志性开机赛博和弦哨音，标志大脑全面苏醒就绪！
    AudioService::getInstance().playBootChime();

    ESP_LOGI(TAG, "🎉 大脑系统启动完毕，全面进入运行态！");
    return ESP_OK;
}

void RobotBrain::networkTask(void *param) {
    ESP_LOGI(TAG, "后台网络监听任务已启动，正在等待 Wi-Fi 获取 IP...");

    // 阻塞等待 Wi-Fi 连接成功
    if (WifiManager::getInstance().waitForConnected(portMAX_DELAY)) {
        ESP_LOGI(TAG, "📡 Wi-Fi 链路已就绪，正在拉起 Web 控制台与 WebSocket 服务...");

        // 一键启动 Web 服务器 (端口 80)
        WebServer::getInstance().start();
    }

    // 任务使命完成，自我注销释放栈内存
    vTaskDelete(nullptr);
}
