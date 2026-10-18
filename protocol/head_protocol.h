/**
 * @file head_protocol.h
 * @brief ESP32 / Luckfox / C8T6 共用的头部协议。只依赖 C 标准库。
 *
 * 字节序固定为小端，禁止直接发送 C struct（填充/对齐会随平台改变）。
 * 帧格式：A5 5A | version | type | sequence | length | payload | CRC16_LE。
 * CRC-16/CCITT-FALSE 覆盖 version 至 payload，初值 FFFF，多项式 1021。
 */
#ifndef HEAD_PROTOCOL_H
#define HEAD_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
#define HP_VERSION 1u
#define HP_MAX_PAYLOAD 64u
#define HP_MAX_FRAME (HP_MAX_PAYLOAD + 8u)
#define HP_STATE_SIZE 10u
#define HP_STATUS_SIZE 36u
#define HP_GAP_TIMEOUT_MS 100u
enum { HP_PING = 1, HP_STATE = 0x10, HP_CAMERA = 0x11, HP_STATUS = 0x80, HP_ACK = 0x81 };
enum {
    HP_BOOTING,
    HP_NORMAL,
    HP_HAPPY,
    HP_SURPRISED,
    HP_SLEEPY,
    HP_LOVE,
    HP_ANGRY,
    HP_CONFUSED,
    HP_DIZZY,
    HP_THINKING,
    HP_WINK,
    HP_COOL,
    HP_EMOTION_COUNT
};
enum { HP_GESTURE_NONE, HP_GESTURE_NOD, HP_GESTURE_SHAKE, HP_GESTURE_RESET };
enum { HP_SERVO_ENABLE = 1u, HP_LINK_ONLINE = 2u, HP_CAMERA_READY = 4u, HP_CLOCK_HSE = 8u };
enum { HP_OK, HP_BAD_LENGTH, HP_BAD_VALUE, HP_BAD_TYPE };
typedef struct {
    uint8_t type, sequence, length, payload[HP_MAX_PAYLOAD];
} HpFrame;
typedef struct {
    uint8_t emotion, energy, gesture, flags;
    uint16_t pan_cdeg, tilt_cdeg, event_id;
} HpState;
typedef struct {
    uint32_t uptime_ms, frames, valid_frames, crc_errors, uart_overruns;
    uint16_t pan_cdeg, tilt_cdeg, target_pan_cdeg, target_tilt_cdeg, event_id;
    uint8_t emotion, flags, last_sequence, reserved;
    uint16_t firmware_version;
} HpStatus;
typedef struct {
    uint8_t buffer[HP_MAX_FRAME];
    uint8_t used;
    uint32_t last_byte_ms, valid_frames, crc_errors, rejected_headers;
} HpParser;
typedef void (*HpFrameCallback)(const HpFrame *, void *);
static inline uint16_t hp_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static inline uint32_t hp_u32(const uint8_t *p) {
    return (uint32_t)hp_u16(p) | ((uint32_t)hp_u16(p + 2) << 16);
}
static inline void hp_put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static inline void hp_put32(uint8_t *p, uint32_t v) {
    hp_put16(p, (uint16_t)v);
    hp_put16(p + 2, (uint16_t)(v >> 16));
}
static inline uint16_t hp_crc16(const uint8_t *p, size_t n) {
    uint16_t crc = 0xffff;
    while (n--) {
        crc ^= (uint16_t)*p++ << 8;
        for (unsigned b = 0; b < 8; b++)
            crc = (uint16_t)((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    return crc;
}
static inline size_t hp_encode(uint8_t type, uint8_t seq, const uint8_t *payload, uint8_t len,
                               uint8_t *out) {
    if (len > HP_MAX_PAYLOAD || (len && !payload))
        return 0;
    out[0] = 0xa5;
    out[1] = 0x5a;
    out[2] = HP_VERSION;
    out[3] = type;
    out[4] = seq;
    out[5] = len;
    if (len)
        memcpy(out + 6, payload, len);
    hp_put16(out + 6 + len, hp_crc16(out + 2, 4u + len));
    return 8u + len;
}
static inline void hp_state_pack(uint8_t *p, const HpState *s) {
    p[0] = s->emotion;
    p[1] = s->energy;
    p[2] = s->gesture;
    p[3] = s->flags;
    hp_put16(p + 4, s->pan_cdeg);
    hp_put16(p + 6, s->tilt_cdeg);
    hp_put16(p + 8, s->event_id);
}
static inline bool hp_state_unpack(const HpFrame *f, HpState *s) {
    if (f->length != HP_STATE_SIZE)
        return false;
    const uint8_t *p = f->payload;
    s->emotion = p[0];
    s->energy = p[1];
    s->gesture = p[2];
    s->flags = p[3];
    s->pan_cdeg = hp_u16(p + 4);
    s->tilt_cdeg = hp_u16(p + 6);
    s->event_id = hp_u16(p + 8);
    return true;
}
static inline void hp_status_pack(uint8_t *p, const HpStatus *s) {
    hp_put32(p, s->uptime_ms);
    hp_put32(p + 4, s->frames);
    hp_put32(p + 8, s->valid_frames);
    hp_put32(p + 12, s->crc_errors);
    hp_put32(p + 16, s->uart_overruns);
    hp_put16(p + 20, s->pan_cdeg);
    hp_put16(p + 22, s->tilt_cdeg);
    hp_put16(p + 24, s->target_pan_cdeg);
    hp_put16(p + 26, s->target_tilt_cdeg);
    hp_put16(p + 28, s->event_id);
    p[30] = s->emotion;
    p[31] = s->flags;
    p[32] = s->last_sequence;
    p[33] = 0;
    hp_put16(p + 34, s->firmware_version);
}
static inline bool hp_status_unpack(const HpFrame *f, HpStatus *s) {
    if (f->length != HP_STATUS_SIZE)
        return false;
    const uint8_t *p = f->payload;
    s->uptime_ms = hp_u32(p);
    s->frames = hp_u32(p + 4);
    s->valid_frames = hp_u32(p + 8);
    s->crc_errors = hp_u32(p + 12);
    s->uart_overruns = hp_u32(p + 16);
    s->pan_cdeg = hp_u16(p + 20);
    s->tilt_cdeg = hp_u16(p + 22);
    s->target_pan_cdeg = hp_u16(p + 24);
    s->target_tilt_cdeg = hp_u16(p + 26);
    s->event_id = hp_u16(p + 28);
    s->emotion = p[30];
    s->flags = p[31];
    s->last_sequence = p[32];
    s->reserved = p[33];
    s->firmware_version = hp_u16(p + 34);
    return true;
}
/** 丢一字节后重新搜索，而非清空整包：损坏帧内部也可能包含下一帧的起点。 */
static inline void hp_parser_drop(HpParser *p, unsigned n) {
    p->used = (uint8_t)(p->used - n);
    memmove(p->buffer, p->buffer + n, p->used);
}
static inline void hp_parser_feed(HpParser *p, uint8_t byte, uint32_t now, HpFrameCallback callback,
                                  void *ctx) {
    if (p->used && (uint32_t)(now - p->last_byte_ms) > HP_GAP_TIMEOUT_MS)
        p->used = 0;
    p->last_byte_ms = now;
    if (p->used == HP_MAX_FRAME)
        hp_parser_drop(p, 1);
    p->buffer[p->used++] = byte;
    while (p->used >= 2) {
        if (p->buffer[0] != 0xa5 || p->buffer[1] != 0x5a) {
            hp_parser_drop(p, 1);
            continue;
        }
        if (p->used < 6)
            break;
        if (p->buffer[2] != HP_VERSION || p->buffer[5] > HP_MAX_PAYLOAD) {
            ++p->rejected_headers;
            hp_parser_drop(p, 1);
            continue;
        }
        unsigned total = 8u + p->buffer[5];
        if (p->used < total)
            break;
        if (hp_u16(p->buffer + total - 2) != hp_crc16(p->buffer + 2, total - 4)) {
            ++p->crc_errors;
            hp_parser_drop(p, 1);
            continue;
        }
        HpFrame frame = {0, 0, 0, {0}};
        frame.type = p->buffer[3];
        frame.sequence = p->buffer[4];
        frame.length = p->buffer[5];
        if (frame.length)
            memcpy(frame.payload, p->buffer + 6, frame.length);
        ++p->valid_frames;
        hp_parser_drop(p, total);
        callback(&frame, ctx);
    }
}
#ifdef __cplusplus
}
#endif
#endif
