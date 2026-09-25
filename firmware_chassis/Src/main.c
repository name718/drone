/**
 * @file main.c
 * @brief 自平衡小车底盘主控程序 (FreeRTOS 多任务版本)
 */

#include "bsp.h"
#include "bsp_spi.h"
#include "bsp_usart.h"
#include "icm42605.h"
#include "log.h"

// 引入 FreeRTOS 核心头文件
#include "FreeRTOS.h"
#include "task.h"

// 全局姿态数据缓存
static Icm42605Data_t s_imu_data;

/**
 * @brief 核心姿态控制任务 (硬实时 100Hz，优先级：High = 5)
 */
static void Task_Control(void *pvParameters) {
    (void)pvParameters;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(10);  // 严格 10ms (100Hz)

    while (1) {
        // 绝对精准周期延时 (无漂移)
        vTaskDelayUntil(&xLastWakeTime, xPeriod);

        // 读取六轴姿态数据 (后续在这里加入卡尔曼滤波与自平衡 PID 控制)
        icm42605_read_data(&s_imu_data);
    }
}

/**
 * @brief 遥测与状态指示任务 (5Hz，优先级：Low = 2)
 */
static void Task_Telemetry(void *pvParameters) {
    (void)pvParameters;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(200));  // 200ms (5Hz) 周期

        led_toggle();

        LOG_I("IMU", "Acc:[%+5.2f, %+5.2f, %+5.2f]g | Gyro:[%+6.1f, %+6.1f, %+6.1f]dps",
              s_imu_data.ax, s_imu_data.ay, s_imu_data.az, s_imu_data.gx, s_imu_data.gy,
              s_imu_data.gz);
    }
}

int main(void) {
    // 1. 底层硬件基准初始化 (160MHz、串口、SPI)
    bsp_init();
    bsp_usart2_init(115200);

    LOG_I("SYS", "========================================");
    LOG_I("SYS", " STM32G473 Chassis Firmware (FreeRTOS)  ");
    LOG_I("SYS", " Clock: 160MHz | Kernel: FreeRTOS V10.5 ");
    LOG_I("SYS", "========================================");

    bsp_spi1_init();
    delay_ms(50);

    // 2. 唤醒并校验 IMU 传感器
    if (icm42605_init()) {
        LOG_I("IMU", "HXY ICM-42605 Initialized Successfully!");
    } else {
        LOG_E("IMU", "IMU Initialization Failed!");
    }

    // 3. 创建 FreeRTOS 任务
    // 任务1: 控制任务 (栈大小 256 字 = 1024 字节，优先级 5)
    xTaskCreate(Task_Control, "Control", 256, NULL, 5, NULL);

    // 任务2: 遥测任务 (栈大小 256 字 = 1024 字节，优先级 2)
    xTaskCreate(Task_Telemetry, "Telemetry", 256, NULL, 2, NULL);

    LOG_I("SYS", "Starting FreeRTOS Scheduler...");

    // 4. 启动操作系统调度器 (正常情况下永远不会返回)
    vTaskStartScheduler();

    // 如果执行到这里，说明内存堆不足导致调度器启动失败
    while (1) {
        led_toggle();
        delay_ms(50);
    }

    return 0;
}
