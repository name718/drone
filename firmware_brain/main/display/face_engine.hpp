#pragma once

#include <cstdint>

#include "oled_driver.hpp"

/**
 * @brief 机器人表情状态枚举
 */
enum class EmotionState {
    NORMAL,     // 正常待机状态 (睁大眼睛 + 随机自然眨眼)
    HAPPY,      // 开心状态 (弯弯笑眼/月牙眼)
    LISTENING,  // 倾听状态 (眼睛变大放光)
};

/**
 * @brief 拟人动态表情引擎类
 */
class FaceEngine {
public:
    /**
     * @brief 构造函数：绑定底层的 OLED 屏幕驱动对象引用
     */
    explicit FaceEngine(OledDriver &oled);

    /**
     * @brief 表情引擎心跳更新函数 (需要在渲染循环中以固定帧率调用，如每 20ms 一次)
     */
    void update();

    /**
     * @brief 切换当前表情状态
     */
    void setEmotion(EmotionState state);

private:
    OledDriver &m_oled;             // 绑定的 OLED 屏幕驱动对象引用
    EmotionState m_currentEmotion;  // 当前所处的表情状态

    // --- 动画状态与计时器 ---
    uint32_t m_lastBlinkTimeMs;      // 上一次眨眼完成的时间戳 (毫秒)
    uint32_t m_nextBlinkIntervalMs;  // 距离下一次眨眼的随机等待间隔 (2000 ~ 4000 ms)
    bool m_isBlinking;               // 当前是否正处于眨眼动画过程中
    int m_blinkPhase;                // 眨眼动画当前帧阶段

    // 默认眼睛标准几何尺寸 (居中对称)
    static constexpr int EYE_WIDTH = 28;     // 眼睛默认宽度
    static constexpr int EYE_HEIGHT = 32;    // 眼睛默认完全睁开的高度
    static constexpr int EYE_RADIUS = 6;     // 眼睛圆角半径
    static constexpr int LEFT_EYE_X = 24;    // 左眼 X 起点坐标
    static constexpr int RIGHT_EYE_X = 76;   // 右眼 X 起点坐标
    static constexpr int EYE_CENTER_Y = 32;  // 眼睛垂直中心线 Y 坐标

    // 内部绘制具体表情的函数
    void drawNormalEyes(int eye_height);
    void drawHappyEyes();
    void drawListeningEyes();
};
