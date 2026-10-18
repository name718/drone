/** @file diagnostics.hpp @brief SWD 可读诊断；所有字段32位，方便学习和自动化读取。 */
#pragma once
#include <cstdint>

#include "head_protocol.h"
struct HeadDiagnostics {
    uint32_t magic, version, uptime_ms, boot_count, reset_flags, core_hz;
    uint32_t frame_count, frame_time_ms, max_frame_ms, spi_errors;
    uint32_t uart_rx_bytes, uart_tx_bytes, uart_errors, tx_drops;
    uint32_t uart_valid_frames, swd_valid_frames, crc_errors, header_errors;
    uint32_t commands_applied, commands_rejected;
    uint32_t pan_cdeg, tilt_cdeg, target_pan_cdeg, target_tilt_cdeg;
    uint32_t servo_enabled, link_online, emotion, energy, event_id, camera_ready;
    uint32_t pwm_pan_us, pwm_tilt_us, loop_count, last_fault, last_sequence, gpioa_idr, gpiob_idr;
};
extern "C" {
extern volatile HeadDiagnostics g_head_diag;
// 调试入口走与UART相同的解帧/校验/命令逻辑，但使用独立解析器及计数。
// SWD写入帧内容后最后写length；此入口不能证明物理UART接线已通。
extern volatile uint8_t g_swd_frame[HP_MAX_FRAME];
extern volatile uint32_t g_swd_length;
extern volatile uint8_t g_swd_reply[HP_MAX_FRAME];
extern volatile uint32_t g_swd_reply_length;
// 1=低速软件SPI+三色条，2=恢复硬件SPI+正常表情；只供显示故障排查。
extern volatile uint32_t g_lcd_test_request;
extern volatile uint32_t g_lcd_test_active;
// 最近一帧的SPI字节数（含窗口命令）及变化块数；用于比较差量刷新的实际收益。
extern volatile uint32_t g_lcd_transfer_bytes, g_lcd_changed_tiles;
}
