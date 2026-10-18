#include "ui/head_controller.hpp"

#include <algorithm>
namespace {
uint16_t approach(uint16_t current, uint16_t target) {
    int d = static_cast<int>(target) - current;
    // 原 0.18 一阶低通改为定点计算，再限制每20ms最多1.5°，减小突变。
    int step = d * 18 / 100;
    if (d && !step)
        step = d > 0 ? 1 : -1;
    step = std::max(-150, std::min(150, step));
    return static_cast<uint16_t>(current + step);
}
}  // namespace
bool HeadController::apply(const HpState &s, uint32_t now) {
    if (s.emotion >= HP_EMOTION_COUNT || s.gesture > HP_GESTURE_RESET ||
        (s.flags & ~HP_SERVO_ENABLE) || s.pan_cdeg > 18000 || s.tilt_cdeg > 18000)
        return false;
    last_state_ms_ = now;
    seen_ = true;
    emotion_ = s.emotion;
    energy_ = s.energy;
    const bool fresh_event = !have_event_ || s.event_id != event_id_;
    // 心跳中重复发送同一个 event_id，不得反复启动点头/摇头。
    if (fresh_event) {
        have_event_ = true;
        event_id_ = s.event_id;
        gesture_ = s.gesture;
        gesture_started_ms_ = now;
        target_pan_ = std::max(HeadLimits::PAN_MIN, std::min(HeadLimits::PAN_MAX, s.pan_cdeg));
        target_tilt_ = std::max(HeadLimits::TILT_MIN, std::min(HeadLimits::TILT_MAX, s.tilt_cdeg));
        if (gesture_ == HP_GESTURE_RESET) {
            target_pan_ = HeadLimits::PAN_HOME;
            target_tilt_ = HeadLimits::TILT_HOME;
            gesture_ = HP_GESTURE_NONE;
        }
    }
    enabled_ = (s.flags & HP_SERVO_ENABLE) != 0;
    return true;
}
bool HeadController::online(uint32_t now) const {
    return seen_ && (uint32_t)(now - last_state_ms_) < HeadLimits::LINK_TIMEOUT_MS;
}
void HeadController::tick(uint32_t now) {
    if (!online(now)) {
        enabled_ = false;
        gesture_ = HP_GESTURE_NONE;
        energy_ = 0;
        return;
    }
    if ((uint32_t)(now - last_step_ms_) < HeadLimits::SERVO_PERIOD_MS)
        return;
    // 不补跑积压的周期：长刷屏/暂停调试后也不连续跳很多步。
    last_step_ms_ = now;
    if (!enabled_) {
        gesture_ = HP_GESTURE_NONE;
        return;
    }
    if (gesture_ == HP_GESTURE_NOD) {
        static constexpr uint16_t pose[] = {0, 2500, 500, 1800, 0};
        uint32_t step = (uint32_t)(now - gesture_started_ms_) / 200;
        if (step < 5)
            target_tilt_ = pose[step];
        else
            gesture_ = HP_GESTURE_NONE;
    } else if (gesture_ == HP_GESTURE_SHAKE) {
        static constexpr uint16_t pose[] = {2900, 900, 5200, 1700, 2900};
        uint32_t step = (uint32_t)(now - gesture_started_ms_) / 180;
        if (step < 5)
            target_pan_ = pose[step];
        else
            gesture_ = HP_GESTURE_NONE;
    }
    pan_ = approach(pan_, target_pan_);
    tilt_ = approach(tilt_, target_tilt_);
}
uint16_t HeadController::pulseUs(uint16_t angle) {
    return static_cast<uint16_t>(
        500u + (static_cast<uint32_t>(std::min<uint16_t>(18000, angle)) * 2000u + 9000u) / 18000u);
}
