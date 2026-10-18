#include <algorithm>
#include <cstring>

#include "board.hpp"
#include "diagnostics.hpp"
#include "head_display.hpp"
#include "ui/face_engine.hpp"
#include "ui/head_controller.hpp"
extern "C" {
volatile HeadDiagnostics g_head_diag{};
volatile uint8_t g_swd_frame[HP_MAX_FRAME]{};
volatile uint32_t g_swd_length = 0;
volatile uint8_t g_swd_reply[HP_MAX_FRAME]{};
volatile uint32_t g_swd_reply_length = 0;
volatile uint32_t g_lcd_test_request = 0;
volatile uint32_t g_lcd_test_active = 0;
volatile uint32_t g_lcd_transfer_bytes = 0, g_lcd_changed_tiles = 0;
}
namespace {
struct BootRecord {
    uint32_t magic, count, last_fault;
};
__attribute__((section(".noinit"))) volatile BootRecord boot_record;
HeadDisplay display;
FaceEngine face(display);
HeadController head;
HpParser uart_parser{}, swd_parser{};
uint8_t status_sequence = 0, last_sequence = 0;
uint32_t last_status_ms = 0, camera_tick = 0;
uint8_t camera_stage = 0;  // 0=休眠/复位，1=等待复位释放，2=等待传感器起振，3=可用。
bool in_service = false;
bool have_state = false, restart_boot_requested = false;
void reply(uint8_t type, uint8_t sequence, const uint8_t *payload, uint8_t length, bool swd) {
    uint8_t packet[HP_MAX_FRAME];
    size_t n = hp_encode(type, sequence, payload, length, packet);
    if (swd) {
        g_swd_reply_length = 0;
        for (size_t i = 0; i < n; ++i)
            g_swd_reply[i] = packet[i];
        __DMB();
        g_swd_reply_length = n;
    } else
        Board::uartWrite(packet, n);
}
void sendStatus(uint8_t sequence, bool swd) {
    HpStatus s{};
    s.uptime_ms = Board::millis();
    s.frames = g_head_diag.frame_count;
    s.valid_frames = uart_parser.valid_frames + swd_parser.valid_frames;
    s.crc_errors = uart_parser.crc_errors + swd_parser.crc_errors;
    s.uart_overruns = Board::uartErrors();
    s.pan_cdeg = head.pan();
    s.tilt_cdeg = head.tilt();
    s.target_pan_cdeg = head.targetPan();
    s.target_tilt_cdeg = head.targetTilt();
    s.event_id = head.eventId();
    s.emotion = static_cast<uint8_t>(face.getEmotion());
    s.last_sequence = last_sequence;
    s.firmware_version = 0x0100;
    s.flags = (head.enabled() ? HP_SERVO_ENABLE : 0) |
              (head.online(Board::millis()) ? HP_LINK_ONLINE : 0) |
              (camera_stage == 3 ? HP_CAMERA_READY : 0) | (Board::hseClock() ? HP_CLOCK_HSE : 0);
    uint8_t payload[HP_STATUS_SIZE];
    hp_status_pack(payload, &s);
    reply(HP_STATUS, sequence, payload, sizeof(payload), swd);
}
void onFrame(const HpFrame *frame, void *context) {
    bool swd = context != nullptr;
    uint8_t result = HP_OK;
    uint32_t now = Board::millis();
    if (frame->type == HP_PING) {
        if (frame->length)
            result = HP_BAD_LENGTH;
        else {
            sendStatus(frame->sequence, swd);
            return;
        }
    } else if (frame->type == HP_STATE) {
        HpState s{};
        bool new_boot_event = false;
        if (!hp_state_unpack(frame, &s))
            result = HP_BAD_LENGTH;
        else {
            // BOOT动画播放后会自行转NORMAL，但期望状态可能仍是BOOT。
            // 新事件须重新播放；同event_id的心跳不能每100ms重置动画。
            new_boot_event = s.emotion == HP_BOOTING &&
                             (!have_state || s.event_id != head.eventId() ||
                              head.emotion() != HP_BOOTING);
            if (!head.apply(s, now))
                result = HP_BAD_VALUE;
            else {
                restart_boot_requested |= new_boot_event;
                have_state = true;
                ++g_head_diag.commands_applied;
                last_sequence = frame->sequence;
            }
        }
    } else if (frame->type == HP_CAMERA) {
        if (frame->length != 1)
            result = HP_BAD_LENGTH;
        else if (frame->payload[0] > 1)
            result = HP_BAD_VALUE;
        else {
            if (frame->payload[0]) {
                // 已经唤醒时的重发只回ACK，不重复把正在采集的摄像头复位。
                if (!camera_stage) {
                    Board::camera(false, false);
                    camera_stage = 1;
                    camera_tick = now;
                }
            } else {
                Board::camera(false, true);
                camera_stage = 0;
            }
            ++g_head_diag.commands_applied;
        }
    } else
        result = HP_BAD_TYPE;
    if (result != HP_OK)
        ++g_head_diag.commands_rejected;
    uint8_t ack[] = {frame->type, result};
    reply(HP_ACK, frame->sequence, ack, sizeof(ack), swd);
}
void service() {
    if (in_service)
        return;
    in_service = true;
    Board::watchdogKick();
    uint32_t now = Board::millis();
    uint8_t byte;
    unsigned budget = 256;
    while (budget-- && Board::uartRead(byte))
        hp_parser_feed(&uart_parser, byte, now, onFrame, nullptr);
    if (g_swd_length) {
        uint32_t n = g_swd_length;
        if (n <= HP_MAX_FRAME) {
            for (uint32_t i = 0; i < n; ++i)
                hp_parser_feed(&swd_parser, g_swd_frame[i], now, onFrame,
                               reinterpret_cast<void *>(1));
        } else
            ++g_head_diag.commands_rejected;
        __DMB();
        g_swd_length = 0;
    }
    head.tick(now);
    Board::servo(head.enabled(), HeadController::pulseUs(head.pan()),
                 HeadController::pulseUs(head.tilt()));
    if (camera_stage == 1 && (uint32_t)(now - camera_tick) >= 10) {
        Board::camera(true, false);
        camera_stage = 2;
        camera_tick = now;
    } else if (camera_stage == 2 && (uint32_t)(now - camera_tick) >= 20)
        camera_stage = 3;
    if ((uint32_t)(now - last_status_ms) >= 200) {
        last_status_ms = now;
        sendStatus(status_sequence++, false);
    }
    Board::led((now % 1000) < (head.online(now) ? 100u : 500u));
    g_head_diag.uptime_ms = now;
    g_head_diag.uart_rx_bytes = Board::rxBytes();
    g_head_diag.uart_tx_bytes = Board::txBytes();
    g_head_diag.uart_errors = Board::uartErrors();
    g_head_diag.tx_drops = Board::txDrops();
    g_head_diag.uart_valid_frames = uart_parser.valid_frames;
    g_head_diag.swd_valid_frames = swd_parser.valid_frames;
    g_head_diag.crc_errors = uart_parser.crc_errors + swd_parser.crc_errors;
    g_head_diag.header_errors = uart_parser.rejected_headers + swd_parser.rejected_headers;
    g_head_diag.pan_cdeg = head.pan();
    g_head_diag.tilt_cdeg = head.tilt();
    g_head_diag.target_pan_cdeg = head.targetPan();
    g_head_diag.target_tilt_cdeg = head.targetTilt();
    g_head_diag.servo_enabled = head.enabled();
    g_head_diag.link_online = head.online(now);
    g_head_diag.emotion = static_cast<uint8_t>(face.getEmotion());
    g_head_diag.energy = head.energy();
    g_head_diag.event_id = head.eventId();
    g_head_diag.camera_ready = camera_stage == 3;
    g_head_diag.pwm_pan_us = TIM2->CCR1;
    g_head_diag.pwm_tilt_us = TIM2->CCR2;
    g_head_diag.last_sequence = last_sequence;
    g_head_diag.gpioa_idr = GPIOA->IDR;
    g_head_diag.gpiob_idr = GPIOB->IDR;
    ++g_head_diag.loop_count;
    in_service = false;
}
}  // namespace
extern "C" void HardFault_Handler() {
    TIM2->CCER = 0;
    boot_record.last_fault = SCB->CFSR ? SCB->CFSR : 0xffffffffu;
    // 不喂看门狗：复位后保留错误码，并保持舵机关闭。
    while (true) {
    }
}
extern "C" void __cxa_pure_virtual() {
    HardFault_Handler();
}
int main() {
    if (boot_record.magic != 0x424f4f54) {
        boot_record.magic = 0x424f4f54;
        boot_record.count = 0;
        boot_record.last_fault = 0;
    }
    ++boot_record.count;
    g_head_diag.magic = 0x48454144;
    g_head_diag.version = 0x0100;
    g_head_diag.boot_count = boot_record.count;
    g_head_diag.last_fault = boot_record.last_fault;
    g_head_diag.reset_flags = RCC->CSR;
    RCC->CSR |= RCC_CSR_RMVF;
    Board::init();
    g_head_diag.core_hz = SystemCoreClock;
    Board::setServiceHook(service);
    bool ready = display.init();
    if (!ready)
        ++g_head_diag.spi_errors;
    uint32_t last_frame = Board::millis();
    EmotionState previous = EmotionState::BOOTING;
    while (true) {
        service();
        uint32_t lcd_test_request = g_lcd_test_request;
        if (lcd_test_request) {
            g_lcd_test_request = 0;
            if (lcd_test_request <= 2) {
                Board::lcdDiagnosticMode(lcd_test_request == 1);
                if (!display.init())
                    ++g_head_diag.spi_errors;
                g_lcd_test_active = lcd_test_request == 1;
            }
        }
        uint32_t now = Board::millis();
        // 目标20 FPS；flush仅发送变化的4x4块，整屏切换仍受1.125MHz带宽限制。
        // flush逐行调用service，刷屏期间仍处理串口、心跳和舵机控制。
        if ((uint32_t)(now - last_frame) >= 50) {
            last_frame = now;
            auto emotion = static_cast<EmotionState>(head.emotion());
            if (emotion != previous || restart_boot_requested) {
                face.setEmotion(emotion);
                previous = emotion;
                restart_boot_requested = false;
            }
            if (g_lcd_test_active) {
                display.clear(Colors::BLACK);
                display.fillRect(0, 0, 43, HeadDisplay::HEIGHT, Colors::RED);
                display.fillRect(43, 0, 42, HeadDisplay::HEIGHT, Colors::GREEN);
                display.fillRect(85, 0, 43, HeadDisplay::HEIGHT, Colors::BLUE);
                display.fillRect(0, 0, HeadDisplay::WIDTH, 8, Colors::WHITE);
            } else
                face.update(now, static_cast<float>(head.energy()) / 255.0f);
            if (!display.flush())
                ++g_head_diag.spi_errors;
            g_lcd_transfer_bytes = display.lastTransferBytes();
            g_lcd_changed_tiles = display.lastChangedTiles();
            ++g_head_diag.frame_count;
            g_head_diag.frame_time_ms = Board::millis() - now;
            g_head_diag.max_frame_ms =
                std::max(g_head_diag.max_frame_ms, g_head_diag.frame_time_ms);
        }
        __WFI();  // 被1ms SysTick或UART唤醒，不空转占满CPU。
    }
}
