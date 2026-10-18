/** @file camera_service.hpp @brief OV5640采集服务；摄像头失败不阻塞头部、音频及控制网页。 */
#pragma once
#include "esp_err.h"
#include "esp_http_server.h"

class CameraService {
public:
    static esp_err_t start();
    static esp_err_t registerControlRoutes(httpd_handle_t server);
private:
    static void task(void *);
    static esp_err_t statusHandler(httpd_req_t *req);
    static esp_err_t previewHandler(httpd_req_t *req);
    static esp_err_t captureHandler(httpd_req_t *req);
    static esp_err_t streamHandler(httpd_req_t *req);
    static esp_err_t retryHandler(httpd_req_t *req);
    static esp_err_t registersHandler(httpd_req_t *req);
    static esp_err_t colorHandler(httpd_req_t *req);
    static esp_err_t focusHandler(httpd_req_t *req);
    static esp_err_t profileHandler(httpd_req_t *req);
};
