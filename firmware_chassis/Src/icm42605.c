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
    uint8_t val = bsp_spi1_swap_byte(0x00);  // 3. 发送空字节推回数据
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

uint8_t icm42605_read_reg_test(uint8_t reg) {
    return icm_read_reg(reg);
}

bool icm42605_init(void) {
    // 1. 连续 5 次片选脉冲，锁定 SPI 模式
    for (int i = 0; i < 5; i++) {
        bsp_spi1_cs_set(false);
        delay_ms(1);
        bsp_spi1_cs_set(true);
        delay_ms(1);
    }

    // 2. 软复位芯片 (向 0x4A SOFT_RST 写入 0xA5)
    icm_write_reg(ICM42605_REG_SOFT_RST, 0xA5);
    delay_ms(50);  // 复位后等待内部电路与修调参数重载

    // 3. 复位后再次片选脉冲，确保锁定在 SPI 模式
    for (int i = 0; i < 5; i++) {
        bsp_spi1_cs_set(false);
        delay_ms(1);
        bsp_spi1_cs_set(true);
        delay_ms(1);
    }

    // 4. 确保切入 Bank 0 (通用寄存器段)
    icm_write_reg(ICM42605_REG_SEG_SEL, 0x00);
    delay_ms(2);

    // 5. 校验芯片身份 ID (华轩阳 HXY 芯片 ID 固定为 0x6A)
    uint8_t id = icm42605_read_who_am_i();
    if (id != ICM42605_WHO_AM_I_VAL) {
        return false;
    }

    // 6. 开启传感器电源: 手册规定 0x7D 要先写入 0x0E 并延时 10ms 之后再进行其他配置
    //    0x0E = (TEMP_EN | ACC_EN | GYR_EN)
    icm_write_reg(ICM42605_REG_PWR_CTRL, 0x0E);
    delay_ms(50);  // 等待微机械陀螺仪与加速度计稳定起振

    // 7. 配置加速度计: 0x40 (ACC_CONF) = 0xA9 (高性能, NORM_AVG4 滤波, 200Hz)
    //                0x41 (ACC_RANGE) = 0x02 (±8g 量程)
    icm_write_reg(ICM42605_REG_ACC_CONF, 0xA9);
    delay_ms(2);
    icm_write_reg(ICM42605_REG_ACC_RANGE, 0x02);
    delay_ms(2);

    // 8. 配置陀螺仪: 0x42 (GYR_CONF) = 0xA9 (高性能, NORM_AVG4 滤波, 200Hz)
    //              0x43 (GYR_RANGE) = 0x00 (±2000 dps 量程)
    icm_write_reg(ICM42605_REG_GYR_CONF, 0xA9);
    delay_ms(2);
    icm_write_reg(ICM42605_REG_GYR_RANGE, 0x00);
    delay_ms(10);

    return true;
}

bool icm42605_read_data(Icm42605Data_t *data) {
    if (!data)
        return false;

    // 从 ACC_XH (0x0C) 开始，连续突发读取 12 字节:
    // [0~5]:   ACC_X, ACC_Y, ACC_Z (各2字节，大端格式高字节在前)
    // [6~11]:  GYR_X, GYR_Y, GYR_Z (各2字节，大端格式高字节在前)
    uint8_t raw_buf[12];
    icm_read_regs(ICM42605_REG_ACC_DATA_START, raw_buf, 12);

    // 拼装 16 位有符号原始数据
    data->accel_x_raw = (int16_t)((raw_buf[0] << 8) | raw_buf[1]);
    data->accel_y_raw = (int16_t)((raw_buf[2] << 8) | raw_buf[3]);
    data->accel_z_raw = (int16_t)((raw_buf[4] << 8) | raw_buf[5]);

    data->gyro_x_raw = (int16_t)((raw_buf[6] << 8) | raw_buf[7]);
    data->gyro_y_raw = (int16_t)((raw_buf[8] << 8) | raw_buf[9]);
    data->gyro_z_raw = (int16_t)((raw_buf[10] << 8) | raw_buf[11]);

    // 换算物理单位:
    // ±8g 灵敏度: 32768 / 8 = 4096 LSB/g (系数 = 8.0f / 32768.0f)
    const float ACCEL_SCALE = 8.0f / 32768.0f;
    data->ax = (float)data->accel_x_raw * ACCEL_SCALE;
    data->ay = (float)data->accel_y_raw * ACCEL_SCALE;
    data->az = (float)data->accel_z_raw * ACCEL_SCALE;

    // ±2000 dps 灵敏度: 32768 / 2000 = 16.384 LSB/(deg/s) (系数 = 2000.0f / 32768.0f)
    const float GYRO_SCALE = 2000.0f / 32768.0f;
    data->gx = (float)data->gyro_x_raw * GYRO_SCALE;
    data->gy = (float)data->gyro_y_raw * GYRO_SCALE;
    data->gz = (float)data->gyro_z_raw * GYRO_SCALE;

    return true;
}
