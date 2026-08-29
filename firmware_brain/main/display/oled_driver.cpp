/**
 * @file oled_driver.cpp
 * @brief SSD1306 0.96寸 I2C OLED 屏幕驱动实现
 * @reference Solomon Systech SSD1306 Datasheet Rev 1.1 (Section 8: Command Table & Descriptions)
 *
 * 显存映射原理 (128x64 像素):
 * 1. 全屏共 128 列 * 64 行 = 8192 个像素点。
 * 2. 垂直方向每 8 个像素点打包为 1 个字节 (Page 0 ~ Page 7 共 8 页)。
 * 3. 显存总容量: 128 列 * 8 页 = 1024 字节。
 * 4. 坐标 (x, y) 映射公式:
 *      • 字节索引: index = x + (y / 8) * 128
 *      • 位偏移量: bit_pos = y % 8
 *      • 点亮像素: buffer[index] |= (1 << bit_pos);
 *      • 熄灭像素: buffer[index] &= ~(1 << bit_pos);
 */

#include "oled_driver.hpp"

#include <cmath>
#include <cstring>

#include "esp_log.h"

static const char *TAG = "OLED_DRIVER";

OledDriver::OledDriver(int sda_pin, int scl_pin, uint8_t i2c_addr)
    : m_sdaPin(sda_pin),
      m_sclPin(scl_pin),
      m_i2cAddr(i2c_addr),
      m_busHandle(nullptr),
      m_devHandle(nullptr) {
    // 构造时将 1024 字节画布全部清零（黑屏）
    memset(m_buffer, 0, sizeof(m_buffer));
}

OledDriver::~OledDriver() {
    if (m_devHandle) {
        i2c_master_bus_rm_device(m_devHandle);
    }
    if (m_busHandle) {
        i2c_del_master_bus(m_busHandle);
    }
}

esp_err_t OledDriver::writeCommand(uint8_t cmd) {
    // SSD1306 I2C 协议: 首字节 0x00 (Co=0, D/C#=0) 表示接下来传输的是控制指令 (Command)
    uint8_t buf[2] = {0x00, cmd};
    return i2c_master_transmit(m_devHandle, buf, sizeof(buf), 100);
}

esp_err_t OledDriver::writeData(const uint8_t *data, size_t len) {
    // SSD1306 I2C 协议: 首字节 0x40 (Co=0, D/C#=1) 表示接下来传输的是显存点阵数据 (Data)
    uint8_t prefix = 0x40;
    i2c_master_transmit_multi_buffer_info_t buffers[2] = {
        {.write_buffer = &prefix, .buffer_size = 1},
        {.write_buffer = const_cast<uint8_t *>(data), .buffer_size = len}};
    return i2c_master_multi_buffer_transmit(m_devHandle, buffers, 2, 200);
}

