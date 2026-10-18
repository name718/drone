#include "head_display.hpp"

#include <algorithm>
#include <cstring>

#include "display_transport.hpp"
namespace {
constexpr uint16_t palette[16] = {Colors::BLACK,
                                  Colors::WHITE,
                                  Colors::RED,
                                  Colors::GREEN,
                                  Colors::BLUE,
                                  Colors::CYAN,
                                  Colors::NEON_BLUE,
                                  Colors::DARK_GRAY,
                                  Colors::DARK_CYAN,
                                  Colors::MAGENTA,
                                  Colors::YELLOW,
                                  Colors::ORANGE,
                                  Colors::PINK,
                                  Colors::PURPLE,
                                  0,
                                  0};
}
uint8_t HeadDisplay::paletteIndex(uint16_t color) {
    for (uint8_t i = 0; i < 14; ++i)
        if (palette[i] == color)
            return i;
    return 0;  // 当前表情仅使用固定14种颜色，未定义颜色不越界访问调色板。
}
void HeadDisplay::clear(uint16_t color) {
    uint8_t i = paletteIndex(color);
    std::memset(pixels_, i | (i << 4), sizeof(pixels_));
}
void HeadDisplay::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT)
        return;
    unsigned p = y * WIDTH + x;
    uint8_t i = paletteIndex(color);
    if (p & 1)
        pixels_[p / 2] = (pixels_[p / 2] & 0xf0u) | i;
    else
        pixels_[p / 2] = (pixels_[p / 2] & 0x0fu) | (i << 4);
}
void HeadDisplay::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    if (w <= 0 || h <= 0)
        return;
    int x2 = std::min<int>(WIDTH, x + w), y2 = std::min<int>(HEIGHT, y + h);
    x = std::max<int>(0, x);
    y = std::max<int>(0, y);
    uint8_t index = paletteIndex(c);
    for (int j = y; j < y2; ++j)
        for (int i = x; i < x2; ++i) {
            unsigned p = j * WIDTH + i;
            if (p & 1)
                pixels_[p / 2] = (pixels_[p / 2] & 0xf0u) | index;
            else
                pixels_[p / 2] = (pixels_[p / 2] & 0x0fu) | (index << 4);
        }
}
uint16_t HeadDisplay::pixel(int x, int y) const {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT)
        return 0;
    unsigned p = y * WIDTH + x;
    uint8_t v = pixels_[p / 2];
    return palette[(p & 1) ? (v & 15u) : (v >> 4)];
}
bool HeadDisplay::command(uint8_t cmd, const uint8_t *data, size_t n) {
    Board::lcdSelect(true);
    Board::lcdData(false);
    bool ok = Board::spiWrite(&cmd, 1);
    if (n && ok) {
        Board::lcdData(true);
        ok = Board::spiWrite(data, n);
    }
    Board::lcdSelect(false);
    return ok;
}
bool HeadDisplay::init() {
    invalidate();  // 屏幕复位后旧缓存已无效，第一帧必须完整写入。
    Board::lcdReset(true);
    Board::delay(10);
    Board::lcdReset(false);
    Board::delay(50);
    Board::lcdReset(true);
    Board::delay(120);
    if (!command(0x01))
        return false;
    Board::delay(150);
    if (!command(0x11))
        return false;
    Board::delay(200);
    // 保留原 ESP32 已验证的 ST7735 参数：128x160、偏移0、MADCTL=C8。
    static const uint8_t init_data[] = {
        0xb1, 3,    0x01, 0x2c, 0x2d, 0xb2, 3,    0x01, 0x2c, 0x2d, 0xb3, 6,    0x01, 0x2c, 0x2d,
        0x01, 0x2c, 0x2d, 0xb4, 1,    0x07, 0xc0, 3,    0xa2, 0x02, 0x84, 0xc1, 1,    0xc5, 0xc2,
        2,    0x0a, 0x00, 0xc3, 2,    0x8a, 0x2a, 0xc4, 2,    0x8a, 0xee, 0xc5, 1,    0x0e, 0x20,
        0,    0x36, 1,    0xc8, 0x3a, 1,    0x05, 0xe0, 16,   0x02, 0x1c, 0x07, 0x12, 0x37, 0x32,
        0x29, 0x2d, 0x29, 0x25, 0x2b, 0x39, 0,    1,    3,    0x10, 0xe1, 16,   0x03, 0x1d, 0x07,
        0x06, 0x2e, 0x2c, 0x29, 0x2d, 0x2e, 0x2e, 0x37, 0x3f, 0,    0,    2,    0x10};
    for (size_t p = 0; p < sizeof(init_data);) {
        uint8_t cmd = init_data[p++], n = init_data[p++];
        if (!command(cmd, init_data + p, n))
            return false;
        p += n;
    }
    if (!command(0x13))
        return false;
    Board::delay(10);
    if (!command(0x29))
        return false;
    Board::delay(50);
    clear();
    return flush();
}
void HeadDisplay::invalidate() {
    std::memset(tile_state_, UNKNOWN, sizeof(tile_state_));
    std::memset(owner_, 0xff, sizeof(owner_));
    next_slot_ = 0;
}
void HeadDisplay::readTile(unsigned tile, uint8_t *packed) const {
    unsigned x = (tile % COLS) * (TILE / 2), y = (tile / COLS) * TILE;
    for (unsigned row = 0; row < TILE; ++row)
        std::memcpy(packed + row * (TILE / 2), pixels_ + (y + row) * (WIDTH / 2) + x, TILE / 2);
}
uint8_t HeadDisplay::uniformColor(const uint8_t *packed) {
    uint8_t color = packed[0] >> 4, pair = color | (color << 4);
    for (int i = 0; i < TILE_BYTES; ++i)
        if (packed[i] != pair)
            return UNKNOWN;
    return color;
}
bool HeadDisplay::tileChanged(unsigned tile, const uint8_t *packed) const {
    uint8_t state = tile_state_[tile];
    if (state == UNKNOWN)
        return true;
    if (state < 16)
        return uniformColor(packed) != state;
    return std::memcmp(cache_[state - 16], packed, TILE_BYTES) != 0;
}
void HeadDisplay::rememberTile(unsigned tile, const uint8_t *packed) {
    uint8_t old = tile_state_[tile], color = uniformColor(packed);
    if (color != UNKNOWN) {
        if (old >= 16 && old != UNKNOWN)
            owner_[old - 16] = 0xffff;
        tile_state_[tile] = color;
        return;
    }
    unsigned slot;
    if (old >= 16 && old != UNKNOWN)
        slot = old - 16;
    else {
        // 先找空槽，满了循环淘汰。被淘汰的块标成未知，下次会重新发送。
        slot = next_slot_;
        for (unsigned i = 0; i < CACHE_SLOTS; ++i) {
            unsigned candidate = (next_slot_ + i) % CACHE_SLOTS;
            if (owner_[candidate] == 0xffff) {
                slot = candidate;
                break;
            }
        }
        if (owner_[slot] != 0xffff)
            tile_state_[owner_[slot]] = UNKNOWN;
        next_slot_ = (slot + 1) % CACHE_SLOTS;
    }
    std::memcpy(cache_[slot], packed, TILE_BYTES);
    owner_[slot] = tile;
    tile_state_[tile] = 16 + slot;
}
bool HeadDisplay::writeWindow(int x, int y, int width, int height) {
    const uint8_t col[] = {0, static_cast<uint8_t>(x), 0, static_cast<uint8_t>(x + width - 1)};
    const uint8_t row[] = {0, static_cast<uint8_t>(y), 0, static_cast<uint8_t>(y + height - 1)};
    if (!command(0x2a, col, 4) || !command(0x2b, row, 4))
        return false;
    Board::lcdSelect(true);
    Board::lcdData(false);
    const uint8_t cmd = 0x2c;
    if (!Board::spiWrite(&cmd, 1)) {
        Board::lcdSelect(false);
        return false;
    }
    Board::lcdData(true);
    uint8_t line[WIDTH * 2];
    for (int row_y = y; row_y < y + height; ++row_y) {
        // 一个字节展开两个像素，避免整屏逐像素函数调用和边界判断。
        for (int pair = 0; pair < width / 2; ++pair) {
            uint8_t packed = pixels_[row_y * (WIDTH / 2) + x / 2 + pair];
            uint16_t left = palette[packed >> 4], right = palette[packed & 15u];
            line[4 * pair] = left >> 8;
            line[4 * pair + 1] = left;
            line[4 * pair + 2] = right >> 8;
            line[4 * pair + 3] = right;
        }
        if (!Board::spiWrite(line, width * 2)) {
            Board::lcdSelect(false);
            return false;
        }
        Board::service();
    }
    Board::lcdSelect(false);
    transfer_bytes_ += width * height * 2 + 11;
    return true;
}
bool HeadDisplay::flush() {
    transfer_bytes_ = changed_tiles_ = 0;
    uint8_t packed[TILE_BYTES];
    for (unsigned row = 0; row < ROWS; ++row) {
        uint32_t dirty = 0;
        for (unsigned col = 0; col < COLS; ++col) {
            unsigned tile = row * COLS + col;
            readTile(tile, packed);
            if (tileChanged(tile, packed)) {
                dirty |= 1u << col;
                ++changed_tiles_;
            }
        }
        // 同一行连续的变化块合并为一个窗口，减少设置地址和DMA启动次数。
        for (unsigned col = 0; col < COLS;) {
            if (!(dirty & (1u << col))) {
                ++col;
                continue;
            }
            unsigned first = col++;
            while (col < COLS && (dirty & (1u << col)))
                ++col;
            if (!writeWindow(first * TILE, row * TILE, (col - first) * TILE, TILE)) {
                // 可能已写入半个窗口，无法推断屏幕状态；下次全量恢复，避免残影。
                invalidate();
                return false;
            }
            // 只在整个窗口发送成功后提交屏幕端缓存。
            for (unsigned x = first; x < col; ++x) {
                unsigned tile = row * COLS + x;
                readTile(tile, packed);
                rememberTile(tile, packed);
            }
        }
        Board::service();
    }
    return true;
}
