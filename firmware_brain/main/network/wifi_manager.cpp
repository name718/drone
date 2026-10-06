/**
 * @file wifi_manager.cpp
 * @brief Wi-Fi 网络连接管理器实现文件
 */
#include "network/wifi_manager.hpp"

#include <cstring>

#include "config/board_config.hpp"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char *TAG = "网络管理";

// 事件组标志位：第 0 位代表成功拿到 IP
constexpr EventBits_t WIFI_CONNECTED_BIT = BIT0;

WifiManager &WifiManager::getInstance() {
    static WifiManager instance;
    return instance;
}

WifiManager::WifiManager() {
    wifi_event_group_ = xEventGroupCreate();
}

esp_err_t WifiManager::init() {
    ESP_LOGI(TAG, "正在初始化 Wi-Fi 网络协议栈...");

    // 1. 初始化 NVS 闪存 (Wi-Fi 底层硬件物理校准数据必须存放在 NVS 中)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS 闪存初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. 初始化底层网络接口层 (Netif) 与全局系统事件循环
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 3. 创建默认 Station (STA) 网卡
    sta_netif_ = esp_netif_create_default_wifi_sta();

    // 4. 初始化 Wi-Fi 硬件驱动
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 5. 注册 Wi-Fi 事件与 IP 事件监听器
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &WifiManager::eventHandler, this, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &WifiManager::eventHandler, this, nullptr));

    // 6. 配置目标 Wi-Fi 的 SSID 与密码
    wifi_config_t wifi_config = {};
    std::strncpy(reinterpret_cast<char *>(wifi_config.sta.ssid), Config::Network::WIFI_SSID,
                 sizeof(wifi_config.sta.ssid));
    std::strncpy(reinterpret_cast<char *>(wifi_config.sta.password), Config::Network::WIFI_PASS,
                 sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // 7. 点火启动 Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Wi-Fi 驱动就绪，正在尝试连接热点 [%s]...", Config::Network::WIFI_SSID);

    return ESP_OK;
}

void WifiManager::eventHandler(void *arg, esp_event_base_t event_base, int32_t event_id,
                               void *event_data) {
    auto *self = static_cast<WifiManager *>(arg);

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // Wi-Fi 硬件已启动，立刻发起连接
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        self->is_connected_.store(false);
        xEventGroupClearBits(self->wifi_event_group_, WIFI_CONNECTED_BIT);

        if (self->retry_count_ < Config::Network::WIFI_MAX_RETRY) {
            self->retry_count_++;
            ESP_LOGW(TAG, "Wi-Fi 连接断开/失败，正在进行第 %d/%d 次重试...", self->retry_count_,
                     Config::Network::WIFI_MAX_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "Wi-Fi 连接重试次数达上限，请检查路由器名称与密码！");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto *event = static_cast<ip_event_got_ip_t *>(event_data);
        ESP_LOGI(TAG, "=================================================");
        ESP_LOGI(TAG, "Wi-Fi 连接成功！");
        ESP_LOGI(TAG, "本机分配 IP : " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "子网掩码    : " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "网关地址    : " IPSTR, IP2STR(&event->ip_info.gw));
        ESP_LOGI(TAG, "=================================================");

        // 配置主公共 DNS (阿里 223.5.5.5) 与备用公共 DNS (腾讯 119.29.29.29)
        if (self->sta_netif_) {
            esp_netif_dns_info_t dns_main = {};
            dns_main.ip.type = ESP_IPADDR_TYPE_V4;
            esp_netif_str_to_ip4("223.5.5.5", &dns_main.ip.u_addr.ip4);
            esp_netif_set_dns_info(self->sta_netif_, ESP_NETIF_DNS_MAIN, &dns_main);

            esp_netif_dns_info_t dns_backup = {};
            dns_backup.ip.type = ESP_IPADDR_TYPE_V4;
            esp_netif_str_to_ip4("119.29.29.29", &dns_backup.ip.u_addr.ip4);
            esp_netif_set_dns_info(self->sta_netif_, ESP_NETIF_DNS_BACKUP, &dns_backup);

            ESP_LOGI(TAG, "公共 DNS 配置就绪: 主 223.5.5.5, 备 119.29.29.29");
        }

        // 启动网络时间协议 SNTP 自动对齐现实北京时间
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "ntp.aliyun.com");
        esp_sntp_setservername(1, "cn.pool.ntp.org");
        esp_sntp_init();
        setenv("TZ", "CST-8", 1);
        tzset();
        ESP_LOGI(TAG, "已启动 SNTP 网络时间同步，时区设为中国标准时间 (UTC+8)");

        self->retry_count_ = 0;
        self->is_connected_.store(true);
        xEventGroupSetBits(self->wifi_event_group_, WIFI_CONNECTED_BIT);
    }
}

bool WifiManager::waitForConnected(TickType_t timeout_ticks) {
    EventBits_t bits =
        xEventGroupWaitBits(wifi_event_group_, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, timeout_ticks);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}
