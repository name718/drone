/** @file head_display.hpp @brief 4bpp 索引显存：10240字节；逐行展开成RGB565发往ST7735。 */
#pragma once
#include <cstddef>

#include "ui/canvas.hpp"
class HeadDisplay final : public Canvas {
public:
    static constexpr int WIDTH = 128, HEIGHT = 160;
    static constexpr size_t BUFFER_SIZE = WIDTH * HEIGHT / 2;
    HeadDisplay() { clear(); invalidate(); }
    bool init();
    bool flush();
    void clear(uint16_t color = Colors::BLACK) override;
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
    uint16_t pixel(int x, int y) const;
    static uint8_t paletteIndex(uint16_t color);
    uint32_t lastTransferBytes() const { return transfer_bytes_; }
    uint32_t lastChangedTiles() const { return changed_tiles_; }

private:
    // 1280块各用一字节记录屏幕端状态：0..15=纯色，16..143=缓存槽，255=未知。
    // 非纯色块保留精确像素，不用哈希，避免碰撞造成漏刷。缓存满时仅多刷，不漏刷。
    static constexpr int TILE = 4, COLS = WIDTH / TILE, ROWS = HEIGHT / TILE;
    static constexpr int TILE_BYTES = TILE * TILE / 2, CACHE_SLOTS = 128;
    static constexpr uint8_t UNKNOWN = 255;
    static_assert(COLS <= 32 && WIDTH % TILE == 0 && HEIGHT % TILE == 0, "tile geometry");
    static_assert(16 + CACHE_SLOTS < UNKNOWN, "cache slots must fit tile state");
    void invalidate();
    void readTile(unsigned tile, uint8_t *packed) const;
    static uint8_t uniformColor(const uint8_t *packed);
    bool tileChanged(unsigned tile, const uint8_t *packed) const;
    void rememberTile(unsigned tile, const uint8_t *packed);
    bool writeWindow(int x, int y, int width, int height);
    bool command(uint8_t cmd, const uint8_t *data = nullptr, size_t n = 0);
    uint8_t pixels_[BUFFER_SIZE];
    uint8_t tile_state_[COLS * ROWS];
    uint8_t cache_[CACHE_SLOTS][TILE_BYTES];
    uint16_t owner_[CACHE_SLOTS];
    uint16_t next_slot_ = 0;
    uint32_t transfer_bytes_ = 0, changed_tiles_ = 0;
};
