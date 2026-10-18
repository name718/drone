/**
 * @file st7735.hpp
 * @brief 1.8寸 ST7735S SPI2 TFT 彩屏硬件驱动头文件
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#include "ui/canvas.hpp"

class ST7735Driver : public Canvas {
public:
    ST7735Driver();
    ~ST7735Driver();

    ST7735Driver(const ST7735Driver &) = delete;
    ST7735Driver &operator=(const ST7735Driver &) = delete;

    /**
     * @brief 初始化 SPI2 硬件总线、GPIO 并点亮屏幕
     */
    esp_err_t init();

    /**
     * @brief 控制背光亮灭
     */
    void setBacklight(bool on);

    /**
     * @brief 清空整屏显存为指定颜色
     */
    void clear(uint16_t color = Colors::BLACK);

    /**
     * @brief 在显存中绘制单个像素
     */
    void drawPixel(int16_t x, int16_t y, uint16_t color);

    /**
     * @brief 绘制水平快速直线
     */
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color);

    /**
     * @brief 绘制垂直快速直线
     */
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color);

    /**
     * @brief 绘制两点间任意倾斜直线
     */
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);

    /**
     * @brief 绘制实心圆形
     */
    void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);

    /**
     * @brief 绘制空心圆形轮廓
     */
    void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);

    /**
     * @brief 绘制实心矩形
     */
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

    /**
     * @brief 绘制实心圆角矩形 (赛博大眼眼睛轮廓基础)
     */
    void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);

    /**
     * @brief 将 40KB 全屏显存批量刷入物理屏幕
     */
    esp_err_t flush();

private:
    void select();
    void deselect();
    void writeCommand(uint8_t cmd);
    void writeData(const uint8_t *data, size_t len);
    void writeDataByte(uint8_t data);
    void setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

    spi_device_handle_t spi_{nullptr};
    uint16_t *frame_buffer_{nullptr};  // 40KB FrameBuffer (128 * 160 * 2 字节)
};
