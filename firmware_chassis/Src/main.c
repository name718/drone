/**
 * @file main.c
 * @brief 自平衡小车底盘主控程序 (FreeRTOS 多任务版本)
 */

#include "attitude.h"
#include "bsp.h"
#include "bsp_spi.h"
#include "bsp_usart.h"
#include "control.h"
#include "encoder.h"
#include "icm42605.h"
#include "log.h"
#include "motor.h"

// 引入 FreeRTOS 核心头文件
#include "FreeRTOS.h"
#include "task.h"

// --- 全局变量区增加轮速缓存 ---
static volatile int16_t s_speed_left = 0;
static volatile int16_t s_speed_right = 0;

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
        vTaskDelayUntil(&xLastWakeTime, xPeriod);

        // 1. 高速读取六轴原始物理量
        icm42605_read_data(&s_imu_data);

        // 2. 100Hz 互补滤波姿态解算
        attitude_update(&s_imu_data, 0.01f);

        // 3. 100Hz 采样左右轮正交编码器增量 (脉冲数/10ms)
        encoder_get_speed((int16_t *)&s_speed_left, (int16_t *)&s_speed_right);

        // 4. 【核心控制闭环】：执行 100Hz 自平衡算法计算步进
        const Attitude_t *att = attitude_get();
        int16_t pwm_l = 0;
        int16_t pwm_r = 0;

        // 如果在正常站立姿态角范围内，正常驱动电机；如果跌倒或悬空，自动刹车
        if (control_step(att->pitch, att->pitch_rate, s_speed_left, s_speed_right, &pwm_l,
                         &pwm_r)) {
            motor_set_speed(pwm_l, pwm_r);
        } else {
            motor_stop();
        }
    }
}

/**
 * @brief 遥测与波形绘制任务 (50Hz，优先级：Low = 2)
 */
static void Task_Telemetry(void *pvParameters) {
    (void)pvParameters;
    uint32_t led_count = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(20));  // 20ms (50Hz)

        if (++led_count >= 25) {
            led_count = 0;
            led_toggle();
        }

        // 推送给 VOFA+ (FireWater 协议):
        // 通道 0: Pitch 滤波姿态角度
        // 通道 1: 加速度计原始静态角度
        // 通道 2: 俯仰角速度 PitchRate
        // 通道 3: 左轮速度 Speed_L (脉冲/10ms)
        // 通道 4: 右轮速度 Speed_R (脉冲/10ms)
        const Attitude_t *att = attitude_get();
        LOG_PLOT("%.2f,%.2f,%.2f,%d,%d", att->pitch, att->acc_pitch, att->pitch_rate, s_speed_left,
                 s_speed_right);
    }
}

int main(void) {
    // 1. 底层硬件基准初始化 (160MHz、串口、SPI)
    bsp_init();
    bsp_usart2_init(460800);

    LOG_I("系统", "========================================");
    LOG_I("系统", " 🏎️ STM32G473 底盘固件启动 (FreeRTOS)    ");
    LOG_I("系统", " 主频: 160MHz | 实时内核: FreeRTOS V10.5 ");
    LOG_I("系统", "========================================");

    bsp_spi1_init();
    delay_ms(50);

    // 2. 唤醒并校验 IMU 传感器
    if (icm42605_init()) {
        LOG_I("姿态", "ICM-42605 六轴陀螺仪初始化成功！");
        // 开机静止 1 秒自动校准陀螺仪零漂
        attitude_init();
    } else {
        LOG_E("姿态", "ICM-42605 六轴陀螺仪初始化失败！");
    }

    motor_init();
    LOG_I("电机", "TB6612 电机驱动硬件就绪");

    encoder_init();
    LOG_I("编码器", "TIM2(左) 与 TIM1(右) 硬件正交编码器初始化完成！");

    // 【新增】：初始化自平衡控制器 (装载基准 PID 与安全保护阈值)
    control_init();
    LOG_I("控制", "串级自平衡 PID 控制器就绪 (跌倒保护阈值: ±35°)");

    // 3. 创建 FreeRTOS 任务
    // 任务1: 控制任务 (栈大小 256 字 = 1024 字节，优先级 5)
    xTaskCreate(Task_Control, "Control", 256, NULL, 5, NULL);

    // 任务2: 遥测任务 (栈大小 256 字 = 1024 字节，优先级 2)
    xTaskCreate(Task_Telemetry, "Telemetry", 256, NULL, 2, NULL);

    LOG_I("系统", "正在启动 FreeRTOS 任务调度器...");

    // 4. 启动操作系统调度器 (正常情况下永远不会返回)
    vTaskStartScheduler();

    // 如果执行到这里，说明内存堆不足导致调度器启动失败
    while (1) {
        led_toggle();
        delay_ms(50);
    }

    return 0;
}
