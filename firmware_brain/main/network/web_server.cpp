/**
 * @file web_server.cpp
 * @brief Web 管理后台与 WebSocket 全双工流式服务实现文件
 */
#include "network/web_server.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include "comm/chassis_service.hpp"
#include "display/display_service.hpp"
#include "gimbal/gimbal_service.hpp"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network/web_page.h"  // 引入我们刚才生成的 HTML 网页常量

static const char *TAG = "WEB服务";

WebServer &WebServer::getInstance() {
    static WebServer instance;
    return instance;
}

WebServer::WebServer() = default;

WebServer::~WebServer() {
    stop();
}

/**
 * @brief 响应浏览器访问首页 (GET http://<ESP32_IP>/)
 */
esp_err_t WebServer::indexHandler(httpd_req_t *req) {
    // 设置响应头为 UTF-8 网页格式
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    // 将保存在 Flash 中的 INDEX_HTML 字符串直接发送给浏览器
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

/**
 * @brief 处理 WebSocket 长连接帧数据 (/ws)
 */
esp_err_t WebServer::wsHandler(httpd_req_t *req) {
    // 1. 如果是初次 HTTP 握手请求，系统握手成功后直接返回
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "🤝 收到来自浏览器的 WebSocket 握手成功！(套接字 fd: %d)",
                 httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    // 2. 准备接收 WebSocket 数据帧
    httpd_ws_frame_t ws_pkt;
    std::memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    // 第一次调用：将 max_len 设为 0，目的是探测本次发来的数据包实际有多长
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK || ws_pkt.len == 0) {
        return ret;
    }

    // 动态开辟临时缓冲区存放发来的内容 (多预留 1 字节放 '\0' 结尾符)
    char *buf = new char[ws_pkt.len + 1];
    ws_pkt.payload = reinterpret_cast<uint8_t *>(buf);

    // 第二次调用：真正将数据读取到内存中
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret == ESP_OK) {
        buf[ws_pkt.len] = '\0';  // 补全字符串结尾
        std::string payload(buf);

        // 获取底盘单例，准备下发运动
        auto &chassis = ChassisService::getInstance();

        // 简易高效按键指令解析 (不依赖繁重第三方 JSON 库)
        if (payload.find("\"FORWARD\"") != std::string::npos) {
            chassis.sendVelocityCommand(200, 0);  // 前进: 200 mm/s
        } else if (payload.find("\"BACKWARD\"") != std::string::npos) {
            chassis.sendVelocityCommand(-200, 0);  // 后退: -200 mm/s
        } else if (payload.find("\"LEFT\"") != std::string::npos) {
            chassis.sendVelocityCommand(0, 500);  // 左转: 500 mrad/s
        } else if (payload.find("\"RIGHT\"") != std::string::npos) {
            chassis.sendVelocityCommand(0, -500);  // 右转: -500 mrad/s
        } else if (payload.find("\"STOP\"") != std::string::npos) {
            chassis.sendVelocityCommand(0, 0);  // 停止
        } else if (payload.find("\"emotion\"") != std::string::npos || payload.find("\"EMOTION\"") != std::string::npos) {
            // 🎭 表情切换控制通道
            auto &display = DisplayService::getInstance();
            if (payload.find("\"HAPPY\"") != std::string::npos) {
                display.setEmotion(EmotionState::HAPPY);
            } else if (payload.find("\"SURPRISED\"") != std::string::npos) {
                display.setEmotion(EmotionState::SURPRISED);
            } else if (payload.find("\"SLEEPY\"") != std::string::npos) {
                display.setEmotion(EmotionState::SLEEPY);
            } else if (payload.find("\"NORMAL\"") != std::string::npos) {
                display.setEmotion(EmotionState::NORMAL);
            }
        } else if (payload.find("\"gimbal_gesture\"") != std::string::npos) {
            // 🦾 云台预设动作手势通道
            auto &gimbal = GimbalService::getInstance();
            if (payload.find("\"NOD\"") != std::string::npos) {
                gimbal.nod();
            } else if (payload.find("\"SHAKE\"") != std::string::npos) {
                gimbal.shake();
            } else if (payload.find("\"RESET\"") != std::string::npos || payload.find("\"CENTER\"") != std::string::npos) {
                gimbal.reset();
            }
        } else if (payload.find("\"gimbal_angle\"") != std::string::npos) {
            // 🦾 云台滑条指定绝对角度控制: {"type":"gimbal_angle","pan":90,"tilt":90}
            int pan = 90;
            int tilt = 90;
            const char *p_pan = std::strstr(buf, "\"pan\":");
            const char *p_tilt = std::strstr(buf, "\"tilt\":");
            if (p_pan) pan = std::atoi(p_pan + 6);
            if (p_tilt) tilt = std::atoi(p_tilt + 7);
            GimbalService::getInstance().lookAt(static_cast<float>(pan), static_cast<float>(tilt));
        } else if (payload.find("\"ai_prompt\"") != std::string::npos) {
            // 【大模型预留通道】：收到网页端发来的文字，先给一个即时握手响应
            const char reply[] =
                "{\"type\":\"ai_reply\",\"text\":\"🤖 [ESP32大脑已接收] WebSocket "
                "全双工流式通道测试成功！待接入大模型音视频流。\"}";
            httpd_ws_frame_t out_pkt;
            std::memset(&out_pkt, 0, sizeof(httpd_ws_frame_t));
            out_pkt.type = HTTPD_WS_TYPE_TEXT;
            out_pkt.payload = reinterpret_cast<uint8_t *>(const_cast<char *>(reply));
            out_pkt.len = std::strlen(reply);
            httpd_ws_send_frame(req, &out_pkt);
        }
    }

    // 释放动态内存
    delete[] buf;
    return ret;
}

