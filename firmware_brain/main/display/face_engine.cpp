/**
 * @file face_engine.cpp
 * @brief 赛博拟人表情引擎算法模型实现文件
 */
#include "display/face_engine.hpp"

#include <cmath>
#include <cstdlib>

#include "audio/audio_service.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 屏幕中心坐标与双眼几何常量 (严格适配 128x160 竖屏比例)
static constexpr int16_t EYE_LEFT_X = 36;    // 左眼水平中心 (距左边缘 20 像素)
static constexpr int16_t EYE_RIGHT_X = 92;   // 右眼水平中心 (距右边缘 20 像素，对称分布)
static constexpr int16_t EYE_CENTER_Y = 74;  // 眼睛垂直中心 (160 像素中轴微偏上，留出底部声浪视窗)

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

void FaceEngine::drawHeartEye(int16_t center_x, int16_t center_y, int16_t pulse) {
    // 心动爱心眼：上方两个实心圆瓣 + 下方三角形收尾 (粉红少女萌宠)
    int16_t r = 6 + pulse;
    int16_t lobe_offset = 6 + pulse;

    // 1. 上半部两个圆心
    driver_.fillCircle(center_x - lobe_offset, center_y - 4, r, Colors::PINK);
    driver_.fillCircle(center_x + lobe_offset, center_y - 4, r, Colors::PINK);

    // 2. 下半部向下收敛的倒三角填充
    int16_t start_w = (lobe_offset + r) * 2;
    int16_t height = 16 + pulse * 2;
    for (int16_t row = 0; row < height; row++) {
        int16_t w = start_w - (row * start_w / height);
        driver_.drawFastHLine(center_x - w / 2, center_y - 2 + row, w, Colors::PINK);
    }

    // 3. 绘制左上角萌宠高光闪烁小白点
    driver_.fillCircle(center_x - lobe_offset - 1, center_y - 6, 2, Colors::WHITE);
}

void FaceEngine::drawAngryEye(int16_t center_x, int16_t center_y, bool is_left) {
    // 生气怒火眼：红色主眼 + 倾斜下切锐利内斜眉
    int16_t w = 32;
    int16_t h = 42;
    int16_t x = center_x - (w / 2);
    int16_t y = center_y - (h / 2);

    // 外轮廓橙光 + 内层炽红
    driver_.fillRoundRect(x - 1, y - 1, w + 2, h + 2, 6, Colors::ORANGE);
    driver_.fillRoundRect(x, y, w, h, 6, Colors::RED);

    // 斜向黑角遮罩切出八字怒眉
    if (is_left) {
        // 左眼：向右上方向内切斜
        for (int16_t i = 0; i < 18; i++) {
            driver_.drawFastHLine(center_x - 16 + (i * 32 / 18), y + i, 36, Colors::BLACK);
        }
    } else {
        // 右眼：向左上方向内切斜 (对称)
        for (int16_t i = 0; i < 18; i++) {
            driver_.drawFastHLine(x - 4, y + i, 32 - (i * 32 / 18), Colors::BLACK);
        }
    }
}

void FaceEngine::drawConfusedEyes() {
    // 疑惑挑眉：左右眼不对称夸张拟人 (一高挑大，一低微眯)
    // 左眼：抬高好奇挑大眼 (带高光)
    drawEye(EYE_LEFT_X, EYE_CENTER_Y - 8, 34, 50, Colors::CYAN);
    driver_.fillCircle(EYE_LEFT_X - 4, EYE_CENTER_Y - 18, 3, Colors::WHITE);

    // 右眼：微垂眯眼 (困惑思考状)
    drawEye(EYE_RIGHT_X, EYE_CENTER_Y + 8, 30, 18, Colors::DARK_CYAN);
    driver_.fillRect(EYE_RIGHT_X - 12, EYE_CENTER_Y + 6, 24, 4, Colors::CYAN);
}

