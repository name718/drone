/**
 * @file display_service.hpp
 * @brief 视觉业务服务头文件
 *
 * 【架构职责】：
 *  1. 单例纳管屏幕驱动与表情引擎的生命周期；
 *  2. 托管专属于 Core 1 的 33 FPS (约 30ms 周期) 丝滑独立渲染线程；
 *  3. 提供多线程安全的 setEmotion 接口供 Web 后台或大模型调用。
 */
#pragma once

#include "display/face_engine.hpp"
#include "display/st7735.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class DisplayService {
public:
    static DisplayService &getInstance();

    /**
     * @brief 初始化硬件驱动与表情引擎
     */
    esp_err_t init();

    /**
     * @brief 启动 Core 1 上的 33 FPS 渲染任务
     */
    esp_err_t start();

    /**
     * @brief 切换表情 (线程安全)
     */
    void setEmotion(EmotionState emotion);

private:
    DisplayService();
    ~DisplayService() = default;

    DisplayService(const DisplayService &) = delete;
    DisplayService &operator=(const DisplayService &) = delete;

    static void renderTask(void *param);

    ST7735Driver driver_;         // 物理屏幕底层驱动
    FaceEngine engine_{driver_};  // 表情算法引擎
    TaskHandle_t task_handle_{nullptr};
    bool is_running_{false};
};
