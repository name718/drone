#include "bsp_usart.h"

#include <stdio.h>

#include "bsp.h"
#include "stm32g4xx.h"

void bsp_usart2_init(uint32_t baud_rate) {
    // 1. 开启 GPIOA 与 USART2 外设时钟
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;

    // 2. 配置 PA2 (TX) 和 PA3 (RX) 为复用模式 (MODER = 10b)
    GPIOA->MODER &= ~((3U << (2 * 2)) | (3U << (3 * 2)));
    GPIOA->MODER |= ((2U << (2 * 2)) | (2U << (3 * 2)));

    // 配置速度为超高速 (Very High Speed)
    GPIOA->OSPEEDR |= ((3U << (2 * 2)) | (3U << (3 * 2)));

    // 配置引脚复用映射寄存器 AFR (AFR[0] 对应 PA0~PA7，AF7 对应 USART2)
    GPIOA->AFR[0] &= ~((0xFU << (2 * 4)) | (0xFU << (3 * 4)));
    GPIOA->AFR[0] |= ((7U << (2 * 4)) | (7U << (3 * 4)));

    // 3. 配置波特率发生器 BRR
    // 默认时钟源 PCLK1 = 160MHz，带四舍五入
    uint32_t pclk = 160000000U;
    USART2->BRR = (pclk + (baud_rate / 2U)) / baud_rate;

    // 4. 配置控制寄存器并使能外设
    // CR1: 8位数据位，无校验位，使能发送器 (TE) 和 接收器 (RE)，最后使能串口 (UE)
    USART2->CR1 = USART_CR1_TE | USART_CR1_RE;
    USART2->CR1 |= USART_CR1_UE;
}

void bsp_usart2_send_char(char ch) {
    // 等待发送数据寄存器为空 (TXE)
    while (!(USART2->ISR & USART_ISR_TXE_TXFNF))
        ;
    USART2->TDR = (uint8_t)ch;
}

void bsp_usart2_send_str(const char *str) {
    while (str && *str) {
        bsp_usart2_send_char(*str++);
    }
}

/**
 * @brief 重定向 newlib-nano C 运行时标准库输出函数
 *        使得标准 printf / puts 自动输出到 USART2
 */
int _write(int file, char *ptr, int len) {
    (void)file;
    for (int i = 0; i < len; i++) {
        bsp_usart2_send_char(ptr[i]);
    }
    return len;
}
