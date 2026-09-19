/**
 * @file icm42605.c
 * @brief ICM-42605 六轴姿态传感器驱动实现
 */

#include "icm42605.h"

#include "bsp.h"  // 提供 delay_ms 延时支持
#include "bsp_spi.h"

// --- 底层寄存器读写帮助函数 (静态局部函数，对外隐藏) ---

/**
 * @brief 向指定寄存器写入 1 字节
 *        (Bit 7 = 0 表示写操作: reg & 0x7F)
 */
static void icm_write_reg(uint8_t reg, uint8_t val) {
    bsp_spi1_cs_set(false);           // 1. 拉低片选，开始通信
    bsp_spi1_swap_byte(reg & 0x7FU);  // 2. 发送寄存器写地址
    bsp_spi1_swap_byte(val);          // 3. 发送写入数据
    bsp_spi1_cs_set(true);            // 4. 拉高片选，结束通信
}

/**
 * @brief 从指定寄存器读取 1 字节
 *        (Bit 7 = 1 表示读操作: reg | 0x80)
 */
static uint8_t icm_read_reg(uint8_t reg) {
    bsp_spi1_cs_set(false);                  // 1. 拉低片选
    bsp_spi1_swap_byte(reg | 0x80U);         // 2. 发送寄存器读地址
    uint8_t val = bsp_spi1_swap_byte(0xFF);  // 3. 发送空字节推回数据
    bsp_spi1_cs_set(true);                   // 4. 拉高片选
    return val;
}

/**
 * @brief 连续突发读取多个连续寄存器 (Burst Read)
 */
static void icm_read_regs(uint8_t reg, uint8_t *buf, uint16_t len) {
    bsp_spi1_cs_set(false);
    bsp_spi1_swap_byte(reg | 0x80U);  // 发送起始读地址
    bsp_spi1_transfer(0, buf, len);   // 连续推时钟读取 len 字节
    bsp_spi1_cs_set(true);
}

// --- 对外公开 API 实现 ---

uint8_t icm42605_read_who_am_i(void) {
    return icm_read_reg(ICM42605_REG_WHO_AM_I);
}

bool icm42605_init(void) {
    // 1. 连续 5 次片选脉冲，彻底唤醒并锁定 SPI 模式
    for (int i = 0; i < 5; i++) {
        bsp_spi1_cs_set(false);
        delay_ms(1);
        bsp_spi1_cs_set(true);
        delay_ms(1);
    }

    // 2. 选入 Bank 0 并软复位
    icm_write_reg(0x76, 0x00);
    delay_ms(5);
    icm_write_reg(ICM42605_REG_DEVICE_CONFIG, 0x01);
    delay_ms(50);

    // 3. 再次确保选回 Bank 0
    icm_write_reg(0x76, 0x00);
    delay_ms(5);

    // 4. 开启传感器电源 (陀螺仪与加速度计 Low Noise 模式)
    icm_write_reg(ICM42605_REG_PWR_MGMT0, 0x0F);
    delay_ms(50);  // 等待微机械起振稳定

    // 5. 配置量程与采样率 (±8g, ±2000dps, 200Hz)
    icm_write_reg(ICM42605_REG_ACCEL_CONFIG0, 0x27);
    icm_write_reg(ICM42605_REG_GYRO_CONFIG0, 0x07);
    delay_ms(10);

    return true;
}

bool icm42605_read_data(Icm42605Data_t *data) {
    if (!data)
        return false;

    // 从 ACCEL_DATA_X1 (0x1F) 开始，连续读取 12 字节:
    // [0~5]: Accel X, Y, Z (各2字节，大端格式高字节在前)
    // [6~11]: Gyro X, Y, Z  (各2字节，大端格式高字节在前)
    uint8_t raw_buf[12];
    icm_read_regs(ICM42605_REG_ACCEL_DATA_X1, raw_buf, 12);

    // 拼装 16 位有符号原始数据
    data->accel_x_raw = (int16_t)((raw_buf[0] << 8) | raw_buf[1]);
    data->accel_y_raw = (int16_t)((raw_buf[2] << 8) | raw_buf[3]);
    data->accel_z_raw = (int16_t)((raw_buf[4] << 8) | raw_buf[5]);

    data->gyro_x_raw = (int16_t)((raw_buf[6] << 8) | raw_buf[7]);
    data->gyro_y_raw = (int16_t)((raw_buf[8] << 8) | raw_buf[9]);
    data->gyro_z_raw = (int16_t)((raw_buf[10] << 8) | raw_buf[11]);

    // 换算物理单位:
    // ±8g 灵敏度: 32768 / 8 = 4096 LSB/g (乘以 1/4096 = 0.00024414f)
    const float ACCEL_SCALE = 8.0f / 32768.0f;
    data->ax = (float)data->accel_x_raw * ACCEL_SCALE;
    data->ay = (float)data->accel_y_raw * ACCEL_SCALE;
    data->az = (float)data->accel_z_raw * ACCEL_SCALE;

    // ±2000 dps 灵敏度: 32768 / 2000 = 16.384 LSB/(deg/s) (乘以 1/16.384 = 0.061035f)
    const float GYRO_SCALE = 2000.0f / 32768.0f;
    data->gx = (float)data->gyro_x_raw * GYRO_SCALE;
    data->gy = (float)data->gyro_y_raw * GYRO_SCALE;
    data->gz = (float)data->gyro_z_raw * GYRO_SCALE;

    return true;
}
