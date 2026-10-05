/**
 * @file face_engine.hpp
 * @brief 赛博拟人表情引擎算法模型头文件
 *
 * 【职责】：
 *  纯数学与图形渲染模型，负责计算眼睛当前帧的高低宽窄、眨眼状态机与情绪变换，
 *  不直接持有死循环线程，完全与底层硬件解耦。
 */
#pragma once

#include <cstdint>

#include "display/st7735.hpp"

// 强类型情绪枚举 (强类型 enum class 避免命名污染)
enum class EmotionState {
    NORMAL,     // 正常状态 (赛博大眼 + 自然呼吸 + 随机眨眼)
    HAPPY,      // 开心状态 (月牙微笑弯弯眼)
    SURPRISED,  // 惊讶状态 (瞳孔放大圆形大眼)
    SLEEPY      // 困倦状态 (半闭眼睑呼吸)
};

class FaceEngine {
public:
    /**
     * @brief 构造函数：接收底层屏幕驱动引用 (零拷贝，高效复用)
     * @param driver 屏幕驱动对象引用
     */
    explicit FaceEngine(ST7735Driver &driver);
    ~FaceEngine() = default;

    /**
     * @brief 动画单帧更新算法 (由上层以 30~33 FPS 频率周期性调用)
     */
    void update();

    /**
     * @brief 切换当前情绪状态
     */
    void setEmotion(EmotionState emotion);

private:
    // 单眼绘制辅助函数
    void drawEye(int16_t center_x, int16_t center_y, int16_t w, int16_t h, uint16_t color);

    // 绘制开心月牙眼
    void drawHappyEye(int16_t center_x, int16_t center_y);

    ST7735Driver &driver_;  // 屏幕驱动引用
    EmotionState current_emotion_{EmotionState::NORMAL};

    // 眨眼状态机变量
    int blink_step_{-1};           // -1 表示睁眼状态，>=0 表示正在执行眨眼帧序列
    uint32_t next_blink_tick_{0};  // 下一次眨眼的时间戳 (FreeRTOS Ticks)
    uint32_t breath_counter_{0};   // 呼吸循环计数器
};
