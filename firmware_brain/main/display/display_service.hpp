/**
 * @file display_service.hpp
 * @brief 视觉业务服务头文件
 *
 * 【架构职责】：
 *  默认远程模式：setEmotion只更新HeadLink状态，由C8T6本地绘制。
 *  旧接线兼容模式才初始化ESP SPI和渲染任务，不能在摄像头新接线下启用。
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
