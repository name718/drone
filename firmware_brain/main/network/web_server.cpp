/**
 * @file web_server.cpp
 * @brief Web 管理后台与 WebSocket 全双工流式服务实现文件
 */
#include "network/web_server.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include "ai/ai_service.hpp"
#include "audio/audio_service.hpp"
#include "cJSON.h"
#include "comm/chassis_service.hpp"
#include "core/interaction_service.hpp"
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
        ESP_LOGI(TAG, "收到来自浏览器的 WebSocket 握手成功！(套接字 fd: %d)",
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

        // 通知交互引擎用户活跃，重置空闲/打盹计时器
        InteractionService::getInstance().notifyUserActivity();

        // 获取底盘单例，准备下发运动
        auto &chassis = ChassisService::getInstance();

        // 简易高效按键指令与摇杆连续控制解析 (底盘全权独立差速运动，不联动云台)
        if (payload.find("\"FORWARD\"") != std::string::npos) {
            chassis.sendVelocityCommand(200, 0);  // 前进: 200 mm/s
        } else if (payload.find("\"BACKWARD\"") != std::string::npos) {
            chassis.sendVelocityCommand(-200, 0);  // 后退: -200 mm/s
        } else if (payload.find("\"LEFT\"") != std::string::npos) {
            chassis.sendVelocityCommand(0, 500);  // 原地左转: 500 mrad/s
        } else if (payload.find("\"RIGHT\"") != std::string::npos) {
            chassis.sendVelocityCommand(0, -500);  // 原地右转: -500 mrad/s
        } else if (payload.find("\"STOP\"") != std::string::npos) {
            chassis.sendVelocityCommand(0, 0);  // 刹车急停
        } else if (payload.find("\"cmd_vel\"") != std::string::npos) {
            // 手机端虚拟摇杆连续比例速度通道: {"type":"cmd_vel","speed":150,"yaw":-200}
            int speed = 0;
            int yaw = 0;
            const char *p_speed = std::strstr(buf, "\"speed\":");
            const char *p_yaw = std::strstr(buf, "\"yaw\":");
            if (p_speed) speed = std::atoi(p_speed + 8);
            if (p_yaw) yaw = std::atoi(p_yaw + 6);

            chassis.sendVelocityCommand(static_cast<int16_t>(speed), static_cast<int16_t>(yaw));
        } else if (payload.find("\"emotion\"") != std::string::npos || payload.find("\"EMOTION\"") != std::string::npos) {
            // 表情与拟人行为联动通道
            auto &interact = InteractionService::getInstance();
            if (payload.find("\"HAPPY\"") != std::string::npos) {
                interact.triggerBehavior(RobotBehavior::HAPPY);
            } else if (payload.find("\"SURPRISED\"") != std::string::npos) {
                interact.triggerBehavior(RobotBehavior::WAKE_UP);
            } else if (payload.find("\"SLEEPY\"") != std::string::npos) {
                interact.triggerBehavior(RobotBehavior::SLEEP);
            } else if (payload.find("\"LOVE\"") != std::string::npos || payload.find("\"HEART\"") != std::string::npos) {
                interact.triggerBehavior(RobotBehavior::LOVE);
            } else if (payload.find("\"ANGRY\"") != std::string::npos) {
                interact.triggerBehavior(RobotBehavior::ANGRY);
            } else if (payload.find("\"CONFUSED\"") != std::string::npos) {
                interact.triggerBehavior(RobotBehavior::CONFUSED);
            } else if (payload.find("\"DIZZY\"") != std::string::npos) {
                interact.triggerBehavior(RobotBehavior::DIZZY);
            } else if (payload.find("\"DANCE\"") != std::string::npos) {
                interact.triggerDance();
            } else if (payload.find("\"NORMAL\"") != std::string::npos) {
                interact.triggerBehavior(RobotBehavior::NORMAL);
            }
        } else if (payload.find("\"dance\"") != std::string::npos) {
            // 赛博跳舞特技专属指令通道
            InteractionService::getInstance().triggerDance();
        } else if (payload.find("\"gimbal_gesture\"") != std::string::npos) {
            // 云台预设动作手势通道
            auto &interact = InteractionService::getInstance();
            if (payload.find("\"NOD\"") != std::string::npos) {
                interact.triggerBehavior(RobotBehavior::NOD);
            } else if (payload.find("\"SHAKE\"") != std::string::npos) {
                interact.triggerBehavior(RobotBehavior::SHAKE);
            } else if (payload.find("\"RESET\"") != std::string::npos || payload.find("\"CENTER\"") != std::string::npos) {
                GimbalService::getInstance().reset();
            }
        } else if (payload.find("\"gimbal_angle\"") != std::string::npos) {
            // 云台滑条指定绝对角度控制: {"type":"gimbal_angle","pan":90,"tilt":90}
            int pan = 90;
            int tilt = 90;
            const char *p_pan = std::strstr(buf, "\"pan\":");
            const char *p_tilt = std::strstr(buf, "\"tilt\":");
            if (p_pan) pan = std::atoi(p_pan + 6);
            if (p_tilt) tilt = std::atoi(p_tilt + 7);
            GimbalService::getInstance().lookAt(static_cast<float>(pan), static_cast<float>(tilt));
        } else if (payload.find("\"autonomous\"") != std::string::npos || payload.find("\"AUTO_MODE\"") != std::string::npos) {
            // 自主拟人交互模式开关
            bool enable = (payload.find("true") != std::string::npos || payload.find("1") != std::string::npos);
            InteractionService::getInstance().setAutonomousMode(enable);
        } else if (payload.find("\"audio\"") != std::string::npos || payload.find("\"AUDIO\"") != std::string::npos) {
            // 音频播放与音量控制通道
            auto &audio = AudioService::getInstance();
            if (payload.find("\"CHIME\"") != std::string::npos) {
                audio.playBootChime();
            } else if (payload.find("\"BEEP\"") != std::string::npos) {
                audio.playBeep();
            } else if (payload.find("\"ALERT\"") != std::string::npos) {
                audio.playAlert();
            } else if (payload.find("\"VOLUME\"") != std::string::npos || payload.find("\"volume\"") != std::string::npos) {
                const char *p_vol = std::strstr(buf, "\"volume\":");
                if (p_vol) {
                    float vol = static_cast<float>(std::atof(p_vol + 9));
                    audio.setVolume(vol);
                }
            }
        } else if (payload.find("\"ai_prompt\"") != std::string::npos) {
            // 解析网页发来的提问提示词并投入 AiService 异步调度队列
            cJSON *root = cJSON_Parse(buf);
            if (root) {
                cJSON *text_item = cJSON_GetObjectItem(root, "text");
                if (text_item && cJSON_IsString(text_item) && text_item->valuestring) {
                    AiService::getInstance().ask(text_item->valuestring);
                }
                cJSON_Delete(root);
            }
        } else if (payload.find("\"voice_listen\"") != std::string::npos) {
            // 触发麦克风语音聆听
            AiService::getInstance().triggerVoiceListen();
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

    // --- ESP32-S3 大脑核心指标 ---
    uint32_t sram_free_kb = esp_get_free_internal_heap_size() / 1024;
    uint32_t sram_min_kb = esp_get_minimum_free_heap_size() / 1024;
    uint32_t psram_free_kb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
    uint32_t psram_total_kb = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024;
    uint32_t uptime_s = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
    float mic_energy = AudioService::getInstance().getMicEnergy();
    float spk_vol = AudioService::getInstance().getVolume();
    bool auto_mode = InteractionService::getInstance().isAutonomousMode();

    // 机器人二自由度云台实时与目标物理角度 (实时 50Hz 平滑滤波后当前角度)
    auto &gimbal = GimbalService::getInstance();
    float g_pan = gimbal.getCurrentPan();
    float g_tilt = gimbal.getCurrentTilt();
    float g_tgt_p = gimbal.getTargetPan();
    float g_tgt_t = gimbal.getTargetTilt();

    // --- STM32G473 底盘全维度遥测指标 ---
    auto &chassis = ChassisService::getInstance();
    bool chassis_online = chassis.isChassisOnline();
    uint16_t stm_flash_tot = chassis.getFlashTotalKb();
    uint16_t stm_flash_used = chassis.getFlashUsedKb();
    uint16_t stm_sram_tot = chassis.getSramTotalKb();
    uint16_t stm_sram_free = chassis.getSramFreeKb();
    uint32_t uid[3] = {0};
    chassis.getChipUid(uid);
    char uid_str[36];
    std::snprintf(uid_str, sizeof(uid_str), "%08lX-%08lX-%08lX",
                  static_cast<unsigned long>(uid[0]),
                  static_cast<unsigned long>(uid[1]),
                  static_cast<unsigned long>(uid[2]));

    float pitch = chassis.getPitch();
    float roll = chassis.getRoll();
    float pitch_rate = chassis.getPitchRate();
    float acc_pitch = chassis.getAccPitch();
    int16_t speed = chassis.getActualSpeed();
    int16_t l_spd = chassis.getLeftSpeed();
    int16_t r_spd = chassis.getRightSpeed();
    int16_t l_pulse = chassis.getLeftPulse();
    int16_t r_pulse = chassis.getRightPulse();
    int16_t l_pwm = chassis.getLeftPwm();
    int16_t r_pwm = chassis.getRightPwm();
    uint16_t bat_mv = chassis.getBatteryMv();
    uint8_t flags = chassis.getStatusFlags();
    uint64_t rx_bytes = chassis.getTotalRxBytes();
    uint32_t rx_pkts = chassis.getRxPackets();

    // 打包为轻量高效 JSON 字符串 (单帧包含双芯片底层硬件全景参数)
    char json_buf[768];
    int len = std::snprintf(
        json_buf, sizeof(json_buf),
        "{\"type\":\"telemetry\","
        "\"uptime\":%lu,\"sram_free\":%lu,\"sram_min\":%lu,\"psram_free\":%lu,\"psram_total\":%lu,"
        "\"mic_energy\":%.2f,\"spk_vol\":%.2f,\"auto_mode\":%s,"
        "\"g_pan\":%.1f,\"g_tilt\":%.1f,\"g_tgt_p\":%.1f,\"g_tgt_t\":%.1f,"
        "\"chassis_online\":%s,\"stm_flash_tot\":%u,\"stm_flash_used\":%u,\"stm_sram_tot\":%u,\"stm_sram_free\":%u,\"stm_uid\":\"%s\","
        "\"pitch\":%.2f,\"roll\":%.2f,\"pitch_rate\":%.2f,\"acc_pitch\":%.2f,"
        "\"speed\":%d,\"l_spd\":%d,\"r_spd\":%d,\"l_pulse\":%d,\"r_pulse\":%d,"
        "\"l_pwm\":%d,\"r_pwm\":%d,\"battery_mv\":%u,\"status_flags\":%u,\"rx_bytes\":%llu,\"rx_pkts\":%lu}",
        static_cast<unsigned long>(uptime_s), static_cast<unsigned long>(sram_free_kb),
        static_cast<unsigned long>(sram_min_kb), static_cast<unsigned long>(psram_free_kb),
        static_cast<unsigned long>(psram_total_kb), static_cast<double>(mic_energy),
        static_cast<double>(spk_vol), auto_mode ? "true" : "false",
        static_cast<double>(g_pan), static_cast<double>(g_tilt),
        static_cast<double>(g_tgt_p), static_cast<double>(g_tgt_t),
        chassis_online ? "true" : "false", stm_flash_tot, stm_flash_used, stm_sram_tot, stm_sram_free, uid_str,
        static_cast<double>(pitch), static_cast<double>(roll), static_cast<double>(pitch_rate), static_cast<double>(acc_pitch),
        speed, l_spd, r_spd, l_pulse, r_pulse, l_pwm, r_pwm, bat_mv, flags,
        static_cast<unsigned long long>(rx_bytes), static_cast<unsigned long>(rx_pkts));

    // 广播遥测帧
    broadcastText(json_buf, len);
}

void WebServer::broadcastText(const char *text, size_t len) {
    if (!server_ || !text || len == 0) return;
    size_t fds = 7;
    int client_fds[7];
    if (httpd_get_client_list(server_, &fds, client_fds) != ESP_OK) return;

    for (size_t i = 0; i < fds; i++) {
        int fd = client_fds[i];
        if (httpd_ws_get_fd_info(server_, fd) == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_frame_t frame;
            std::memset(&frame, 0, sizeof(httpd_ws_frame_t));
            frame.type = HTTPD_WS_TYPE_TEXT;
            frame.payload = reinterpret_cast<uint8_t *>(const_cast<char *>(text));
            frame.len = len;
            httpd_ws_send_frame_async(server_, fd, &frame);
        }
    }
}

void WebServer::broadcastAiReply(const std::string &reply_text) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "ai_reply");
    cJSON_AddStringToObject(root, "text", reply_text.c_str());
    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        broadcastText(json, std::strlen(json));
        cJSON_free(json);
    }
    cJSON_Delete(root);
}

void WebServer::broadcastAiStatus(const std::string &status) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "ai_status");
    cJSON_AddStringToObject(root, "status", status.c_str());
    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        broadcastText(json, std::strlen(json));
        cJSON_free(json);
    }
    cJSON_Delete(root);
}

void WebServer::telemetryTask(void *param) {
    auto *self = static_cast<WebServer *>(param);
    while (self->is_running_) {
        self->broadcastTelemetry();
        vTaskDelay(pdMS_TO_TICKS(100));  // 100ms (10Hz) 动态刷新率，保证水平姿态仪与遥测丝滑响应
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

    ESP_LOGI(TAG, "Web 控制台与 WebSocket 服务已成功就绪！");
    return ESP_OK;
}

void WebServer::stop() {
    if (server_) {
        is_running_ = false;
        httpd_stop(server_);
        server_ = nullptr;
    }
}

void WebServer::broadcastChatMessage(const std::string &text, const std::string &sender) {
    if (!server_) return;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "chat_msg");
    cJSON_AddStringToObject(root, "sender", sender.c_str());
    cJSON_AddStringToObject(root, "text", text.c_str());

    char *buf = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (buf) {
        broadcastText(buf, strlen(buf));
        cJSON_free(buf);
    }
}
