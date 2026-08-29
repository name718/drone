// ### 💡 拟人眨眼动画的 3 个核心设计细节：
//  #### 1. 随机间隔（拒绝僵硬机械感）
//  • 现实中人类眨眼不是死板的每隔 3 秒整整齐齐眨一次。
//  • 算法设计：每次眨完眼，生成一个 2000 ~ 4500 毫秒 的随机时间作为下一次眨眼的等待期。

//  #### 2. 毫秒级极速帧序列（闭眼快、睁眼弹）
//  • 眨眼过程非常快（总共仅约 120 毫秒），我们用一个 6
//  阶段的状态机来控制眼睛的高度变化：
//      • 阶段 0：高度 32px（正常大眼）
//      • 阶段 1：高度 20px（快速闭合）
//      • 阶段 2：高度 8px（接近全闭）
//      • 阶段 3：高度 3px（完全闭合成一条细缝线）
//      • 阶段 4：高度 18px（快速回弹）
//      • 阶段 5：高度 32px（完全睁开，眨眼结束，重置下一次随机计时器）
// #### 3. 动态居中算法（上下合拢）

// • 眼睛在高度从 32 像素压缩到 3 像素时，眼睛的垂直中心点（Y=32）必须保持不变：
//   当 前  Y 起 点  = 32 - (当 前 高 度 /2)

// • 这样双眼是从“上下两头往中间自然闭合”，视觉效果极其顺滑！

/**
 * @file face_engine.cpp
 * @brief 机器人拟人动态表情引擎实现
 */

#include "face_engine.hpp"

#include <cstdlib>

#include "esp_timer.h"

// 获取系统自开机以来的运行毫秒数
static inline uint32_t getSystemTimeMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

FaceEngine::FaceEngine(OledDriver &oled)
    : m_oled(oled),
      m_currentEmotion(EmotionState::NORMAL),
      m_lastBlinkTimeMs(0),
      m_nextBlinkIntervalMs(2500),
      m_isBlinking(false),
      m_blinkPhase(0) {}

void FaceEngine::setEmotion(EmotionState state) {
    m_currentEmotion = state;
    // 切换表情时重置眨眼状态
    m_isBlinking = false;
    m_blinkPhase = 0;
}

void FaceEngine::update() {
    m_oled.clear();

    switch (m_currentEmotion) {
        case EmotionState::NORMAL: {
            uint32_t now = getSystemTimeMs();

            // 1. 判断是否该触发下一次眨眼
            if (!m_isBlinking && (now - m_lastBlinkTimeMs >= m_nextBlinkIntervalMs)) {
                m_isBlinking = true;
                m_blinkPhase = 0;
            }

            // 2. 处理眨眼帧动画
            if (m_isBlinking) {
                // 眨眼动画的 6 帧高度变化表 (单位: 像素)
                const int blink_heights[] = {32, 20, 8, 3, 18, 32};
                const int total_phases = sizeof(blink_heights) / sizeof(blink_heights[0]);

                drawNormalEyes(blink_heights[m_blinkPhase]);

                m_blinkPhase++;
                if (m_blinkPhase >= total_phases) {
                    // 眨眼动作完成
                    m_isBlinking = false;
                    m_blinkPhase = 0;
                    m_lastBlinkTimeMs = now;
                    // 随机生成下一次眨眼间隔: 2000ms ~ 4500ms
                    m_nextBlinkIntervalMs = 2000 + (rand() % 2500);
                }
            } else {
                // 正常睁大眼睛
                drawNormalEyes(EYE_HEIGHT);
            }
            break;
        }

        case EmotionState::HAPPY:
            drawHappyEyes();
            break;

        case EmotionState::LISTENING:
            drawListeningEyes();
            break;
    }

    // 刷屏到硬件
    m_oled.update();
}

void FaceEngine::drawNormalEyes(int eye_height) {
    // 动态计算 Y 坐标，确保上下居中闭合
    int current_y = EYE_CENTER_Y - (eye_height / 2);
    // 当眼睛高度变矮时，圆角半径相应缩小，防止圆角溢出
    int radius = (eye_height < EYE_RADIUS * 2) ? (eye_height / 2) : EYE_RADIUS;

    // 绘制左眼
    m_oled.fillRoundRect(LEFT_EYE_X, current_y, EYE_WIDTH, eye_height, radius, true);
    // 绘制右眼
    m_oled.fillRoundRect(RIGHT_EYE_X, current_y, EYE_WIDTH, eye_height, radius, true);
}

void FaceEngine::drawHappyEyes() {
    // 开心月牙眼: 绘制上弯的圆弧眉眼
    for (int offset = 0; offset < 5; offset++) {
        // 左月牙
        m_oled.drawRoundRect(LEFT_EYE_X, 22 + offset, EYE_WIDTH, 20, 8, true);
        // 右月牙
        m_oled.drawRoundRect(RIGHT_EYE_X, 22 + offset, EYE_WIDTH, 20, 8, true);
    }
    // 遮挡掉下半部分，留下弯弯的月牙笑眼
    m_oled.fillRect(LEFT_EYE_X - 2, 32, EYE_WIDTH + 4, 25, false);
    m_oled.fillRect(RIGHT_EYE_X - 2, 32, EYE_WIDTH + 4, 25, false);
}

void FaceEngine::drawListeningEyes() {
    // 倾听状态: 眼睛微微放大变圆，显出灵动好奇的神态
    m_oled.fillRoundRect(LEFT_EYE_X - 2, EYE_CENTER_Y - 18, EYE_WIDTH + 4, 36, 10, true);
    m_oled.fillRoundRect(RIGHT_EYE_X - 2, EYE_CENTER_Y - 18, EYE_WIDTH + 4, 36, 10, true);
}
