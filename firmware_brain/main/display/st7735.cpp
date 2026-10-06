/**
 * @file st7735.cpp
 * @brief 1.8寸 ST7735S SPI2 TFT 彩屏硬件驱动实现文件
 */
#include "display/st7735.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "config/board_config.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "屏幕驱动";

// 屏幕分辨率参数
static constexpr int SCREEN_W = Config::Display::WIDTH;                    // 160
static constexpr int SCREEN_H = Config::Display::HEIGHT;                   // 128
static constexpr size_t FB_SIZE = SCREEN_W * SCREEN_H * sizeof(uint16_t);  // 40960 字节

ST7735Driver::ST7735Driver() = default;

ST7735Driver::~ST7735Driver() {
    if (frame_buffer_) {
        free(frame_buffer_);
        frame_buffer_ = nullptr;
    }
}

void ST7735Driver::select() {
    gpio_set_level((gpio_num_t)Config::Display::PIN_CS, 0);  // CS=0 选中从机
}

void ST7735Driver::deselect() {
    gpio_set_level((gpio_num_t)Config::Display::PIN_CS, 1);  // CS=1 释放从机
}

void ST7735Driver::writeCommand(uint8_t cmd) {
    select();
    gpio_set_level((gpio_num_t)Config::Display::PIN_DC, 0);  // DC=0 表示发送控制指令
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &cmd;
    spi_device_polling_transmit(spi_, &t);
    deselect();
}

void ST7735Driver::writeData(const uint8_t *data, size_t len) {
    if (len == 0)
        return;
    select();
    gpio_set_level((gpio_num_t)Config::Display::PIN_DC, 1);  // DC=1 表示发送显存数据
    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = data;
    spi_device_polling_transmit(spi_, &t);
    deselect();
}

void ST7735Driver::writeDataByte(uint8_t data) {
    writeData(&data, 1);
}

void ST7735Driver::setBacklight(bool on) {
    gpio_set_level((gpio_num_t)Config::Display::PIN_BLK, on ? 1 : 0);
}

void ST7735Driver::setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    // 叠加屏幕硬件制造工艺物理偏移量 (彻底解决边缘花屏和雪花残影)
    uint16_t x_start = x0 + Config::Display::OFFSET_X;
    uint16_t x_end = x1 + Config::Display::OFFSET_X;
    uint16_t y_start = y0 + Config::Display::OFFSET_Y;
    uint16_t y_end = y1 + Config::Display::OFFSET_Y;

    // 列地址设置 (CASET: 0x2A)
    writeCommand(0x2A);
    uint8_t data_x[] = {
        static_cast<uint8_t>(x_start >> 8), static_cast<uint8_t>(x_start & 0xFF),
        static_cast<uint8_t>(x_end >> 8),   static_cast<uint8_t>(x_end & 0xFF)
    };
    writeData(data_x, 4);

    // 行地址设置 (RASET: 0x2B)
    writeCommand(0x2B);
    uint8_t data_y[] = {
        static_cast<uint8_t>(y_start >> 8), static_cast<uint8_t>(y_start & 0xFF),
        static_cast<uint8_t>(y_end >> 8),   static_cast<uint8_t>(y_end & 0xFF)
    };
    writeData(data_y, 4);
}

