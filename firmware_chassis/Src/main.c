/**
 * @file main.c
 * @brief 自平衡小车底盘主控程序 (里程碑 1 & 2：六轴传感器驱动)
 */

#include "bsp.h"
#include "bsp_spi.h"
#include "icm42605.h"

int main(void) {
    // 1. 初始化系统基准 (160MHz 主频、1ms 滴答、PC13 蓝灯)
    bsp_init();

    // 2. 初始化 SPI1 硬件外设
    bsp_spi1_init();
    delay_ms(50);

    // 3. 执行陀螺仪唤醒与全量初始化
    icm42605_init();

    // 4. 通关指示：蓝灯快闪 6 次，随后常亮 1 秒，标志底层硬件全面打通！
    for (int i = 0; i < 6; i++) {
        led_toggle();
        delay_ms(100);
    }
    led_set(true);
    delay_ms(1000);

    // 5. 正式进入 100Hz 底盘主控循环
    Icm42605Data_t imu_data;
    uint32_t heart_beat = 0;

    while (1) {
        // 高速读取六轴加速度 (g) 与角速度 (deg/s)
        icm42605_read_data(&imu_data);

        // 每 25 帧 (250ms) 翻转一次 LED，作为正常运行的心跳指示
        if (++heart_beat >= 25) {
            heart_beat = 0;
            led_toggle();
        }

        delay_ms(10);  // 100 Hz 周期节拍
    }

    return 0;
}
