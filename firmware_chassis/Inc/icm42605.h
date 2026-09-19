/**
 * @file icm42605.h
 * @brief ICM-42605 六轴姿态传感器驱动头文件
 */
#ifndef ICM42605_H
#define ICM42605_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
// --- ICM-42605 核心寄存器地址字典 ---
#define ICM42605_REG_DEVICE_CONFIG 0x11  // 软复位寄存器
#define ICM42605_REG_TEMP_DATA1 0x1D     // 温度数据高字节
#define ICM42605_REG_ACCEL_DATA_X1 0x1F  // 加速度计 X 轴高字节 (起始连续 12 字节)
#define ICM42605_REG_PWR_MGMT0 0x4E      // 电源管理寄存器 (开启陀螺仪/加速度计)
#define ICM42605_REG_GYRO_CONFIG0 0x4F   // 陀螺仪量程与采样率配置
#define ICM42605_REG_ACCEL_CONFIG0 0x50  // 加速度计量程与采样率配置
#define ICM42605_REG_WHO_AM_I 0x75       // 芯片身份识别寄存器 (固定值 0x42)

#define ICM42605_WHO_AM_I_VAL 0x42  // 正确的芯片 ID

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
 * @brief 读取 ICM-42605 芯片身份 ID
 * @return uint8_t 正常应返回 0x42
 */
uint8_t icm42605_read_who_am_i(void);

/**
 * @brief 初始化 ICM-42605 传感器
 *        - 软复位芯片
 *        - 配置量程: 陀螺仪 ±2000 dps, 加速度计 ±8g
 *        - 配置输出速率: 200Hz Low Noise 低噪模式
 * @return true 初始化成功; false 通信失败或 ID 不匹配
 */
bool icm42605_init(void);

/**
 * @brief 一次性突发读取 (Burst Read) 6 轴加速度与角速度原始数据并换算物理量
 * @param data 输出数据结构体指针
 * @return true 读取成功; false 读取失败
 */
bool icm42605_read_data(Icm42605Data_t *data);
#ifdef __cplusplus
}
#endif
#endif