esp_err_t OledDriver::init() {
    ESP_LOGI(TAG, "正在初始化 I2C 总线 (SDA: GPIO %d, SCL: GPIO %d)...", m_sdaPin, m_sclPin);

    // 1. 初始化 I2C 主机总线 (使用 ESP-IDF 5.x/6.x 新一代 Master Bus 驱动)
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = I2C_NUM_0;
    bus_cfg.sda_io_num = static_cast<gpio_num_t>(m_sdaPin);
    bus_cfg.scl_io_num = static_cast<gpio_num_t>(m_sclPin);
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;                // 滤除 7 个时钟周期以内的硬件毛刺
    bus_cfg.flags.enable_internal_pullup = true;  // 启用 ESP32 内部弱上拉

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &m_busHandle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C 总线创建失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. 挂载 OLED 从机设备 (7位从机地址 0x3C，400kHz Fast-mode 速率)
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = m_i2cAddr;
    dev_cfg.scl_speed_hz = 400000;  // 400 kHz

    ret = i2c_master_bus_add_device(m_busHandle, &dev_cfg, &m_devHandle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OLED 设备添加失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /**
     * 3. 发送 SSD1306 上电初始化指令序列
     * 详细指令解析参考手册: SSD1306 Datasheet Section 8 & Appendix
     */
    const uint8_t init_cmds[] = {
        // [手册 Section 8.1.13] Set Display OFF (AEh)
        // 关闭 OLED 面板显示，进入睡眠模式，防止初始化期间屏幕闪烁或显示乱码
        0xAE,

        // [手册 Section 8.1.14] Set Display Clock Divide Ratio/Oscillator Frequency (D5h)
        // 参数 0x80: 高4位 0x8 为振荡器中心频率；低4位 0x0 为时钟分频比 (Divide by 1)
        // 产生约 370kHz 的刷新率，兼顾显示稳定与低功耗
        0xD5, 0x80,

        // [手册 Section 8.1.5] Set Multiplex Ratio (A8h)
        // 参数 0x3F (十进制 63): 设置复用路数 = 63 + 1 = 64 路，对应 128x64 屏幕的 64 条扫描线
        0xA8, 0x3F,

        // [手册 Section 8.1.6] Set Display Offset (D3h)
        // 参数 0x00: 设置显示垂直偏移量为 0 行 (RAM 行 0 映射至 COM0)
        0xD3, 0x00,

        // [手册 Section 8.1.4] Set Display Start Line (40h ~ 7Fh)
        // 0x40 | 0: 设置显存起始行为 COM0，不进行垂直滚动
        0x40,

        // [手册 Appendix: Charge Pump Setting] (8Dh)
        // 参数 0x14 (二进制 0001 0100): 启用芯片内部升压电荷泵 (Charge Pump Enable)
        // ⚠️ 极其关键: 0.96寸模块由 3.3V 供电，必须开启内部升压电荷泵生成 7.5V
        // 驱动高压，否则点不亮屏幕
        0x8D, 0x14,

        // [手册 Section 8.1.1] Set Memory Addressing Mode (20h)
        // 参数 0x00: 设置为“水平寻址模式” (Horizontal Addressing Mode)
        // 每次写入字节后列地址自动 +1；写完一行末尾 (列127) 自动换到下一页起始列，适合 1024
        // 字节连续无缝推送
        0x20, 0x00,

        // [手册 Section 8.1.3] Set Segment Re-map (A0h / A1h)
        // 0xA1: 将列地址 127 映射到 SEG0 (水平方向左右镜像翻转)，适配排针朝上的正常视觉方向
        0xA1,

        // [手册 Section 8.1.10] Set COM Output Scan Direction (C0h / C8h)
        // 0xC8: 设置从 COM[N-1] 向 COM0 反向扫描 (垂直方向上下翻转)，与 0xA1 配合构成正立视角
        0xC8,

        // [手册 Section 8.1.11] Set COM Pins Hardware Configuration (DAh)
        // 参数 0x12 (二进制 0001 0010): Bit[4]=1 启用交错式/替代 COM 引脚配置，适配 128x64
        // 面板硬件连线
        0xDA, 0x12,

        // [手册 Section 8.1.2] Set Contrast Control (81h)
        // 参数 0xCF (十进制 207): 对比度等级 (0~255)，0xCF 为推荐的高对比度值，亮度充足且不过热
        0x81, 0xCF,

        // [手册 Section 8.1.15] Set Pre-charge Period (D9h)
        // 参数 0xF1: 高4位 0xF (15 DCLK) 为放电周期；低4位 0x1 (1 DCLK)
        // 为预充电周期，优化像素响应速度与清晰度
        0xD9, 0xF1,

        // [手册 Section 8.1.16] Set VCOMH Deselect Level (DBh)
        // 参数 0x40: 设置 VCOMH 稳压电压约为 0.83 * VCC，防止暗像素漏光
        0xDB, 0x40,

        // [手册 Section 8.1.7] Entire Display ON (A4h / A5h)
        // 0xA4: 恢复正常显示 (显示输出完全取决于 RAM 显存中的点阵数据；0xA5 则为强制全白测试模式)
        0xA4,

        // [手册 Section 8.1.8] Set Normal/Inverse Display (A6h / A7h)
        // 0xA6: 正常正显模式 (显存中 1 代表发光点亮，0 代表熄灭)
        0xA6,

        // [手册 Section 8.1.13] Set Display ON (AFh)
        // 开启主显示输出，正式点亮 OLED 屏幕
        0xAF};

    // 逐条向 OLED 发送初始化命令
    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        writeCommand(init_cmds[i]);
    }

    clear();
    update();
    ESP_LOGI(TAG, "✅ OLED 屏幕初始化成功！");
    return ESP_OK;
}

