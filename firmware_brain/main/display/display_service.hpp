/**
 * @file display_service.hpp
 * @brief 机器人视觉与表情后台管理服务
 */

#pragma once

#include "oled_driver.hpp"
#include "face_engine.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class DisplayService {
public:
    DisplayService(int sda_pin = 8, int scl_pin = 9, uint8_t i2c_addr = 0x3C);
    ~DisplayService();

    /**
     * @brief 初始化 OLED 屏幕硬件
     */
    esp_err_t init();

    /**
     * @brief 启动 Core 1 独立表情渲染任务 (~33 FPS)
     */
    esp_err_t start();

    /**
     * @brief 设置机器人当前表情状态
     */
    void setEmotion(EmotionState emotion);

    /**
     * @brief 获取底层表情引擎对象引用
     */
    FaceEngine& getFaceEngine() { return m_face; }

private:
    OledDriver   m_oled;
    FaceEngine   m_face;
    TaskHandle_t m_taskHandle;

    static void renderTaskEntry(void* pvParameters);
};
