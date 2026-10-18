#include "ui/canvas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
void Canvas::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    if (w <= 0 || h <= 0)
        return;
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i)
            drawPixel(i, j, c);
}
void Canvas::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) {
    fillRect(x, y, w, 1, c);
}
void Canvas::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) {
    fillRect(x, y, 1, h, c);
}
void Canvas::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -std::abs(y1 - y0),
        sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        drawPixel(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e = 2 * err;
        if (e >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}
void Canvas::fillCircle(int16_t x, int16_t y, int16_t r, uint16_t c) {
    if (r <= 0)
        return;
    for (int dy = -r; dy <= r; ++dy) {
        int dx = static_cast<int>(std::sqrt(static_cast<float>(r * r - dy * dy)));
        drawFastHLine(x - dx, y + dy, 2 * dx + 1, c);
    }
}
void Canvas::drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t c) {
    if (r <= 0)
        return;
    int f = 1 - r, dx = 1, dy = -2 * r, x = 0, y = r;
    drawPixel(x0, y0 + r, c);
    drawPixel(x0, y0 - r, c);
    drawPixel(x0 + r, y0, c);
    drawPixel(x0 - r, y0, c);
    while (x < y) {
        if (f >= 0) {
            --y;
            dy += 2;
            f += dy;
        }
        ++x;
        dx += 2;
        f += dx;
        drawPixel(x0 + x, y0 + y, c);
        drawPixel(x0 - x, y0 + y, c);
        drawPixel(x0 + x, y0 - y, c);
        drawPixel(x0 - x, y0 - y, c);
        drawPixel(x0 + y, y0 + x, c);
        drawPixel(x0 - y, y0 + x, c);
        drawPixel(x0 + y, y0 - x, c);
        drawPixel(x0 - y, y0 - x, c);
    }
}
void Canvas::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c) {
    if (w <= 0 || h <= 0)
        return;
    r = std::max<int>(0, std::min<int>(r, std::min(w, h) / 2));
    fillRect(x + r, y, w - 2 * r, h, c);
    fillRect(x, y + r, r, h - 2 * r, c);
    fillRect(x + w - r, y + r, r, h - 2 * r, c);
    // 所有角点都通过 drawPixel 的边界检查；不再直接写显存，避免旧实现越界。
    for (int dy = 0; dy <= r; ++dy)
        for (int dx = 0; dx <= r; ++dx)
            if (dx * dx + dy * dy <= r * r) {
                drawPixel(x + r - dx, y + r - dy, c);
                drawPixel(x + w - r + dx - 1, y + r - dy, c);
                drawPixel(x + r - dx, y + h - r + dy - 1, c);
                drawPixel(x + w - r + dx - 1, y + h - r + dy - 1, c);
            }
}