/**
 * @brief 定时将硬件健康数据广播推送给所有已连接的网页
 */
void WebServer::broadcastTelemetry() {
    if (!server_)
        return;

    // 获取当前连在 ESP32 上的所有客户端连接套接字 (最多支持 8 个)
    size_t fds = 8;
    int client_fds[8];
    if (httpd_get_client_list(server_, &fds, client_fds) != ESP_OK || fds == 0) {
        return;
    }

    // 收集硬件指标
    uint32_t sram_kb = esp_get_free_internal_heap_size() / 1024;
    uint32_t psram_kb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
    uint32_t uptime_s = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);

    // 打包为轻量 JSON 字符串
    char json_buf[160];
    int len =
        std::snprintf(json_buf, sizeof(json_buf),
                      "{\"type\":\"telemetry\",\"uptime\":%lu,\"sram_kb\":%lu,\"psram_kb\":%lu}",
                      static_cast<unsigned long>(uptime_s), static_cast<unsigned long>(sram_kb),
                      static_cast<unsigned long>(psram_kb));

    // 异步推送到所有处于 WebSocket 状态的客户端
    for (size_t i = 0; i < fds; i++) {
        int fd = client_fds[i];
        if (httpd_ws_get_fd_info(server_, fd) == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_frame_t frame;
            std::memset(&frame, 0, sizeof(httpd_ws_frame_t));
            frame.type = HTTPD_WS_TYPE_TEXT;
            frame.payload = reinterpret_cast<uint8_t *>(json_buf);
            frame.len = len;
            httpd_ws_send_frame_async(server_, fd, &frame);
        }
    }
}

void WebServer::telemetryTask(void *param) {
    auto *self = static_cast<WebServer *>(param);
    while (self->is_running_) {
        self->broadcastTelemetry();
        vTaskDelay(pdMS_TO_TICKS(1000));  // 每隔 1 秒主动向网页推送一次最新体检数据
    }
    vTaskDelete(nullptr);
}

esp_err_t WebServer::start() {
    if (server_)
        return ESP_OK;

    // 默认 HTTP 服务器配置 (标准 80 端口)
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_open_sockets = 7;

    ESP_LOGI(TAG, "正在启动 WebServer (端口 80)...");
    esp_err_t ret = httpd_start(&server_, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启动 WebServer 失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 1. 注册普通 HTTP URI: GET /
    httpd_uri_t uri_get = {};
    uri_get.uri = "/";
    uri_get.method = HTTP_GET;
    uri_get.handler = indexHandler;
    uri_get.user_ctx = nullptr;
    uri_get.is_websocket = false;
    httpd_register_uri_handler(server_, &uri_get);

    // 2. 注册 WebSocket 升级 URI: GET /ws (关键: is_websocket = true)
    httpd_uri_t uri_ws = {};
    uri_ws.uri = "/ws";
    uri_ws.method = HTTP_GET;
    uri_ws.handler = wsHandler;
    uri_ws.user_ctx = nullptr;
    uri_ws.is_websocket = true;
    httpd_register_uri_handler(server_, &uri_ws);

    // 3. 启动后台每秒广播任务
    is_running_ = true;
    xTaskCreatePinnedToCore(telemetryTask, "WsTelemetry", 4096, this, 3, nullptr, 0);

    ESP_LOGI(TAG, "🎉 Web 控制台与 WebSocket 服务已成功就绪！");
    return ESP_OK;
}

void WebServer::stop() {
    if (server_) {
        is_running_ = false;
        httpd_stop(server_);
        server_ = nullptr;
    }
}
