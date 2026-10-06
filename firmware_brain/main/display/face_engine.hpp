/**
 * @file face_engine.hpp
 * @brief 赛博拟人表情引擎算法模型头文件
 *
 * 【职责】：
 *  1. 纯数学与图形渲染模型，负责计算眼睛当前帧的高低宽窄、眨眼状态机与情绪变换；
 *  2. 扩展丰富赛博表情库：正常 (NORMAL)、开心 (HAPPY)、惊讶 (SURPRISED)、
 *     困倦 (SLEEPY)、爱心心动 (LOVE)、生气戒备 (ANGRY)、疑惑挑眉 (CONFUSED)、眩晕转圈 (DIZZY)；
 *  3. 集成屏底赛博动态频谱声浪 (Audio Soundwave Visualizer)，实时随环境声能跳跃共振。
 */
#pragma once

#include <cstdint>

#include "display/st7735.hpp"

// 强类型情绪枚举 (强类型 enum class 避免命名污染)
enum class EmotionState {
    BOOTING,    // 开机自检科技动画 (光圈雷达展开与双眼苏醒)
    NORMAL,     // 正常状态 (赛博大眼 + 自然呼吸 + 随机眨眼)
    HAPPY,      // 开心状态 (月牙微笑弯弯眼)
    SURPRISED,  // 惊讶状态 (瞳孔放大圆形大眼)
    SLEEPY,     // 困倦状态 (半闭眼睑呼吸)
    LOVE,       // 爱心心动 (跳动粉色爱心眼)
    ANGRY,      // 生气戒备 (红色斜切怒火眼)
    CONFUSED,   // 疑惑挑眉 (左眼挑高挑起，右眼微眯疑惑)
    DIZZY,      // 眩晕转圈 (动态旋转螺旋圈)
    THINKING,   // 思考状态 (眼神微上扬与科技扫描波)
    WINK,       // 调皮眨单眼 (左眼弯弯笑，右眼闪烁眨星)
    COOL        // 炫酷帅气 (赛博墨镜造型)
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

    /**
     * @brief 获取当前情绪
     */
    EmotionState getEmotion() const { return current_emotion_; }

private:
    // 基础单眼绘制辅助函数
    void drawEye(int16_t center_x, int16_t center_y, int16_t w, int16_t h, uint16_t color);

    // 绘制开心月牙眼
    void drawHappyEye(int16_t center_x, int16_t center_y);

    // 绘制跳动粉红爱心眼
    void drawHeartEye(int16_t center_x, int16_t center_y, int16_t pulse);

    // 绘制生气锐角斜眉怒眼
    void drawAngryEye(int16_t center_x, int16_t center_y, bool is_left);

    // 绘制疑惑不平衡挑眉大眼
    void drawConfusedEyes();

    // 绘制动态旋转螺旋蚊香眼
    void drawDizzyEye(int16_t center_x, int16_t center_y, float angle_rad);

    // 绘制开机科技感自检扫描动画
    void drawBootingAnimation();

    // 绘制思考推演动效
    void drawThinkingAnimation();

    // 绘制调皮眨单眼
    void drawWinkEyes();

    // 绘制炫酷赛博墨镜
    void drawCoolShades();

    // 绘制屏幕底部声波频谱动效 (声浪律动)
    void drawSoundwaveVisualizer(float energy);

    ST7735Driver &driver_;  // 屏幕驱动引用
    EmotionState current_emotion_{EmotionState::BOOTING};  // 初始启动为开机动画

    // 眨眼状态机变量
    int blink_step_{-1};           // -1 表示睁眼状态，>=0 表示正在执行眨眼帧序列
    uint32_t next_blink_tick_{0};  // 下一次眨眼的时间戳 (FreeRTOS Ticks)
    uint32_t breath_counter_{0};   // 呼吸/动画帧计数器
    uint32_t boot_frame_{0};       // 开机自检动画帧计数器
    float smooth_energy_{0.0f};    // 底部声浪滤波平滑能量
};