void OledDriver::clear() {
    // 将 1024 字节画布全部填 0 (全黑)
    memset(m_buffer, 0, sizeof(m_buffer));
}

void OledDriver::update() {
    // [手册 Section 8.1.9] Set Column Address (21h)
    // 设置写入的列范围: 0 ~ 127 列
    writeCommand(0x21);
    writeCommand(0);
    writeCommand(SCREEN_WIDTH - 1);

    // [手册 Section 8.1.9] Set Page Address (22h)
    // 设置写入的页范围: 0 ~ 7 页 (共 8 页 * 8 像素 = 64 像素高)
    writeCommand(0x22);
    writeCommand(0);
    writeCommand(7);

    // 一口气将 1024 字节显存流式刷入 OLED 硬件显存中
    writeData(m_buffer, sizeof(m_buffer));
}

void OledDriver::drawPixel(int x, int y, bool color) {
    // 边界安全检查，防止坐标越界污染内存
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT)
        return;

    if (color) {
        // 将对应字节的对应位 置 1
        m_buffer[x + (y / 8) * SCREEN_WIDTH] |= (1 << (y % 8));
    } else {
        // 将对应字节的对应位 清 0
        m_buffer[x + (y / 8) * SCREEN_WIDTH] &= ~(1 << (y % 8));
    }
}

void OledDriver::fillRect(int x, int y, int w, int h, bool color) {
    for (int i = x; i < x + w; i++) {
        for (int j = y; j < y + h; j++) {
            drawPixel(i, j, color);
        }
    }
}

void OledDriver::drawRoundRect(int x, int y, int w, int h, int r, bool color) {
    // 1. 绘制四条直边
    for (int i = x + r; i < x + w - r; i++) {
        drawPixel(i, y, color);          // 上横边
        drawPixel(i, y + h - 1, color);  // 下横边
    }
    for (int j = y + r; j < y + h - r; j++) {
        drawPixel(x, j, color);          // 左竖边
        drawPixel(x + w - 1, j, color);  // 右竖边
    }

    // 2. 绘制四个圆角的弧线边缘
    for (int dx = 0; dx <= r; dx++) {
        for (int dy = 0; dy <= r; dy++) {
            int d = dx * dx + dy * dy;
            if (d <= r * r && d >= (r - 1) * (r - 1)) {
                drawPixel(x + r - dx, y + r - dy, color);                  // 左上弧
                drawPixel(x + w - r - 1 + dx, y + r - dy, color);          // 右上弧
                drawPixel(x + r - dx, y + h - r - 1 + dy, color);          // 左下弧
                drawPixel(x + w - r - 1 + dx, y + h - r - 1 + dy, color);  // 右下弧
            }
        }
    }
}

void OledDriver::fillRoundRect(int x, int y, int w, int h, int r, bool color) {
    // 1. 填充十字形中心区域 (分为上、中、下三个矩形无缝拼合)
    fillRect(x + r, y, w - 2 * r, h, color);
    fillRect(x, y + r, r, h - 2 * r, color);
    fillRect(x + w - r, y + r, r, h - 2 * r, color);

    // 2. 利用圆的标准方程 (dx^2 + dy^2 <= r^2) 对四个角进行圆弧填充
    for (int dx = 0; dx <= r; dx++) {
        for (int dy = 0; dy <= r; dy++) {
            if (dx * dx + dy * dy <= r * r) {
                drawPixel(x + r - dx, y + r - dy, color);                  // 左上角圆弧
                drawPixel(x + w - r - 1 + dx, y + r - dy, color);          // 右上角圆弧
                drawPixel(x + r - dx, y + h - r - 1 + dy, color);          // 左下角圆弧
                drawPixel(x + w - r - 1 + dx, y + h - r - 1 + dy, color);  // 右下角圆弧
            }
        }
    }
}
