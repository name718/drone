#include "comm/head_link.hpp"

#include <algorithm>
#include <cmath>

#include "audio/audio_service.hpp"
#include "config/board_config.hpp"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ui/head_controller.hpp"
namespace {
constexpr uart_port_t PORT = UART_NUM_2;
constexpr uint32_t PERIOD_MS = 100;
uint32_t nowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}
uint16_t angle(float degree, uint16_t min, uint16_t max) {
    if (!std::isfinite(degree))
        return min;
    return static_cast<uint16_t>(
        std::clamp(degree * 100.0f, static_cast<float>(min), static_cast<float>(max)));
}
}  // namespace
HeadLink &HeadLink::getInstance() {
    static HeadLink instance;
    return instance;
}
esp_err_t HeadLink::init() {
    if (initialized_)
        return ESP_OK;
    uart_config_t config{};
    config.baud_rate = Config::HeadCom::BAUD_RATE;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    esp_err_t err = uart_param_config(PORT, &config);
    if (err != ESP_OK)
        return err;
    err = uart_set_pin(PORT, Config::HeadCom::PIN_TX, Config::HeadCom::PIN_RX, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
        return err;
    err = uart_driver_install(PORT, 2048, 0, 0, nullptr, 0);
    if (err != ESP_OK)
        return err;
    initialized_ = true;
    ESP_LOGI("头部通信", "UART2: TX=%d RX=%d %lu baud，经Luckfox转发", Config::HeadCom::PIN_TX,
             Config::HeadCom::PIN_RX, static_cast<unsigned long>(Config::HeadCom::BAUD_RATE));
    return ESP_OK;
}
esp_err_t HeadLink::start() {
    if (running_)
        return ESP_OK;
    esp_err_t err = init();
    if (err != ESP_OK)
        return err;
    running_ = true;
    if (xTaskCreatePinnedToCore(task, "HeadLink", 4096, this, 5, &task_handle_, 1) != pdPASS) {
        running_ = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
void HeadLink::setEmotion(uint8_t emotion) {
    if (emotion >= HP_EMOTION_COUNT)
        return;
    portENTER_CRITICAL(&lock_);
    desired_.emotion = emotion;
    portEXIT_CRITICAL(&lock_);
}
void HeadLink::lookAt(float pan, float tilt) {
    if (!std::isfinite(pan) || !std::isfinite(tilt))
        return;
    portENTER_CRITICAL(&lock_);
    desired_.pan_cdeg = angle(pan, HeadLimits::PAN_MIN, HeadLimits::PAN_MAX);
    desired_.tilt_cdeg = angle(tilt, HeadLimits::TILT_MIN, HeadLimits::TILT_MAX);
    desired_.gesture = HP_GESTURE_NONE;
    ++desired_.event_id;
    portEXIT_CRITICAL(&lock_);
}
void HeadLink::gesture(uint8_t action) {
    if (action > HP_GESTURE_RESET)
        return;
    portENTER_CRITICAL(&lock_);
    desired_.gesture = action;
    ++desired_.event_id;
    if (action == HP_GESTURE_RESET) {
        desired_.pan_cdeg = HeadLimits::PAN_HOME;
        desired_.tilt_cdeg = HeadLimits::TILT_HOME;
    }
    portEXIT_CRITICAL(&lock_);
}
void HeadLink::enableServos(bool enable) {
    portENTER_CRITICAL(&lock_);
    desired_.flags = enable ? HP_SERVO_ENABLE : 0;
    portEXIT_CRITICAL(&lock_);
}
void HeadLink::requestCamera(bool awake) {
    portENTER_CRITICAL(&lock_);
    camera_awake_ = awake;
    portEXIT_CRITICAL(&lock_);
}
HpState HeadLink::desired() {
    portENTER_CRITICAL(&lock_);
    HpState s = desired_;
    portEXIT_CRITICAL(&lock_);
    return s;
}
HpStatus HeadLink::status() {
    portENTER_CRITICAL(&lock_);
    HpStatus s = status_;
    portEXIT_CRITICAL(&lock_);
    return s;
}
uint32_t HeadLink::statusAgeMs() {
    portENTER_CRITICAL(&lock_);
    uint32_t age = have_status_ ? nowMs() - received_ms_ : UINT32_MAX;
    portEXIT_CRITICAL(&lock_);
    return age;
}
bool HeadLink::online() {
    // 收到STATUS只能证明C8→ESP方向通了。必须同时确认C8最近收到了
    // STATE心跳，才能把ESP→C8控制方向也判为在线，避免单向断线误报。
    portENTER_CRITICAL(&lock_);
    bool connected = have_status_ &&
                     static_cast<uint32_t>(nowMs() - received_ms_) < HeadLimits::LINK_TIMEOUT_MS &&
                     (status_.flags & HP_LINK_ONLINE) != 0;
    portEXIT_CRITICAL(&lock_);
    return connected;
}
void HeadLink::received(const HpFrame *frame, void *context) {
    auto *self = static_cast<HeadLink *>(context);
    HpStatus s{};
    if (frame->type != HP_STATUS || !hp_status_unpack(frame, &s) ||
        (s.firmware_version >> 8) != HP_VERSION)
        return;
    portENTER_CRITICAL(&self->lock_);
    self->status_ = s;
    self->have_status_ = true;
    self->received_ms_ = nowMs();
    // C8确认接收后清除待重放手势，防止其重启后重播已经执行过的动作。
    if (s.event_id == self->desired_.event_id)
        self->desired_.gesture = HP_GESTURE_NONE;
    portEXIT_CRITICAL(&self->lock_);
}
void HeadLink::task(void *context) {
    auto *self = static_cast<HeadLink *>(context);
    HpParser parser{};
    uint8_t sequence = 0, input[128], packet[HP_MAX_FRAME], payload[HP_STATE_SIZE];
    uint32_t last_state = nowMs() - PERIOD_MS, last_camera = nowMs() - 1000;
    bool last_camera_state = false;
    bool was_online = false;
    while (true) {
        int n = uart_read_bytes(PORT, input, sizeof(input), pdMS_TO_TICKS(10));
        uint32_t now = nowMs();
        for (int i = 0; i < n; ++i)
            hp_parser_feed(&parser, input[i], now, received, self);
        if (static_cast<uint32_t>(now - last_state) >= PERIOD_MS) {
            last_state = now;
            HpState state = self->desired();
            float energy = AudioService::getInstance().getMicEnergy();
            state.energy = std::isfinite(energy)
                               ? static_cast<uint8_t>(std::clamp(energy, 0.0f, 1.0f) * 255)
                               : 0;
            hp_state_pack(payload, &state);
            size_t size = hp_encode(HP_STATE, sequence++, payload, sizeof(payload), packet);
            uart_write_bytes(PORT, packet, size);
        }
        portENTER_CRITICAL(&self->lock_);
        bool camera = self->camera_awake_;
        portEXIT_CRITICAL(&self->lock_);
        if (camera != last_camera_state || static_cast<uint32_t>(now - last_camera) >= 1000) {
            uint8_t awake = camera;
            size_t size = hp_encode(HP_CAMERA, sequence++, &awake, 1, packet);
            uart_write_bytes(PORT, packet, size);
            last_camera = now;
            last_camera_state = camera;
        }
        bool connected = self->online();
        if (connected != was_online) {
            ESP_LOGI("头部通信", "头部控制链路%s（同时检查回传新鲜度与C8心跳）",
                     connected ? "在线" : "离线");
            was_online = connected;
        }
    }
}