esp_err_t ST7735Driver::init() {
    ESP_LOGI(TAG, "正在初始化 ST7735S SPI2 总线与引脚...");

    // 1. 初始化控制 GPIO (CS, DC, RST, BLK)
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << Config::Display::PIN_CS) | (1ULL << Config::Display::PIN_DC) |
                           (1ULL << Config::Display::PIN_RST) | (1ULL << Config::Display::PIN_BLK);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    deselect();           // 初始拉高 CS，不选中芯片
    setBacklight(false);  // 先熄灭背光，防止开机雪花闪烁

    // 2. 硬件稳固复位时序 (释放复位脚并留足内部晶振起振时间)
    gpio_set_level((gpio_num_t)Config::Display::PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)Config::Display::PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level((gpio_num_t)Config::Display::PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    // 3. 配置 SPI2 主机总线
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = Config::Display::PIN_MOSI;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = Config::Display::PIN_SCLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 4096 * 4;  // 16KB DMA 描述符缓冲空间

    ESP_ERROR_CHECK(spi_bus_initialize(Config::Display::SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 4. 将屏幕设备挂载到 SPI2 总线 (spics_io_num = -1 手动控制片选，杜绝帧中抖动)
    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = Config::Display::SPI_CLOCK_SPEED_HZ;  // 10 MHz 工业高信噪比时钟
    devcfg.mode = 0;                                              // SPI Mode 0 (CPOL=0, CPHA=0)
    devcfg.spics_io_num = -1;                                     // 手动控制 CS，确保连续流式推流
    devcfg.queue_size = 7;

    ESP_ERROR_CHECK(spi_bus_add_device(Config::Display::SPI_HOST, &devcfg, &spi_));

    // 5. 分配 40KB 内部显存 (优先使用内部 DMA 兼容内存，保证极速推流)
    frame_buffer_ = (uint16_t *)heap_caps_malloc(FB_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!frame_buffer_) {
        frame_buffer_ = (uint16_t *)heap_caps_malloc(FB_SIZE, MALLOC_CAP_SPIRAM);
    }
    if (!frame_buffer_) {
        ESP_LOGE(TAG, "无法为屏幕分配 40KB 显存！");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "屏幕显存分配就绪 (首地址: %p, 尺寸: %u 字节)", frame_buffer_, (unsigned)FB_SIZE);
    clear(Colors::BLACK);

    // =========================================================================
    // 6. 执行 ST7735 工业级完整电源泵与内部振荡时序 (彻底根治花屏与杂波)
    // =========================================================================
    writeCommand(0x01);  // 软件复位 (SWRESET)
    vTaskDelay(pdMS_TO_TICKS(150));

    writeCommand(0x11);  // 退出睡眠 (SLPOUT)
    vTaskDelay(pdMS_TO_TICKS(200));

    // 帧率控制 (Normal Mode) - FRMCTR1 (0xB1)
    writeCommand(0xB1);
    writeDataByte(0x01);
    writeDataByte(0x2C);
    writeDataByte(0x2D);

    // 帧率控制 (Idle Mode) - FRMCTR2 (0xB2)
    writeCommand(0xB2);
    writeDataByte(0x01);
    writeDataByte(0x2C);
    writeDataByte(0x2D);

    // 帧率控制 (Partial Mode) - FRMCTR3 (0xB3)
    writeCommand(0xB3);
    writeDataByte(0x01);
    writeDataByte(0x2C);
    writeDataByte(0x2D);
    writeDataByte(0x01);
    writeDataByte(0x2C);
    writeDataByte(0x2D);

    // 点反转控制 (INVCTR: 0xB4) - 消除液晶闪烁与杂斑
    writeCommand(0xB4);
    writeDataByte(0x07);

    // 内部高压电荷泵电源控制 1~5 (PWCTR1 ~ PWCTR5: 0xC0~0xC4)
    writeCommand(0xC0);
    writeDataByte(0xA2);
    writeDataByte(0x02);
    writeDataByte(0x84);

    writeCommand(0xC1);
    writeDataByte(0xC5);

    writeCommand(0xC2);
    writeDataByte(0x0A);
    writeDataByte(0x00);

    writeCommand(0xC3);
    writeDataByte(0x8A);
    writeDataByte(0x2A);

    writeCommand(0xC4);
    writeDataByte(0x8A);
    writeDataByte(0xEE);

    // VCOM 电压控制 (VMCTR1: 0xC5) - 稳定基准电压，彻底消除水波纹与白斑
    writeCommand(0xC5);
    writeDataByte(0x0E);

    // 关闭显示反转 (INVOFF: 0x20)
    writeCommand(0x20);

    // 显存访问控制 (MADCTL: 0x36) - 竖屏模式 (MV=0)
    writeCommand(0x36);
    writeDataByte(Config::Display::MADCTL_VAL);

    // 像素格式 (COLMOD: 0x3A) - 16-bit RGB565
    writeCommand(0x3A);
    writeDataByte(0x05);

    // Gamma 伽马曲线精确校准 (正极性 GMCTRP1: 0xE0)
    writeCommand(0xE0);
    static const uint8_t gamma_p[] = {
        0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
        0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10
    };
    writeData(gamma_p, sizeof(gamma_p));

    // Gamma 伽马曲线精确校准 (负极性 GMCTRN1: 0xE1)
    writeCommand(0xE1);
    static const uint8_t gamma_n[] = {
        0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
        0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10
    };
    writeData(gamma_n, sizeof(gamma_n));

    // 开启正常显示 (NORON: 0x13)
    writeCommand(0x13);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 开启屏幕输出 (DISPON: 0x29)
    writeCommand(0x29);
    vTaskDelay(pdMS_TO_TICKS(50));

    // 7. 【关键抗花屏处理】：在开背光前，先往全屏 GRAM 刷一层纯黑底色
    flush();
    vTaskDelay(pdMS_TO_TICKS(20));

    // 8. 点亮背光
    setBacklight(true);

    ESP_LOGI(TAG, "ST7735S 竖屏初始化完毕 (%dx%d 像素，显存: %u 字节)",
             SCREEN_W, SCREEN_H, (unsigned)FB_SIZE);
    return ESP_OK;
}

void ST7735Driver::clear(uint16_t color) {
    if (!frame_buffer_)
        return;
    // 转换高低字节 (SPI 传输为大端序)
    uint16_t swap_color = (color << 8) | (color >> 8);
    for (size_t i = 0; i < (SCREEN_W * SCREEN_H); i++) {
        frame_buffer_[i] = swap_color;
    }
}

void ST7735Driver::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H || !frame_buffer_)
        return;
    frame_buffer_[y * SCREEN_W + x] = (color << 8) | (color >> 8);
}