void FaceEngine::drawDizzyEye(int16_t center_x, int16_t center_y, float angle_rad) {
    // 眩晕转圈：动态旋转阿基米德螺旋蚊香眼
    for (float a = 0.5f; a < 4.8f * M_PI; a += 0.22f) {
        float r = a * 2.3f;
        int16_t px = center_x + static_cast<int16_t>(std::cos(a + angle_rad) * r);
        int16_t py = center_y + static_cast<int16_t>(std::sin(a + angle_rad) * r);
        driver_.fillCircle(px, py, 1, Colors::CYAN);
    }
}

void FaceEngine::drawBootingAnimation() {
    boot_frame_++;

    if (boot_frame_ < 40) {
        // 阶段 1: 赛博朋克同心雷达与中心能量核自检展开
        driver_.drawCircle(64, 74, 14, Colors::DARK_CYAN);
        driver_.drawCircle(64, 74, 28, Colors::NEON_BLUE);
        driver_.drawCircle(64, 74, 42, Colors::DARK_CYAN);

        driver_.drawFastHLine(18, 74, 92, Colors::DARK_CYAN);
        driver_.drawFastVLine(64, 28, 92, Colors::DARK_CYAN);

        float rad = static_cast<float>(boot_frame_) * 0.22f;
        int16_t ex = 64 + static_cast<int16_t>(std::cos(rad) * 40.0f);
        int16_t ey = 74 + static_cast<int16_t>(std::sin(rad) * 40.0f);
        driver_.drawLine(64, 74, ex, ey, Colors::CYAN);

        driver_.fillCircle(64, 74, 3, Colors::WHITE);

        // 底部高科技自检进度条
        driver_.drawFastHLine(24, 134, 80, Colors::DARK_GRAY);
        int16_t pw = static_cast<int16_t>((boot_frame_ * 80) / 40);
        driver_.fillRect(24, 133, pw, 3, Colors::CYAN);
        driver_.fillCircle(20, 134, 2, Colors::CYAN);
        driver_.fillCircle(108, 134, 2, Colors::CYAN);
    } else if (boot_frame_ < 75) {
        // 阶段 2: 能量核平滑分裂演变为双眼并缓缓苏醒睁开
        float t = static_cast<float>(boot_frame_ - 40) / 35.0f;
        int16_t lx = 64 - static_cast<int16_t>(28.0f * t);
        int16_t rx = 64 + static_cast<int16_t>(28.0f * t);
        int16_t w = 14 + static_cast<int16_t>(18.0f * t);
        int16_t h = 4 + static_cast<int16_t>(44.0f * t);

        drawEye(lx, EYE_CENTER_Y, w, h, Colors::CYAN);
        drawEye(rx, EYE_CENTER_Y, w, h, Colors::CYAN);
    } else {
        // 自检完毕，自然切入常态大眼
        current_emotion_ = EmotionState::NORMAL;
        blink_step_ = -1;
    }
}

void FaceEngine::drawThinkingAnimation() {
    // 思考推演：眼神微上扬，伴随科技水平扫描光束
    drawEye(EYE_LEFT_X, EYE_CENTER_Y - 4, 30, 42, Colors::CYAN);
    drawEye(EYE_RIGHT_X, EYE_CENTER_Y - 4, 30, 42, Colors::CYAN);

    int16_t scan_offset = static_cast<int16_t>((breath_counter_ * 3) % 40);
    int16_t scan_y = EYE_CENTER_Y - 22 + scan_offset;
    driver_.drawFastHLine(EYE_LEFT_X - 14, scan_y, 28, Colors::WHITE);
    driver_.drawFastHLine(EYE_RIGHT_X - 14, scan_y, 28, Colors::WHITE);
}

