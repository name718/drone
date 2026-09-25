/**
 * @file encoder.c
 * @brief STM32G473 硬件正交编码器驱动 (TIM2-左轮 32位 / TIM1-右轮 16位)
 */

#include "encoder.h"

#include "stm32g4xx.h"

void encoder_init(void) {
    // 1. 开启 GPIOA, TIM2 (APB1), TIM1 (APB2) 外设时钟
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM2EN;
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;

    // 2. 配置 PA0, PA1 为复用模式 AF1 (TIM2_CH1, TIM2_CH2)
    GPIOA->MODER &= ~((3U << (0 * 2)) | (3U << (1 * 2)));
    GPIOA->MODER |= ((2U << (0 * 2)) | (2U << (1 * 2)));
    // 开启内部弱上拉，防止编码器悬空抖动
    GPIOA->PUPDR &= ~((3U << (0 * 2)) | (3U << (1 * 2)));
    GPIOA->PUPDR |= ((1U << (0 * 2)) | (1U << (1 * 2)));
    // PA0, PA1 在 AFRL (AFR[0])，配置为 AF1
    GPIOA->AFR[0] &= ~((0xFU << (0 * 4)) | (0xFU << (1 * 4)));
    GPIOA->AFR[0] |= ((1U << (0 * 4)) | (1U << (1 * 4)));

    // 3. 配置 PA8, PA9 为复用模式 AF6 (TIM1_CH1, TIM1_CH2)
    GPIOA->MODER &= ~((3U << (8 * 2)) | (3U << (9 * 2)));
    GPIOA->MODER |= ((2U << (8 * 2)) | (2U << (9 * 2)));
    // 开启内部弱上拉
    GPIOA->PUPDR &= ~((3U << (8 * 2)) | (3U << (9 * 2)));
    GPIOA->PUPDR |= ((1U << (8 * 2)) | (1U << (9 * 2)));
    // PA8, PA9 在 AFRH (AFR[1])，配置为 AF6
    GPIOA->AFR[1] &= ~((0xFU << ((8 - 8) * 4)) | (0xFU << ((9 - 8) * 4)));
    GPIOA->AFR[1] |= ((6U << ((8 - 8) * 4)) | (6U << ((9 - 8) * 4)));

    // 4. 配置 TIM2 (左轮 32-bit): 正交编码器模式 3 (双边沿 4 倍频)
    TIM2->PSC = 0;
    TIM2->ARR = 0xFFFFFFFF;
    // CC1S = 01 (TI1 输入), CC2S = 01 (TI2 输入)
    TIM2->CCMR1 = (1U << TIM_CCMR1_CC1S_Pos) | (1U << TIM_CCMR1_CC2S_Pos);
    // CC1P = 0, CC2P = 0 (不反相，上升沿捕获)
    TIM2->CCER = 0;
    // SMS = 0011b (Encoder mode 3: 双通道四倍频计数)
    TIM2->SMCR = (3U << TIM_SMCR_SMS_Pos);
    TIM2->CNT = 0;
    TIM2->CR1 |= TIM_CR1_CEN;

    // 5. 配置 TIM1 (右轮 16-bit): 正交编码器模式 3 (双边沿 4 倍频)
    TIM1->PSC = 0;
    TIM1->ARR = 0xFFFF;
    TIM1->CCMR1 = (1U << TIM_CCMR1_CC1S_Pos) | (1U << TIM_CCMR1_CC2S_Pos);
    TIM1->CCER = 0;
    TIM1->SMCR = (3U << TIM_SMCR_SMS_Pos);
    TIM1->CNT = 0;
    TIM1->CR1 |= TIM_CR1_CEN;
}

void encoder_get_speed(int16_t *left_speed, int16_t *right_speed) {
    if (!left_speed || !right_speed)
        return;

    // 左轮保持不变 (向前为正)
    *left_speed = (int16_t)(TIM2->CNT);
    TIM2->CNT = 0;

    // 右轮取反：使左右两轮向前推时均为正数
    *right_speed = -(int16_t)(TIM1->CNT);
    TIM1->CNT = 0;
}