void ST7735Driver::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    fillRect(x, y, w, 1, color);
}

void ST7735Driver::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
    fillRect(x, y, 1, h, color);
}

void ST7735Driver::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    // Bresenham 硬件经典高效画线算法
    int16_t dx = std::abs(x1 - x0);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t dy = -std::abs(y1 - y0);
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx + dy;

    while (true) {
        drawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void ST7735Driver::fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
    if (!frame_buffer_ || r <= 0) return;
    int16_t r2 = r * r;
    for (int16_t dy = -r; dy <= r; dy++) {
        int16_t dx = static_cast<int16_t>(std::sqrt(r2 - dy * dy));
        drawFastHLine(x0 - dx, y0 + dy, dx * 2 + 1, color);
    }
}

void ST7735Driver::drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
    if (!frame_buffer_ || r <= 0) return;
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;

    drawPixel(x0, y0 + r, color);
    drawPixel(x0, y0 - r, color);
    drawPixel(x0 + r, y0, color);
    drawPixel(x0 - r, y0, color);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        drawPixel(x0 + x, y0 + y, color);
        drawPixel(x0 - x, y0 + y, color);
        drawPixel(x0 + x, y0 - y, color);
        drawPixel(x0 - x, y0 - y, color);
        drawPixel(x0 + y, y0 + x, color);
        drawPixel(x0 - y, y0 + x, color);
        drawPixel(x0 + y, y0 - x, color);
        drawPixel(x0 - y, y0 - x, color);
    }
}

void ST7735Driver::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (!frame_buffer_)
        return;
    int16_t x2 = x + w;
    int16_t y2 = y + h;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x2 > SCREEN_W)
        x2 = SCREEN_W;
    if (y2 > SCREEN_H)
        y2 = SCREEN_H;

    uint16_t swap_color = (color << 8) | (color >> 8);
    for (int16_t j = y; j < y2; j++) {
        int row_idx = j * SCREEN_W;
        for (int16_t i = x; i < x2; i++) {
            frame_buffer_[row_idx + i] = swap_color;
        }
    }
}

