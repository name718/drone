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

        // 4. 【控制模式】：自平衡已停止，完全由上位机接管差速控制与安全防冲
        int16_t cmd_speed = 0;
        int16_t cmd_yaw = 0;
        uint8_t cmd_mode = 0;
        bool is_online = bsp_usart2_get_cmd(&cmd_speed, &cmd_yaw, &cmd_mode);

        if (!is_online || cmd_mode == 0 || cmd_mode == 2 || (cmd_speed == 0 && cmd_yaw == 0)) {
            // 上位机离线 (>500ms)、处于待机/急停模式或发送速度为 0 -> 电机完全刹车停机
            motor_stop();
        } else if (cmd_mode == 1) {
            // 上位机使能运行模式：两轮差速驱动运动学转换
            // 目标线速度 (mm/s, e.g. 200) 乘以 1.5 得到基准 PWM (~300)
            // 目标角速度 (mrad/s, e.g. 500) 乘以 0.4 得到转向差值 PWM (~200)
            float base = (float)cmd_speed * 1.5f;
            float diff = (float)cmd_yaw * 0.4f;

            float left_pwm = base - diff;
            float right_pwm = base + diff;

            // 死区补偿：克服 TB6612 驱动芯片与 N20 减速箱静摩擦阻力 (75 点)
            if (left_pwm > 5.0f) {
                left_pwm += 75.0f;
            } else if (left_pwm < -5.0f) {
                left_pwm -= 75.0f;
            }

            if (right_pwm > 5.0f) {
                right_pwm += 75.0f;
            } else if (right_pwm < -5.0f) {
                right_pwm -= 75.0f;
            }

            motor_set_speed((int16_t)left_pwm, (int16_t)right_pwm);
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

    // 自平衡模式已停用，切换为上位机全权直控差速模式
    LOG_I("控制", "自平衡功能已停止，底盘已进入【上位机全权差速直控模式】！");

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
