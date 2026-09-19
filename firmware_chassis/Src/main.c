/**
 * @file main.c
 * @brief 自平衡小车底盘主控程序 (里程碑 1 & 2：六轴传感器驱动)
 */

#include "bsp.h"
#include "bsp_spi.h"
#include "bsp_usart.h"  // 1. 引入串口驱动
#include "icm42605.h"
#include "log.h"  // 2. 引入日志抽象系统

int main(void) {
    // 1. 初始化系统基准 (160MHz 主频、1ms 滴答、PC13 蓝灯)
    bsp_init();

    bsp_usart2_init(115200);
    LOG_I("SYS", "========================================");
    LOG_I("SYS", " STM32G473 Chassis Firmware Initializing ");
    LOG_I("SYS", " Clock: 160MHz | USART2: 115200 8N1     ");
    LOG_I("SYS", "========================================");

    // 2. 初始化 SPI1 硬件外设
    bsp_spi1_init();
    delay_ms(50);

    // 3. 执行陀螺仪唤醒与全量初始化
    uint8_t chip_id = icm42605_read_who_am_i();
    LOG_I("IMU", "Reading Chip ID: 0x%02X (Expected HXY ID: 0x%02X)", chip_id, ICM42605_WHO_AM_I_VAL);

    if (icm42605_init()) {
        LOG_I("IMU", "HXY ICM-42605 / ICM-42650 Initialized & Powered ON Successfully!");
    } else {
        LOG_E("IMU", "IMU Initialization Failed! Please check SPI bus.");
    }

    // 4. 通关指示：蓝灯快闪 6 次，随后常亮 1 秒，标志底层硬件全面打通！
    for (int i = 0; i < 6; i++) {
        led_toggle();
        delay_ms(100);
    }
    led_set(true);
    delay_ms(1000);

    // 5. 正式进入 100Hz 底盘主控循环
    Icm42605Data_t imu_data;
    uint32_t print_counter = 0;

    while (1) {
        // 高速读取六轴加速度 (g) 与角速度 (deg/s) - 核心解算保持 100Hz 真实高频
        icm42605_read_data(&imu_data);

        // 如果需要使用 VOFA+ 示波器波形图，请解除下面一行的注释:
        // LOG_PLOT("%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n", imu_data.ax, imu_data.ay, imu_data.az, imu_data.gx, imu_data.gy, imu_data.gz);

        // 每 20 帧 (200ms = 5Hz) 打印一次人类可读日志，既流畅又清晰不晃眼
        if (++print_counter >= 20) {
            print_counter = 0;
            led_toggle();

            // 采用等宽格式化对齐 (+号、保留2位/1位小数)，让终端数字垂直整齐对齐，绝不乱跳
            LOG_I("IMU", "Acc:[%+5.2f, %+5.2f, %+5.2f]g | Gyro:[%+6.1f, %+6.1f, %+6.1f]dps",
                  imu_data.ax, imu_data.ay, imu_data.az,
                  imu_data.gx, imu_data.gy, imu_data.gz);
        }

        delay_ms(10);  // 100 Hz 周期节拍
    }

    return 0;
}
