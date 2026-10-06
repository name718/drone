/**
 * @file wifi_manager.hpp
 * @brief Wi-Fi 网络连接管理器头文件
 *
 * 【职责】：
 *  1. 单例模式纳管整个系统的 Wi-Fi 生命周期；
 *  2. 封装 ESP-IDF 的 NVS、Netif 和 Wi-Fi 硬件事件驱动；
 *  3. 提供断线自动重连机制与连接状态查询。
 */
#pragma once

#include <atomic>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

class WifiManager {
public:
    /**
     * @brief 获取全局单例引用
     */
    static WifiManager &getInstance();

    /**
     * @brief 初始化 NVS 闪存、网络接口并拉起 Wi-Fi Station 连接
     * @return esp_err_t ESP_OK 表示初始化并启动成功
     */
    esp_err_t init();

    /**
     * @brief 查询当前是否已成功连上 Wi-Fi 并获取到 IP
     */
    bool isConnected() const { return is_connected_.load(); }

    /**
     * @brief 阻塞等待 Wi-Fi 连接成功
     * @param timeout_ticks 超时时间 (默认最大等待)
     * @return true 连接成功，false 超时
     */
    bool waitForConnected(TickType_t timeout_ticks = portMAX_DELAY);

private:
    WifiManager();
    ~WifiManager() = default;

    WifiManager(const WifiManager &) = delete;
    WifiManager &operator=(const WifiManager &) = delete;

    /**
     * @brief ESP-IDF 系统底层 Wi-Fi / IP 事件统一回调
     */
    static void eventHandler(void *arg, esp_event_base_t event_base, int32_t event_id,
                             void *event_data);

    esp_netif_t *sta_netif_{nullptr};
    std::atomic<bool> is_connected_{false};
    EventGroupHandle_t wifi_event_group_{nullptr};
    int retry_count_{0};
};
