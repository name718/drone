/**
 * @file motor.c
 * @brief TB6612 双路电机驱动实现
 */

#include "motor.h"

#include "stm32g4xx.h"

void motor_init(void) {
    // 1. 开启 GPIOA, GPIOB, GPIOC 与 TIM3 硬件外设时钟
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN | RCC_AHB2ENR_GPIOCEN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM3EN;

    // 2. 配置方向引脚为通用推挽输出 (PC8, PC9, PA15, PB0, PB1)
    // AIN1: PC8, AIN2: PC9
    GPIOC->MODER &= ~((3U << (8 * 2)) | (3U << (9 * 2)));
    GPIOC->MODER |= ((1U << (8 * 2)) | (1U << (9 * 2)));

    // BIN1: PA15
    GPIOA->MODER &= ~(3U << (15 * 2));
    GPIOA->MODER |= (1U << (15 * 2));

    // BIN2: PB0, STBY: PB1
    GPIOB->MODER &= ~((3U << (0 * 2)) | (3U << (1 * 2)));
    GPIOB->MODER |= ((1U << (0 * 2)) | (1U << (1 * 2)));

    // 3. 配置 PC6 (TIM3_CH1) 与 PC7 (TIM3_CH2) 为复用功能 AF2
    GPIOC->MODER &= ~((3U << (6 * 2)) | (3U << (7 * 2)));
    GPIOC->MODER |= ((2U << (6 * 2)) | (2U << (7 * 2)));  // 复用模式

    // PC6, PC7 在 AFR[0] 的高半区 (即 AFRL), AF2 为 TIM3
    GPIOC->AFR[0] &= ~((0xFU << (6 * 4)) | (0xFU << (7 * 4)));
    GPIOC->AFR[0] |= ((2U << (6 * 4)) | (2U << (7 * 4)));

    // 4. 配置 TIM3 产生 20kHz 硬件 PWM:
    // 定时器输入时钟 = 160MHz
    // PSC = 7 -> 分频后时钟 = 160MHz / (7 + 1) = 20MHz
    // ARR = 999 -> PWM 周期 = 20MHz / (999 + 1) = 20kHz (超声波静音频率)
    TIM3->PSC = 7;
    TIM3->ARR = MOTOR_MAX_PWM - 1;  // 999
    TIM3->CNT = 0;

    // 配置 CH1 和 CH2 为 PWM 模式 1 (OC1M = 0110b, OC2M = 0110b) 并使能预装载
    TIM3->CCMR1 =
        (6U << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE | (6U << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE;

    // 使能通道 1 与通道 2 输出
    TIM3->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E;

    // 启动定时器
    TIM3->CR1 |= TIM_CR1_CEN | TIM_CR1_ARPE;

    // 5. 初始刹车并唤醒 TB6612 (STBY 置 1)
    motor_stop();
    GPIOB->BSRR = (1U << 1);  // PB1 (STBY) 置高，解除休眠
}

void motor_set_speed(int16_t left_speed, int16_t right_speed) {
    // 解决两轮对称背靠背安装导致的物理镜像问题：
    // 统一约定：正数 = 前进，负数 = 后退
    left_speed = -left_speed;
    // 限幅保护：-1000 ~ +1000
    if (left_speed > MOTOR_MAX_PWM)
        left_speed = MOTOR_MAX_PWM;
    if (left_speed < -MOTOR_MAX_PWM)
        left_speed = -MOTOR_MAX_PWM;
    if (right_speed > MOTOR_MAX_PWM)
        right_speed = MOTOR_MAX_PWM;
    if (right_speed < -MOTOR_MAX_PWM)
        right_speed = -MOTOR_MAX_PWM;

    // --- 左电机控制 (Motor A: PC6 PWM, PC8/PC9 方向) ---
    if (left_speed > 0) {
        // 正转: AIN1 = 1, AIN2 = 0
        GPIOC->BSRR = (1U << 8) | (1U << (9 + 16));
        TIM3->CCR1 = (uint32_t)left_speed;
    } else if (left_speed < 0) {
        // 反转: AIN1 = 0, AIN2 = 1
        GPIOC->BSRR = (1U << (8 + 16)) | (1U << 9);
        TIM3->CCR1 = (uint32_t)(-left_speed);
    } else {
        // 停止/滑行
        GPIOC->BSRR = (1U << (8 + 16)) | (1U << (9 + 16));
        TIM3->CCR1 = 0;
    }

    // --- 右电机控制 (Motor B: PC7 PWM, PA15/PB0 方向) ---
    if (right_speed > 0) {
        // 正转: BIN1 = 1, BIN2 = 0
        GPIOA->BSRR = (1U << 15);
        GPIOB->BSRR = (1U << (0 + 16));
        TIM3->CCR2 = (uint32_t)right_speed;
    } else if (right_speed < 0) {
        // 反转: BIN1 = 0, BIN2 = 1
        GPIOA->BSRR = (1U << (15 + 16));
        GPIOB->BSRR = (1U << 0);
        TIM3->CCR2 = (uint32_t)(-right_speed);
    } else {
        // 停止/滑行
        GPIOA->BSRR = (1U << (15 + 16));
        GPIOB->BSRR = (1U << (0 + 16));
        TIM3->CCR2 = 0;
    }
}

void motor_stop(void) {
    // 两路输出全部归零并拉低方向引脚
    TIM3->CCR1 = 0;
    TIM3->CCR2 = 0;
    GPIOC->BSRR = (1U << (8 + 16)) | (1U << (9 + 16));
    GPIOA->BSRR = (1U << (15 + 16));
    GPIOB->BSRR = (1U << (0 + 16));
}
