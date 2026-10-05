/**
 * @file face_engine.cpp
 * @brief 赛博拟人表情引擎算法模型实现文件
 */
#include "display/face_engine.hpp"

#include <cstdlib>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 屏幕中心坐标与双眼几何常量 (严格适配 128x160 竖屏比例)
static constexpr int16_t EYE_LEFT_X = 36;    // 左眼水平中心 (距左边缘 20 像素)
static constexpr int16_t EYE_RIGHT_X = 92;   // 右眼水平中心 (距右边缘 20 像素，对称分布)
static constexpr int16_t EYE_CENTER_Y = 76;  // 眼睛垂直中心 (160 像素中轴微偏上，视觉拟人最自然)

static constexpr int16_t DEFAULT_W = 32;  // 正常眼宽 (两眼间距 24 像素)
static constexpr int16_t DEFAULT_H = 48;  // 正常眼高
static constexpr int16_t CORNER_R = 8;    // 基础圆角弧度

// 拟人眨眼微动画高度衰减序列 (模拟上下眼睑向中心极速闭合与回弹)
static const int16_t BLINK_HEIGHTS[] = {48, 32, 14, 4, 26, 48};
static constexpr int BLINK_FRAME_COUNT = sizeof(BLINK_HEIGHTS) / sizeof(BLINK_HEIGHTS[0]);

FaceEngine::FaceEngine(ST7735Driver &driver) : driver_(driver) {
    // 初始化下一次眨眼触发时机 (开机 2 秒后)
    next_blink_tick_ = xTaskGetTickCount() + pdMS_TO_TICKS(2000);
}

void FaceEngine::setEmotion(EmotionState emotion) {
    current_emotion_ = emotion;
    blink_step_ = -1;  // 切换情绪时立刻打断眨眼，呈现目标情绪
}

void FaceEngine::drawEye(int16_t center_x, int16_t center_y, int16_t w, int16_t h, uint16_t color) {
    // 【垂直居中核心几何】：y 坐标始终以 center_y 为轴上下对称展开
    int16_t x = center_x - (w / 2);
    int16_t y = center_y - (h / 2);
    int16_t r = (h < 16) ? (h / 2) : CORNER_R;

    // 绘制外发光微晕轮廓 (霓虹科技感)
    driver_.fillRoundRect(x - 1, y - 1, w + 2, h + 2, r, Colors::DARK_CYAN);
    // 绘制核心发光大眼 (纯净赛博青色)
    driver_.fillRoundRect(x, y, w, h, r, color);
}

void FaceEngine::drawHappyEye(int16_t center_x, int16_t center_y) {
    // 开心笑眼：月牙弧线绘制 (适配 32 像素眼宽)
    int16_t x = center_x - 16;
    int16_t y = center_y - 10;
    // 画一个外实心弧，再用黑色背景切掉下半部，形成弯弯笑眼
    driver_.fillRoundRect(x, y, 32, 22, 10, Colors::CYAN);
    driver_.fillRoundRect(x - 2, y + 6, 36, 20, 8, Colors::BLACK);
}

void FaceEngine::update() {
    uint32_t now_ticks = xTaskGetTickCount();
    breath_counter_++;

    // 1. 每帧开始：全屏显存清黑
    driver_.clear(Colors::BLACK);

    // 2. 根据当前情绪状态机执行具体绘制
    switch (current_emotion_) {
        case EmotionState::NORMAL: {
            // --- 拟人自然呼吸算法 (每 30 帧产生 1 像素微小轻柔起伏) ---
            int16_t breath_h = DEFAULT_H + ((breath_counter_ / 16) % 2 == 0 ? 0 : 1);

            // --- 拟人自然随机眨眼算法 ---
            int16_t render_h = breath_h;
            if (blink_step_ >= 0) {
                // 正在眨眼中：逐帧取出对应高度
                render_h = BLINK_HEIGHTS[blink_step_];
                blink_step_++;
                if (blink_step_ >= BLINK_FRAME_COUNT) {
                    // 眨眼动作播放完毕：生成下一次随机间隔 (2000ms ~ 4500ms 随机区间)
                    blink_step_ = -1;
                    uint32_t random_ms = 2000 + (std::rand() % 2500);
                    next_blink_tick_ = now_ticks + pdMS_TO_TICKS(random_ms);
                }
            } else if (now_ticks >= next_blink_tick_) {
                // 时间到达，触发新一轮眨眼
                blink_step_ = 0;
            }

            // 绘制左右两只赛博大眼
            drawEye(EYE_LEFT_X, EYE_CENTER_Y, DEFAULT_W, render_h, Colors::CYAN);
            drawEye(EYE_RIGHT_X, EYE_CENTER_Y, DEFAULT_W, render_h, Colors::CYAN);
            break;
        }

        case EmotionState::HAPPY:
            // 绘制一对笑眯眯的月牙大眼
            drawHappyEye(EYE_LEFT_X, EYE_CENTER_Y);
            drawHappyEye(EYE_RIGHT_X, EYE_CENTER_Y);
            break;

        case EmotionState::SURPRISED:
            // 惊讶：两只瞪圆的大眼睛 (放大为 36x44，保持垂直对称)
            drawEye(EYE_LEFT_X, EYE_CENTER_Y, 36, 44, Colors::CYAN);
            drawEye(EYE_RIGHT_X, EYE_CENTER_Y, 36, 44, Colors::CYAN);
            break;

        case EmotionState::SLEEPY:
            // 困倦：半闭眼睑 (高度只有 8 像素的狭长慵懒眼)
            drawEye(EYE_LEFT_X, EYE_CENTER_Y + 12, DEFAULT_W, 8, Colors::DARK_CYAN);
            drawEye(EYE_RIGHT_X, EYE_CENTER_Y + 12, DEFAULT_W, 8, Colors::DARK_CYAN);
            break;
    }

    // 3. 将计算好的整屏显存批量 DMA 推流上屏！
    driver_.flush();
}
