/**
 * @file bsp_spi.c
 * @brief STM32G473 SPI1 底层硬件驱动实现 (用于 ICM-42605)
 */
#include "bsp_spi.h"

#include "stm32g4xx.h"

#define SPI_TIMEOUT_CYCLES 50000U  // 硬件超时阈值，防止总线异常死锁

void bsp_spi1_init() {
    // 1. 开启GPIOA与 Spi 的硬件时钟
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    // 2. 配置 PA4 为通用推挽输出01 (作为软件片选 CS 引脚)
    // pin4 是 8-9 位，每位占两个位，00000011 左移动 8 位 及 01
    GPIOA->MODER &= ~(3U << (4 * 2));
    GPIOA->MODER |= (1U << (4 * 2));    // 01
    GPIOA->OSPEEDR |= (3U << (4 * 2));  // PA4 输出速度
    bsp_spi1_cs_set(true);              // 默认初始拉高 CS，处于空闲状态

    // 3. 配置 PA5 (SCK), PA6 (MISO), PA7 (MOSI) 为复用功能 AF5
    // 清除原有模式位并设置为 10 (复用模式)
    GPIOA->MODER &= ~((3U << (5 * 2)) | (3U << (6 * 2)) | (3U << (7 * 2)));  // 0
    GPIOA->MODER |= ((2U << (5 * 2)) | (2U << (6 * 2)) | (2U << (7 * 2)));   // 1

    // 设置引脚 PA5 (SCK), PA6 (MISO), PA7 (MOSI) 速度为超高速 (Very High Speed)
    GPIOA->OSPEEDR |= ((3U << (5 * 2)) | (3U << (6 * 2)) | (3U << (7 * 2)));

    //  PA5 (SCK), PA6 (MISO), PA7 (MOSI) 配置复用映射寄存器 AFR (AFR[0] 对应 PA0~PA7，AF5 为 SPI1)
    GPIOA->AFR[0] &= ~((0xFU << (5 * 4)) | (0xFU << (6 * 4)) | (0xFU << (7 * 4)));
    GPIOA->AFR[0] |= ((5U << (5 * 4)) | (5U << (6 * 4)) | (5U << (7 * 4)));

    // 4. 复位并配置 SPI1 控制寄存器
    SPI1->CR1 = 0;  // 先清零并禁用 SPI
    SPI1->CR2 = 0;

    // CR1 寄存器配置:
    // - MSTR = 1: 主机模式
    // - BR[2:0] = 011b: 16分频 (160MHz / 16 = 10MHz，极稳的通信速率)
    // - CPOL = 0, CPHA = 0: SPI Mode 0 (ICM-42605 完美支持)
    // - SSM = 1, SSI = 1: 软件从机管理模式 (由我们手动控制 PA4 片选)
    // SPI1->CR1 = SPI_CR1_MSTR | (3U << SPI_CR1_BR_Pos) | SPI_CR1_SSM | SPI_CR1_SSI;
    // 配置为 SPI Mode 3 (CPOL=1, CPHA=1), 256分频 (625 kHz)
    SPI1->CR1 = SPI_CR1_MSTR | (7U << SPI_CR1_BR_Pos) | SPI_CR1_CPOL | SPI_CR1_CPHA | SPI_CR1_SSM |
                SPI_CR1_SSI;

    // CR2 寄存器配置:
    // - DS[3:0] = 0111b: 8位数据位长度
    // - FRXTH = 1: 接收 FIFO 阈值设为 8-bit (收到 1 个字节立即置位 RXNE)
    SPI1->CR2 = (7U << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH;

    // 5. 开启 SPI1 外设
    SPI1->CR1 |= SPI_CR1_SPE;
}

void bsp_spi1_cs_set(bool high) {
    if (high) {
        while (SPI1->SR & SPI_SR_BSY)
            ;                     // 确保最后一个时钟打完再拉高，避免截断尾包
        GPIOA->BSRR = (1U << 4);  // 拉高 PA4 (原子操作，置位)
    } else {
        GPIOA->BSRR = (1U << (4 + 16));  // 拉低 PA4 (原子操作，复位)
    }
}

uint8_t bsp_spi1_swap_byte(uint8_t tx_byte) {
    uint32_t timeout = SPI_TIMEOUT_CYCLES;

    // 等待发送缓冲区为空 (TXE)
    while (!(SPI1->SR & SPI_SR_TXE)) {
        if (--timeout == 0)
            return 0xFF;  // 防死锁保护
    }

    // 写入要发送的字节 (注意必须强制转为 8 位指针访问，防止硬件触发 16 位传输)
    *(__IO uint8_t *)&SPI1->DR = tx_byte;

    timeout = SPI_TIMEOUT_CYCLES;
    // 等待接收缓冲区非空 (RXNE)
    while (!(SPI1->SR & SPI_SR_RXNE)) {
        if (--timeout == 0)
            return 0xFF;  // 防死锁保护
    }

    // 读取并返回从机推回的数据
    return *(__IO uint8_t *)&SPI1->DR;
}

void bsp_spi1_transfer(const uint8_t *tx_buf, uint8_t *rx_buf, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        uint8_t send_val = tx_buf ? tx_buf[i] : 0xFF;
        uint8_t recv_val = bsp_spi1_swap_byte(send_val);
        if (rx_buf) {
            rx_buf[i] = recv_val;
        }
    }
}
