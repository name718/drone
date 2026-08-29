/**
 * @file wifi_manager.hpp
 * @brief ESP32-S3 Wi-Fi Station 模式连接与自动重连管理服务 (嵌入式轻量零动态内存版)
 */

#pragma once

#include <cstdint>
#include <functional>
#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

class WifiManager {
public:
    // 回调函数定义：连接状态变动 (is_connected, ip_str)
    using ConnectCallback = std::function<void(bool is_connected, const char* ip)>;

    WifiManager();
    ~WifiManager();

    /**
     * @brief 初始化网络协议栈 (NVS / Netif / EventLoop)
     */
    esp_err_t init();

    /**
     * @brief 连接指定的 Wi-Fi 热点
     * @param ssid Wi-Fi 名称 (C 字符串)
     * @param password Wi-Fi 密码 (C 字符串)
     * @param timeout_ms 超时时间 (默认 10000ms)
     * @return ESP_OK 表示连接成功并成功获取到 IP
     */
    esp_err_t connect(const char* ssid, const char* password, uint32_t timeout_ms = 10000);

    /**
     * @brief 注册网络连接状态变动回调
     */
    void setOnConnectCallback(ConnectCallback cb) { m_connectCb = cb; }

    /**
     * @brief 当前是否已连接互联网
     */
    bool isConnected() const { return m_isConnected; }

    /**
     * @brief 获取当前分配的 IP 地址
     */
    const char* getIp() const { return m_ipAddr; }

private:
    bool               m_isConnected;
    char               m_ipAddr[16];
    EventGroupHandle_t m_wifiEventGroup;
    ConnectCallback    m_connectCb;

    static void eventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
};
