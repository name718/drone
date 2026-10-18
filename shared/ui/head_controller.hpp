/** @file head_controller.hpp @brief 纯逻辑状态机；主机单测和 C8T6 使用同一实现。 */
#pragma once
#include <cstdint>

#include "head_protocol.h"
namespace HeadLimits {
constexpr uint16_t PAN_HOME = 2900, TILT_HOME = 0;
// 依据旧程序已使用的动作范围设置保守界限，单位 0.01°；扩展行程前先校准机构。
constexpr uint16_t PAN_MIN = 900, PAN_MAX = 5200, TILT_MIN = 0, TILT_MAX = 2500;
constexpr uint32_t LINK_TIMEOUT_MS = 1000, SERVO_PERIOD_MS = 20;
}  // namespace HeadLimits
class HeadController {
public:
    bool apply(const HpState &state, uint32_t now);
    void tick(uint32_t now);
    bool online(uint32_t now) const;
    bool enabled() const { return enabled_; }
    uint16_t pan() const { return pan_; }
    uint16_t tilt() const { return tilt_; }
    uint16_t targetPan() const { return target_pan_; }
    uint16_t targetTilt() const { return target_tilt_; }
    uint16_t eventId() const { return event_id_; }
    uint8_t emotion() const { return emotion_; }
    uint8_t energy() const { return energy_; }
    static uint16_t pulseUs(uint16_t angle_cdeg);

private:
    uint16_t pan_ = HeadLimits::PAN_HOME, tilt_ = HeadLimits::TILT_HOME;
    uint16_t target_pan_ = HeadLimits::PAN_HOME, target_tilt_ = HeadLimits::TILT_HOME;
    uint16_t event_id_ = 0;
    uint32_t last_state_ms_ = 0, last_step_ms_ = 0, gesture_started_ms_ = 0;
    uint8_t emotion_ = HP_BOOTING, energy_ = 0, gesture_ = HP_GESTURE_NONE;
    bool seen_ = false, enabled_ = false, have_event_ = false;
};
