// 主机测试验证真实协议解析、动作状态机和全部表情的内存边界；不依赖开发板。
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include "head_display.hpp"
#include "head_protocol.h"
#include "ui/face_engine.hpp"
#include "ui/head_controller.hpp"
#include "ui/dizzy_points.hpp"
static std::vector<uint8_t> transmitted;
// 模拟ST7735窗口和RGB565显存，验证差量刷新后的最终屏幕，而非只检查字节数。
static uint16_t panel[160][128]{};
static bool selected = false, data_mode = false, high_byte = true;
static uint8_t panel_command = 0, params[4]{}, pixel_high = 0;
static unsigned param_count = 0, x_start = 0, x_end = 127, y_start = 0, y_end = 159;
static unsigned cursor_x = 0, cursor_y = 0;
static int writes_before_failure = -1;
namespace Board {
bool spiWrite(const uint8_t *bytes, size_t size) {
    assert(selected);
    if (writes_before_failure == 0) {
        writes_before_failure = -1;
        return false;
    }
    if (writes_before_failure > 0)
        --writes_before_failure;
    transmitted.insert(transmitted.end(), bytes, bytes + size);
    for (size_t i = 0; i < size; ++i) {
        uint8_t byte = bytes[i];
        if (!data_mode) {
            panel_command = byte;
            param_count = 0;
            if (byte == 0x2c) {
                cursor_x = x_start;
                cursor_y = y_start;
                high_byte = true;
            }
        } else if (panel_command == 0x2a || panel_command == 0x2b) {
            assert(param_count < 4);
            params[param_count++] = byte;
            if (param_count == 4) {
                unsigned first = params[0] * 256 + params[1], last = params[2] * 256 + params[3];
                if (panel_command == 0x2a) {
                    assert(first <= last && last < 128);
                    x_start = first; x_end = last;
                } else {
                    assert(first <= last && last < 160);
                    y_start = first; y_end = last;
                }
            }
        } else if (panel_command == 0x2c) {
            if (high_byte)
                pixel_high = byte;
            else {
                assert(cursor_y <= y_end);
                panel[cursor_y][cursor_x] = pixel_high * 256 + byte;
                if (++cursor_x > x_end) {
                    cursor_x = x_start;
                    ++cursor_y;
                }
            }
            high_byte = !high_byte;
        }
    }
    return true;
}
void lcdSelect(bool value) { selected = value; }
void lcdData(bool value) { data_mode = value; }
void lcdReset(bool) {}
void delay(uint32_t) {}
void service() {}
}  // namespace Board
static std::vector<HpFrame> frames;
static void received(const HpFrame *f, void *) {
    frames.push_back(*f);
}
static void feed(HpParser &parser, const uint8_t *p, size_t n, uint32_t time = 1) {
    for (size_t i = 0; i < n; ++i)
        hp_parser_feed(&parser, p[i], time, received, nullptr);
}
static void protocolTests() {
    const uint8_t vector[] = "123456789";
    assert(hp_crc16(vector, 9) == 0x29b1);
    uint8_t packet[HP_MAX_FRAME], data[HP_MAX_PAYLOAD];
    const uint8_t ping_golden[] = {0xa5, 0x5a, 1, 1, 7, 0, 0xd3, 0x5c};
    assert(hp_encode(HP_PING, 7, nullptr, 0, packet) == sizeof(ping_golden));
    assert(std::memcmp(packet, ping_golden, sizeof(ping_golden)) == 0);
    for (unsigned i = 0; i < sizeof(data); ++i)
        data[i] = i;
    data[17] = 0xa5;
    data[18] = 0x5a;
    HpParser parser{};
    size_t size = hp_encode(HP_STATE, 255, data, 64, packet);
    assert(size == 72);
    feed(parser, packet, size);
    assert(frames.size() == 1);
    assert(frames.back().sequence == 255 && frames.back().length == 64);
    assert(std::memcmp(frames.back().payload, data, 64) == 0);
    packet[size - 1] ^= 1;
    feed(parser, packet, size);
    assert(parser.crc_errors == 1);
    packet[size - 1] ^= 1;
    feed(parser, packet, size);
    assert(frames.size() == 2);
    uint8_t invalid[] = {0xa5, 0x5a, 1, 0x10, 0, 255};
    feed(parser, invalid, sizeof(invalid));
    feed(parser, packet, size);
    assert(frames.size() == 3);
    feed(parser, packet, 5, 100);
    feed(parser, packet, size, 300);
    assert(frames.size() == 4);
    uint8_t prefix = 0xa5;
    feed(parser, &prefix, 1, 301);
    feed(parser, packet, size, 301);
    assert(frames.size() == 5);
    assert(hp_encode(HP_STATE, 0, data, 65, packet) == 0);
    // 固定种子噪声压力测试：ASan/UBSan检查解析器不会越界或无限增长。
    uint32_t rng = 123;
    for (int i = 0; i < 100000; ++i) {
        rng = rng * 1664525u + 1013904223u;
        uint8_t byte = rng >> 24;
        hp_parser_feed(&parser, byte, 400 + i / 10, received, nullptr);
        assert(parser.used <= HP_MAX_FRAME);
    }
    HpState s{HP_HAPPY, 200, HP_GESTURE_SHAKE, HP_SERVO_ENABLE, 2900, 100, 65535};
    uint8_t state_data[HP_STATE_SIZE];
    hp_state_pack(state_data, &s);
    HpFrame f{};
    f.length = HP_STATE_SIZE;
    std::memcpy(f.payload, state_data, sizeof(state_data));
    HpState decoded{};
    assert(hp_state_unpack(&f, &decoded));
    assert(decoded.event_id == 65535 && decoded.pan_cdeg == 2900);
    HpStatus status{};
    status.frames = 123456;
    status.firmware_version = 0x100;
    status.pan_cdeg = 2900;
    f.length = HP_STATUS_SIZE;
    hp_status_pack(f.payload, &status);
    HpStatus restored{};
    assert(hp_status_unpack(&f, &restored));
    assert(restored.frames == 123456 && restored.pan_cdeg == 2900);
    std::puts(
        "PASS protocol: golden CRC, fragmentation, resync, timeout, bounds, 100k noise bytes");
}
static void controllerTests() {
    HeadController head;
    head.tick(10);
    assert(!head.enabled());
    HpState s{HP_NORMAL, 0, HP_GESTURE_NONE, HP_SERVO_ENABLE, 18000, 18000, 1};
    assert(head.apply(s, 20));
    assert(head.targetPan() == 5200 && head.targetTilt() == 2500);
    head.tick(40);
    assert(head.pan() == 3050 && head.tilt() == 150);
    s.emotion = 99;
    assert(!head.apply(s, 1000));
    head.tick(1020);
    assert(!head.enabled());
    s = {HP_HAPPY, 220, HP_GESTURE_NOD, HP_SERVO_ENABLE, 2900, 0, 2};
    assert(head.apply(s, 2000));
    head.tick(2200);
    assert(head.targetTilt() == 2500);
    assert(head.apply(s, 2300));
    head.tick(2400);
    assert(head.targetTilt() == 500);  // 重发不能重启手势。
    s.gesture = HP_GESTURE_NONE;
    s.pan_cdeg = 2900;
    s.tilt_cdeg = 0;
    s.event_id = 3;
    assert(head.apply(s, 2420));
    for (uint32_t t = 2420; t < 3300; t += 20) {
        head.apply(s, t);
        head.tick(t);
    }
    assert(head.pan() == 2900 && head.tilt() == 0);
    s.flags = 0;
    assert(head.apply(s, 3400));
    head.tick(3420);
    assert(!head.enabled());
    s.flags = HP_SERVO_ENABLE;
    s.event_id = 4;
    assert(head.apply(s, 0xfffffff0));
    assert(head.online(30));
    head.tick(1100);
    assert(!head.enabled());
    assert(HeadController::pulseUs(0) == 500 && HeadController::pulseUs(9000) == 1500 &&
           HeadController::pulseUs(18000) == 2500);
    std::puts(
        "PASS controller: output disabled at boot, limits, smoothing, event deduplication, "
        "timeout, tick wrap");
}
static void graphicsTests(const char *output) {
    HeadDisplay lcd;
    FaceEngine engine(lcd);
    lcd.fillRoundRect(-50, -20, 100, 80, 80, Colors::RED);
    lcd.fillRoundRect(120, 150, 40, 40, 20, Colors::GREEN);
    lcd.fillRect(-20, -20, 300, 300, Colors::BLUE);
    assert(lcd.pixel(0, 0) == Colors::BLUE && lcd.pixel(127, 159) == Colors::BLUE);
    lcd.clear();
    lcd.drawPixel(0, 0, Colors::RED);
    lcd.drawPixel(1, 0, Colors::GREEN);
    assert(lcd.pixel(0, 0) == Colors::RED && lcd.pixel(1, 0) == Colors::GREEN);
    assert(lcd.flush());
    assert(transmitted.size() == 128 * 160 * 2 + 40 * 11);
    assert(transmitted[11] == 0xf8 && transmitted[12] == 0x00);
    assert(transmitted[13] == 0x07 && transmitted[14] == 0xe0);
    constexpr int cols = 4, rows = 3;
    std::vector<uint8_t> rgb(cols * 128 * rows * 160 * 3);
    for (unsigned emotion = 0; emotion < HP_EMOTION_COUNT; ++emotion) {
        engine.setEmotion(static_cast<EmotionState>(emotion));
        for (unsigned i = 0; i < 30; ++i)
            engine.update(emotion * 1500 + i * 50, 0.6f);
        for (int y = 0; y < 160; ++y)
            for (int x = 0; x < 128; ++x) {
                uint16_t c = lcd.pixel(x, y);
                int offset =
                    (((emotion / cols) * 160 + y) * (cols * 128) + (emotion % cols) * 128 + x) * 3;
                rgb[offset] = ((c >> 11) & 31) * 255 / 31;
                rgb[offset + 1] = ((c >> 5) & 63) * 255 / 63;
                rgb[offset + 2] = (c & 31) * 255 / 31;
            }
    }
    engine.setEmotion(EmotionState::BOOTING);
    engine.update(99999, 0.0f);
    assert(engine.getEmotion() == EmotionState::BOOTING);
    std::ofstream stream(output, std::ios::binary);
    stream << "P6\n512 480\n255\n";
    stream.write(reinterpret_cast<const char *>(rgb.data()), rgb.size());
    assert(stream.good());
    std::puts(
        "PASS graphics: clipped primitives, packed adjacent pixels, all 12 original emotions "
        "rendered under sanitizers");
}
static void assertPanel(const HeadDisplay &lcd) {
    for (int y = 0; y < 160; ++y)
        for (int x = 0; x < 128; ++x)
            assert(panel[y][x] == lcd.pixel(x, y));
}
static void incrementalTests() {
    HeadDisplay lcd;
    assert(lcd.init());
    assertPanel(lcd);
    transmitted.clear();
    assert(lcd.flush());
    assert(transmitted.empty() && lcd.lastChangedTiles() == 0);
    // 跨块、跨行以及右下边界；清屏必须擦掉上一帧内容。
    lcd.fillRect(7, 7, 3, 3, Colors::PINK);
    lcd.drawPixel(127, 159, Colors::WHITE);
    assert(lcd.flush());
    assert(lcd.lastChangedTiles() == 5 && lcd.lastTransferBytes() < 800);
    assertPanel(lcd);
    lcd.clear();
    assert(lcd.flush());
    assertPanel(lcd);
    // 所有块都是非纯色，强制缓存淘汰；容量不足只能多发，不能漏发或残影。
    for (int y = 0; y < 160; ++y)
        for (int x = 0; x < 128; ++x)
            lcd.drawPixel(x, y, ((x + y) & 1) ? Colors::CYAN : Colors::RED);
    for (int i = 0; i < 2; ++i) {
        assert(lcd.flush());
        assertPanel(lcd);
    }
    lcd.clear(Colors::GREEN);
    assert(lcd.flush());
    assertPanel(lcd);
    // 在地址命令及已写入部分像素后失败，下次必须恢复完整画面。
    for (int fail_at : {0, 3, 8, 15}) {
        lcd.clear(fail_at & 1 ? Colors::BLUE : Colors::ORANGE);
        lcd.drawPixel(0, 0, Colors::WHITE);
        writes_before_failure = fail_at;
        assert(!lcd.flush());
        assert(!selected);
        assert(lcd.flush());
        assertPanel(lcd);
    }
    FaceEngine engine(lcd);
    // 每种表情连续动画及跨情绪切换都与完整帧逐像素比较，包含声浪和眨眼。
    uint32_t tick = 100;
    for (unsigned emotion = 0; emotion < HP_EMOTION_COUNT; ++emotion) {
        engine.setEmotion(static_cast<EmotionState>(emotion));
        for (unsigned frame = 0; frame < 100; ++frame) {
            engine.update(tick, (frame % 17) / 16.0f);
            tick += 50;
            transmitted.clear();
            assert(lcd.flush());
            assertPanel(lcd);
        }
    }
    std::puts("PASS incremental display: exact panel pixels, cache eviction, erase, partial failure recovery, 1200 animation frames");
}
static void spiralGeometryTests() {
    // 与优化前的逐点浮点公式比较，而非只验证新公式自己；量化最多偏移1像素。
    for (int frame = 0; frame < 1000; ++frame) {
        float angle = frame * 0.16f, a = 0.5f;
        int32_t c = static_cast<int32_t>(std::cos(angle) * 16384.0f);
        int32_t s = static_cast<int32_t>(std::sin(angle) * 16384.0f);
        unsigned count = 0;
        for (const auto &p : DIZZY_POINTS) {
            assert(a < 4.8 * M_PI);
            float r = a * 2.3f;
            int x = static_cast<int>(std::cos(a + angle) * r);
            int y = static_cast<int>(std::sin(a + angle) * r);
            assert(std::abs(x - (p[0] * c - p[1] * s) / 4194304) <= 1);
            assert(std::abs(y - (p[0] * s + p[1] * c) / 4194304) <= 1);
            a += 0.22f;
            ++count;
        }
        assert(a >= 4.8 * M_PI && count == 67);
    }
    std::puts("PASS spiral: original geometry preserved within 1 pixel over 1000 angles");
}
int main(int argc, char **argv) {
    assert(argc == 2);
    protocolTests();
    controllerTests();
    graphicsTests(argv[1]);
    incrementalTests();
    spiralGeometryTests();
}
