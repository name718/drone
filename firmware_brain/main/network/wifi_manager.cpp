/**
 * @file wifi_manager.cpp
 * @brief ESP32-S3 Wi-Fi Station 模式连接与自动重连管理服务实现
 */

#include "wifi_manager.hpp"

#include <cstring>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char *TAG = "WIFI_MANAGER";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

WifiManager::WifiManager() : m_isConnected(false), m_wifiEventGroup(nullptr) {
    strncpy(m_ipAddr, "0.0.0.0", sizeof(m_ipAddr));
}

WifiManager::~WifiManager() {
    if (m_wifiEventGroup) {
        vEventGroupDelete(m_wifiEventGroup);
    }
}

esp_err_t WifiManager::init() {
    ESP_LOGI(TAG, "初始化 NVS 存储与 TCP/IP 网络协议栈...");

    // 1. 初始化 NVS
    // 准备好板载 SPI Flash
    // 中的特定存储区域，以便后续以“键值对（Key-Value）”的形式读写掉电不丢失的数据。
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. 初始化底层网络接口与事件循环
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // 3. 创建事件组
    m_wifiEventGroup = xEventGroupCreate();

    // 4. 初始化 Wi-Fi 驱动底层
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 5. 注册事件监听器
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &WifiManager::eventHandler, this, nullptr));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &WifiManager::eventHandler, this, nullptr));

    ESP_LOGI(TAG, "✅ Wi-Fi 协议栈初始化就绪！");
    return ESP_OK;
}

esp_err_t WifiManager::connect(const char *ssid, const char *password, uint32_t timeout_ms) {
    if (!ssid)
        return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "正在连接 Wi-Fi 热点: [%s] ...", ssid);

    wifi_config_t wifi_config = {};
    strncpy(reinterpret_cast<char *>(wifi_config.sta.ssid), ssid, sizeof(wifi_config.sta.ssid));
    if (password) {
        strncpy(reinterpret_cast<char *>(wifi_config.sta.password), password,
                sizeof(wifi_config.sta.password));
    }
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 阻塞等待连接成功或超时
    EventBits_t bits = xEventGroupWaitBits(m_wifiEventGroup, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "🎉 成功连接到 Wi-Fi: [%s], 分配 IP 地址: %s", ssid, m_ipAddr);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "❌ 连接 Wi-Fi: [%s] 超时失败！请检查名称与密码。", ssid);
        return ESP_ERR_TIMEOUT;
    }
}

void WifiManager::eventHandler(void *arg, esp_event_base_t event_base, int32_t event_id,
                               void *event_data) {
    auto *self = static_cast<WifiManager *>(arg);

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        self->m_isConnected = false;
        ESP_LOGW(TAG, "⚠️ Wi-Fi 连接断开，正在尝试自动重连...");
        esp_wifi_connect();
        xEventGroupClearBits(self->m_wifiEventGroup, WIFI_CONNECTED_BIT);
        if (self->m_connectCb) {
            self->m_connectCb(false, "");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto *event = static_cast<ip_event_got_ip_t *>(event_data);
        esp_ip4addr_ntoa(&event->ip_info.ip, self->m_ipAddr, sizeof(self->m_ipAddr));

        self->m_isConnected = true;
        xEventGroupSetBits(self->m_wifiEventGroup, WIFI_CONNECTED_BIT);

        if (self->m_connectCb) {
            self->m_connectCb(true, self->m_ipAddr);
        }
    }
}