void FaceEngine::drawWinkEyes() {
    // 调皮眨眼：左眼弯弯笑，右眼眯线闪耀星星
    drawHappyEye(EYE_LEFT_X, EYE_CENTER_Y);
    drawEye(EYE_RIGHT_X, EYE_CENTER_Y, 32, 6, Colors::CYAN);

    // 闪耀十字星辉
    driver_.drawLine(EYE_RIGHT_X - 6, EYE_CENTER_Y - 14, EYE_RIGHT_X + 10, EYE_CENTER_Y - 14, Colors::YELLOW);
    driver_.drawLine(EYE_RIGHT_X + 2, EYE_CENTER_Y - 22, EYE_RIGHT_X + 2, EYE_CENTER_Y - 6, Colors::YELLOW);
    driver_.fillCircle(EYE_RIGHT_X + 2, EYE_CENTER_Y - 14, 2, Colors::WHITE);
}

void FaceEngine::drawCoolShades() {
    // 炫酷帅气：赛博墨镜造型，带有镜面反光条
    int16_t y = EYE_CENTER_Y - 10;
    driver_.fillRoundRect(16, y, 96, 26, 6, Colors::DARK_CYAN);
    driver_.fillRoundRect(18, y + 2, 92, 22, 4, Colors::CYAN);

    driver_.drawLine(30, y + 4, 46, y + 20, Colors::WHITE);
    driver_.drawLine(31, y + 4, 47, y + 20, Colors::WHITE);
    driver_.drawLine(76, y + 4, 92, y + 20, Colors::WHITE);
    driver_.drawLine(77, y + 4, 93, y + 20, Colors::WHITE);
}

void FaceEngine::drawSoundwaveVisualizer(float energy) {
    // 底部声波频谱动效：一阶低通平滑 + 对称 9 柱赛博律动声浪
    smooth_energy_ = (smooth_energy_ * 0.70f) + (energy * 0.30f);

    constexpr int16_t BASE_Y = 153;       // 底部基准线 Y
    constexpr int16_t NUM_BARS = 9;       // 9 根对称声柱
    constexpr int16_t BAR_W = 4;          // 每根声柱宽 4 像素
    constexpr int16_t BAR_SPACING = 4;    // 柱间隙 4 像素
    constexpr int16_t TOTAL_W = (NUM_BARS * BAR_W) + ((NUM_BARS - 1) * BAR_SPACING); // 68 像素
    int16_t start_x = 64 - (TOTAL_W / 2); // 水平严格居中

    // 绘制极简赛博基底细线
    driver_.drawFastHLine(20, BASE_Y + 1, 88, Colors::DARK_GRAY);

    for (int i = 0; i < NUM_BARS; i++) {
        int16_t bx = start_x + (i * (BAR_W + BAR_SPACING));
        int dist_center = std::abs(i - 4);  // 距中心声柱的距离 (0~4)

        // 结合平滑声能与正弦相位波动
        float factor = 1.0f - (static_cast<float>(dist_center) * 0.16f);
        float wave = std::sin((breath_counter_ * 0.25f) + (i * 0.8f)) * 1.5f;
        int16_t h = 2 + static_cast<int16_t>(smooth_energy_ * 18.0f * factor + wave);
        if (h < 2) h = 2;
        if (h > 20) h = 20;

        // 根据声浪高度呈现动态变色渐变 (青色 -> 橙色 -> 红色)
        uint16_t color = Colors::CYAN;
        if (h > 15) {
            color = Colors::RED;
        } else if (h > 9) {
            color = Colors::ORANGE;
        } else if (h > 5) {
            color = Colors::NEON_BLUE;
        } else {
            color = Colors::DARK_CYAN;
        }

        // 向上展开绘制实心圆柱
        driver_.fillRect(bx, BASE_Y - h, BAR_W, h, color);
    }
}