void ST7735Driver::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
                                 uint16_t color) {
    if (!frame_buffer_)
        return;
    // 中间十字矩形填充
    fillRect(x + r, y, w - 2 * r, h, color);
    fillRect(x, y + r, r, h - 2 * r, color);
    fillRect(x + w - r, y + r, r, h - 2 * r, color);

    // 四个圆角填充 (利用圆几何距离方程: dx^2 + dy^2 <= r^2)
    uint16_t swap_color = (color << 8) | (color >> 8);
    int16_t r2 = r * r;

    for (int16_t dy = 0; dy <= r; dy++) {
        for (int16_t dx = 0; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r2) {
                // 左上圆角
                frame_buffer_[(y + r - dy) * SCREEN_W + (x + r - dx)] = swap_color;
                // 右上圆角
                frame_buffer_[(y + r - dy) * SCREEN_W + (x + w - r + dx - 1)] = swap_color;
                // 左下圆角
                frame_buffer_[(y + h - r + dy - 1) * SCREEN_W + (x + r - dx)] = swap_color;
                // 右下圆角
                frame_buffer_[(y + h - r + dy - 1) * SCREEN_W + (x + w - r + dx - 1)] = swap_color;
            }
        }
    }
}

esp_err_t ST7735Driver::flush() {
    if (!frame_buffer_ || !spi_)
        return ESP_ERR_INVALID_STATE;

    // 1. 设置屏幕刷新视窗 (0, 0) ~ (WIDTH-1, HEIGHT-1)
    setAddrWindow(0, 0, SCREEN_W - 1, SCREEN_H - 1);

    // 2. 选中屏幕片选 (CS=0)
    select();

    // 3. 发送 RAMWR (0x2C) 进入显存写入模式
    gpio_set_level((gpio_num_t)Config::Display::PIN_DC, 0);
    uint8_t cmd_ramwr = 0x2C;
    spi_transaction_t t_cmd = {};
    t_cmd.length = 8;
    t_cmd.tx_buffer = &cmd_ramwr;
    esp_err_t err = spi_device_polling_transmit(spi_, &t_cmd);
    if (err != ESP_OK) {
        deselect();
        return err;
    }

    // 4. 切换为数据模式 (DC=1)，保持 CS=0 连续分片 DMA 推流
    // 【重大修复说明】：ESP32-S3 硬件 SPI 单次 DMA 传输最大长度寄存器上限为 32KB (262,144 bits)。
    // 我们的显存是 128x160x2 = 40960 字节 (> 32KB)。若单次直接传输，底层驱动会抛出 ESP_ERR_INVALID_ARG
    // 导致数据根本未发送到屏幕（屏幕呈现出白/蓝杂波雪花花屏）。
    // 采用 4096 字节 (4KB) 分片推流，DMA 极速吞吐且百分之百在硬件有效窗口内！
    gpio_set_level((gpio_num_t)Config::Display::PIN_DC, 1);

    constexpr size_t CHUNK_SIZE = 4096;
    const uint8_t *ptr = reinterpret_cast<const uint8_t *>(frame_buffer_);
    size_t remaining = FB_SIZE;

    while (remaining > 0) {
        size_t chunk_len = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
        spi_transaction_t t_data = {};
        t_data.length = chunk_len * 8;  // 单位: bit
        t_data.tx_buffer = ptr;
        err = spi_device_polling_transmit(spi_, &t_data);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "DMA 分片推流失败: %s", esp_err_to_name(err));
            deselect();
            return err;
        }
        ptr += chunk_len;
        remaining -= chunk_len;
    }

    // 5. 整屏推流完毕，释放片选 (CS=1)
    deselect();
    return ESP_OK;
}
