/** @file canvas.hpp @brief 与芯片无关的 RGB565 绘图接口；显存布局由各驱动决定。 */
#pragma once
#include <cstdint>
namespace Colors {
constexpr uint16_t BLACK = 0x0000, WHITE = 0xffff, RED = 0xf800, GREEN = 0x07e0, BLUE = 0x001f,
                   CYAN = 0x07ff, NEON_BLUE = 0x05bf, DARK_GRAY = 0x18c3, DARK_CYAN = 0x0250,
                   MAGENTA = 0xf81f, YELLOW = 0xffe0, ORANGE = 0xfd20, PINK = 0xfe19,
                   PURPLE = 0x8010;
}
class Canvas {
public:
    virtual ~Canvas() = default;
    virtual void clear(uint16_t color = Colors::BLACK) = 0;
    virtual void drawPixel(int16_t x, int16_t y, uint16_t color) = 0;
    virtual void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    virtual void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color);
    virtual void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color);
    virtual void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
    virtual void fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color);
    virtual void drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color);
    virtual void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
                               uint16_t color);
};