void FaceEngine::update() {
    uint32_t now_ticks = xTaskGetTickCount();
    breath_counter_++;

    // 1. 每帧开始：全屏显存清黑
    driver_.clear(Colors::BLACK);

    // 2. 根据当前情绪状态机执行具体双眼绘制
    switch (current_emotion_) {
        case EmotionState::NORMAL: {
            // --- 拟人自然呼吸算法 (每 16 帧产生 1 像素微小轻柔起伏) ---
            int16_t breath_h = DEFAULT_H + ((breath_counter_ / 16) % 2 == 0 ? 0 : 1);

            // --- 拟人自然随机眨眼算法 ---
            int16_t render_h = breath_h;
            if (blink_step_ >= 0) {
                render_h = BLINK_HEIGHTS[blink_step_];
                blink_step_++;
                if (blink_step_ >= BLINK_FRAME_COUNT) {
                    blink_step_ = -1;
                    uint32_t random_ms = 2000 + (std::rand() % 2500);
                    next_blink_tick_ = now_ticks + pdMS_TO_TICKS(random_ms);
                }
            } else if (now_ticks >= next_blink_tick_) {
                blink_step_ = 0;
            }

            drawEye(EYE_LEFT_X, EYE_CENTER_Y, DEFAULT_W, render_h, Colors::CYAN);
            drawEye(EYE_RIGHT_X, EYE_CENTER_Y, DEFAULT_W, render_h, Colors::CYAN);
            break;
        }

        case EmotionState::HAPPY:
            drawHappyEye(EYE_LEFT_X, EYE_CENTER_Y);
            drawHappyEye(EYE_RIGHT_X, EYE_CENTER_Y);
            break;

        case EmotionState::SURPRISED:
            drawEye(EYE_LEFT_X, EYE_CENTER_Y, 36, 44, Colors::CYAN);
            drawEye(EYE_RIGHT_X, EYE_CENTER_Y, 36, 44, Colors::CYAN);
            break;

        case EmotionState::SLEEPY:
            drawEye(EYE_LEFT_X, EYE_CENTER_Y + 12, DEFAULT_W, 8, Colors::DARK_CYAN);
            drawEye(EYE_RIGHT_X, EYE_CENTER_Y + 12, DEFAULT_W, 8, Colors::DARK_CYAN);
            break;

        case EmotionState::LOVE: {
            // 爱心跳动心律脉动微动画 (~60 BPM 拟人律动)
            int16_t pulse = ((breath_counter_ / 6) % 6 < 2) ? 2 : 0;
            drawHeartEye(EYE_LEFT_X, EYE_CENTER_Y, pulse);
            drawHeartEye(EYE_RIGHT_X, EYE_CENTER_Y, pulse);
            break;
        }

        case EmotionState::ANGRY:
            drawAngryEye(EYE_LEFT_X, EYE_CENTER_Y, true);
            drawAngryEye(EYE_RIGHT_X, EYE_CENTER_Y, false);
            break;

        case EmotionState::CONFUSED:
            drawConfusedEyes();
            break;

        case EmotionState::BOOTING:
            drawBootingAnimation();
            break;

        case EmotionState::THINKING:
            drawThinkingAnimation();
            break;

        case EmotionState::WINK:
            drawWinkEyes();
            break;

        case EmotionState::COOL:
            drawCoolShades();
            break;

        case EmotionState::DIZZY: {
            // 动态旋转蚊香圈动画
            float rot = static_cast<float>(breath_counter_) * 0.16f;
            drawDizzyEye(EYE_LEFT_X, EYE_CENTER_Y, rot);
            drawDizzyEye(EYE_RIGHT_X, EYE_CENTER_Y, rot);
            break;
        }
    }

    // 3. 屏幕底部渲染麦克风实时声波频谱动效 (声浪律动，开机自检期间不遮挡进度条)
    if (current_emotion_ != EmotionState::BOOTING) {
        float mic_energy = AudioService::getInstance().getMicEnergy();
        drawSoundwaveVisualizer(mic_energy);
    }

    // 4. 将计算好的整屏显存批量 DMA 推流上屏！
    driver_.flush();
}
