/**
 * @file web_server.hpp
 * @brief Web 管理后台与 WebSocket 全双工流式服务头文件
 *
 * 【架构职责】：
 *  1. 监听 80 端口，响应 HTTP GET / 请求，将内嵌的前端 SPA 网页秒级推给浏览器；
 *  2. 监听 /ws 路径，将 HTTP 连接一键升级为全双工 WebSocket 长连接；
 *  3. 后台以 1Hz 频率向所有打开网页的用户广播内存、CPU与底盘遥测数据；
 *  4. 接收前端发来的遥控指令（前进/后退/左转/右转）与大模型提示词。
 */
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"  // 官方 Web/WebSocket 核心驱动

class WebServer {
public:
    /**
     * @brief 获取 WebServer 单例引用
     */
    static WebServer &getInstance();

    /**
     * @brief 启动 Web 与 WebSocket 服务
     * @return esp_err_t ESP_OK 表示启动成功
     */
    esp_err_t start();

    /**
     * @brief 停止服务并释放资源
     */
    void stop();

    /**
     * @brief 向所有已连接的 WebSocket 前端广播遥测 JSON 数据
     */
    void broadcastTelemetry();

private:
    WebServer();
    ~WebServer();

    // 单例防克隆护栏 (删除拷贝构造和赋值运算符)
    WebServer(const WebServer &) = delete;
    WebServer &operator=(const WebServer &) = delete;

    /**
     * @brief HTTP GET / 请求处理回调函数：向浏览器返回 HTML 页面
     */
    static esp_err_t indexHandler(httpd_req_t *req);

    /**
     * @brief WebSocket /ws 请求与数据帧处理回调函数
     */
    static esp_err_t wsHandler(httpd_req_t *req);

    /**
     * @brief FreeRTOS 周期性数据广播任务 (运行在 Core 0)
     */
    static void telemetryTask(void *param);

    httpd_handle_t server_{nullptr};  // Web 服务器实例句柄
    bool is_running_{false};          // 服务运行状态标志
};
