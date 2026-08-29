#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/i2c_master.h"
#include "esp_err.h"

/**
 * @brief SSD1306 OLED 屏幕轻量驱动类 (128x64 分辨率)
 * @reference Solomon Systech SSD1306 Datasheet Rev 1.1
 */
class OledDriver {
public:
    static constexpr uint8_t SCREEN_WIDTH = 128;  // 屏幕宽度 (128 列)
    static constexpr uint8_t SCREEN_HEIGHT = 64;  // 屏幕高度 (64 行)

    /**
     * @brief 构造函数：指定 SDA、SCL 引脚及 I2C 从机地址
     * @param sda_pin I2C 数据线引脚 (默认 GPIO 8)
     * @param scl_pin I2C 时钟线引脚 (默认 GPIO 9)
     * @param i2c_addr I2C 7位从机地址 (默认 0x3C)
     */
    OledDriver(int sda_pin = 8, int scl_pin = 9, uint8_t i2c_addr = 0x3C);
    ~OledDriver();

    /**
     * @brief 初始化 I2C 总线并发送 SSD1306 上电配置指令
     */
    esp_err_t init();

    /**
     * @brief 清空画布 (1024 字节显存全填 0)
     */
    void clear();

    /**
     * @brief 将单片机内存中的 1024 字节显存流式刷入屏幕硬件
     */
    void update();

    /**
     * @brief 绘制单像素点
     * @param x 横坐标 (0 ~ 127)
     * @param y 纵坐标 (0 ~ 63)
     * @param color true 为点亮(白), false 为熄灭(黑)
     */
    void drawPixel(int x, int y, bool color);

    /**
     * @brief 填充实心矩形
     */
    void fillRect(int x, int y, int w, int h, bool color);

    /**
     * @brief 绘制空心圆角矩形线条 (用于绘制月牙眉眼等轮廓)
     * @param r 圆角半径
     */
    void drawRoundRect(int x, int y, int w, int h, int r, bool color);

    /**
     * @brief 填充实心圆角矩形 (用于绘制科技感机器人大眼睛)
     * @param r 圆角半径
     */
    void fillRoundRect(int x, int y, int w, int h, int r, bool color);

private:
    int m_sdaPin;
    int m_sclPin;
    uint8_t m_i2cAddr;
    i2c_master_bus_handle_t m_busHandle;  // I2C 主机总线句柄
    i2c_master_dev_handle_t m_devHandle;  // OLED 挂载设备句柄

    // 显存画布: 128 列 * (64行 / 8位) = 1024 字节
    uint8_t m_buffer[SCREEN_WIDTH * SCREEN_HEIGHT / 8];

    // 发送单个控制命令字节 (0x00 前缀)
    esp_err_t writeCommand(uint8_t cmd);
    // 发送显存数据块 (0x40 前缀)
    esp_err_t writeData(const uint8_t *data, size_t len);
};
