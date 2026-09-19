/**
 * @file icm42605.h
 * @brief 华轩阳 HXY ICM-42605-HXY (ICM-42650) 六轴姿态传感器驱动头文件
 */
#ifndef ICM42605_H
#define ICM42605_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- HXY ICM-42605-HXY / ICM-42650 核心寄存器地址字典 ---
#define ICM42605_REG_WHO_AM_I       0x01  // 芯片身份识别寄存器 (固定值 0x6A)
#define ICM42605_REG_COM_CFG        0x05  // 通信控制 (BDU, Addr_Auto)
#define ICM42605_REG_DATA_STAT      0x0B  // 数据就绪状态
#define ICM42605_REG_ACC_DATA_START 0x0C  // 姿态数据起始地址 (连续12字节: ACC_X/Y/Z + GYR_X/Y/Z)
#define ICM42605_REG_ACC_CONF       0x40  // 加速度计配置 (滤波模式与 ODR 输出速率)
#define ICM42605_REG_ACC_RANGE      0x41  // 加速度计量程 (0x02: ±8g)
#define ICM42605_REG_GYR_CONF       0x42  // 陀螺仪配置 (滤波模式与 ODR 输出速率)
#define ICM42605_REG_GYR_RANGE      0x43  // 陀螺仪量程 (0x00: ±2000 dps)
#define ICM42605_REG_FIFO_DOWNS     0x45  // FIFO 降采样配置 (默认 0x88)
#define ICM42605_REG_SOFT_RST       0x4A  // 软复位寄存器 (写入 0xA5 触发芯片整机复位)
#define ICM42605_REG_PWR_CTRL       0x7D  // 核心电源管理寄存器 (写入 0x0E 唤醒 TEMP+ACC+GYR)
#define ICM42605_REG_SEG_SEL        0x7F  // 寄存器段选择 (写入 0x00 访问通用寄存器段)

#define ICM42605_WHO_AM_I_VAL       0x6A  // 华轩阳 HXY 芯片正确 ID

/**
 * @brief 六轴传感器物理量数据结构体
 */
typedef struct {
    // 原始 16 位 ADC 计数
    int16_t accel_x_raw;
    int16_t accel_y_raw;
    int16_t accel_z_raw;
    int16_t gyro_x_raw;
    int16_t gyro_y_raw;
    int16_t gyro_z_raw;

    // 转换后的真实物理量
    float ax;  // 加速度 X (单位: g)
    float ay;  // 加速度 Y (单位: g)
    float az;  // 加速度 Z (单位: g)
    float gx;  // 角速度 X (单位: deg/s)
    float gy;  // 角速度 Y (单位: deg/s，前后倾倒角速度，平衡车命脉)
    float gz;  // 角速度 Z (单位: deg/s，自转航向角速度)
} Icm42605Data_t;

/**
 * @brief 读取芯片身份 ID
 * @return uint8_t 正常应返回 0x6A
 */
uint8_t icm42605_read_who_am_i(void);

/**
 * @brief 读取任意寄存器用于底层验证
 */
uint8_t icm42605_read_reg_test(uint8_t reg);

/**
 * @brief 初始化 ICM-42605-HXY 传感器
 *        - 软复位芯片 (写 0x4A = 0xA5)
 *        - 唤醒传感器核心 (写 0x7D = 0x0E)
 *        - 配置量程: 陀螺仪 ±2000 dps, 加速度计 ±8g
 *        - 配置输出速率: 200Hz 高性能滤波模式
 * @return true 初始化成功; false 通信失败或 ID 不匹配
 */
bool icm42605_init(void);

/**
 * @brief 一次性突发读取 6 轴加速度与角速度原始数据并换算物理量
 * @param data 输出数据结构体指针
 * @return true 读取成功; false 读取失败
 */
bool icm42605_read_data(Icm42605Data_t *data);

#ifdef __cplusplus
}
#endif
#endif
