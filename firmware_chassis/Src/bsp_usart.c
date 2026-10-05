/**
 * @file bsp_usart.c
 * @brief STM32G473 底盘串口通信驱动实现 (USART2 PA2-TX / PA3-RX)
 *
 * 【核心技术实践】：
 *  1. 硬件中断驱动 (RXNE Interrupt)：避免高频轮询浪费 CPU，每当一个字节到达由硬件中断自动唤醒解析；
 *  2. 有限状态机 (FSM)：支持流式自愈解包，即使线上存在随机毛刺杂波，也能毫秒级定位 0xAA 帧头；
 *  3. 通信看门狗 (Safety Watchdog)：上位机异常断开时 500ms 内触发保护，防止小车飞车失控。
 */
#include "bsp_usart.h"

#include <stdio.h>
#include <string.h>

#include "bsp.h"
#include "stm32g4xx.h"

// 引入 FreeRTOS 调度器内核接口获取系统节拍数
#include "FreeRTOS.h"
#include "task.h"

// --- 全局最新指令与通信状态量 (原子/中断上下文共享) ---
static volatile int16_t  s_target_speed = 0;   // 目标线速度 (mm/s)
static volatile int16_t  s_target_yaw   = 0;   // 目标角速度 (mrad/s)
static volatile uint8_t  s_motion_mode  = 0;   // 运行模式 (0=待机, 1=使能运行, 2=急停)
static volatile uint32_t s_last_cmd_tick = 0;  // 上次收到有效指令帧的系统毫秒节拍
static volatile bool     s_has_valid_cmd = false; // 是否曾收到过有效指令

// 协议解析有限状态机 (FSM) 接收缓存
static uint8_t s_rx_buffer[sizeof(RobotCmdPacket_t)];
static uint8_t s_rx_index = 0;
static uint8_t s_rx_state = 0;  // 0: 等待 0xAA 帧头; 1: 持续收齐载荷

void bsp_usart2_init(uint32_t baud_rate) {
    // 1. 开启 GPIOA 与 USART2 硬件外设时钟
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;

    // 2. 配置 PA2 (TX) 和 PA3 (RX) 为复用模式 (MODER = 10b)
    GPIOA->MODER &= ~((3U << (2 * 2)) | (3U << (3 * 2)));
    GPIOA->MODER |= ((2U << (2 * 2)) | (2U << (3 * 2)));

    // 配置引脚速度为超高速 (Very High Speed)，保障 460.8k 高波特率边沿陡峭
    GPIOA->OSPEEDR |= ((3U << (2 * 2)) | (3U << (3 * 2)));

    // 配置引脚复用映射 AF7 (AFR[0] 对应 PA0~PA7，每引脚占 4 位)
    GPIOA->AFR[0] &= ~((0xFU << (2 * 4)) | (0xFU << (3 * 4)));
    GPIOA->AFR[0] |= ((7U << (2 * 4)) | (7U << (3 * 4)));

    // 3. 配置波特率发生器 BRR (160MHz 主频时钟源，带四舍五入)
    uint32_t pclk = 160000000U;
    USART2->BRR = (pclk + (baud_rate / 2U)) / baud_rate;

    // 4. 配置控制寄存器 CR1:
    // 开启发送使能 (TE)、接收使能 (RE) 以及接收中断使能 (RXNEIE)
    USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE;

    // 5. 配置 NVIC 中断优先级并使能中断通道
    // 注意：FreeRTOS 可管理的最大系统调用优先级为 5，我们设为 6，确保中断与内核安全共存
    NVIC_SetPriority(USART2_IRQn, 6);
    NVIC_EnableIRQ(USART2_IRQn);

    // 6. 最终使能串口外设
    USART2->CR1 |= USART_CR1_UE;

    s_rx_state = 0;
    s_rx_index = 0;
    s_has_valid_cmd = false;
}

/**
 * @brief USART2 硬件接收中断处理函数
 */
void USART2_IRQHandler(void) {
    uint32_t isr = USART2->ISR;

    // 1. 硬件错误标志自愈清除 (过载错误 ORE / 帧错误 FE / 噪声错误 NE)
    // 若不及时清除，硬件会锁死不再产生任何 RXNE 中断！
    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
        USART2->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
    }

    // 2. 检查是否有新字节到达 (RXNE)
    if (isr & USART_ISR_RXNE_RXFNE) {
        uint8_t byte = (uint8_t)(USART2->RDR);

        // --- 3. 逐字节流式有限状态机 (FSM) 解码 ---
        if (s_rx_state == 0) {
            // 状态 0：寻找协议魔数帧头 0xAA
            if (byte == PROTOCOL_FRAME_HEADER_CMD) {
                s_rx_buffer[0] = byte;
                s_rx_index = 1;
                s_rx_state = 1;  // 找到帧头，转移到接收载荷状态
            }
        } else if (s_rx_state == 1) {
            // 状态 1：收齐完整控制帧 (共 9 字节)
            s_rx_buffer[s_rx_index++] = byte;

            if (s_rx_index >= sizeof(RobotCmdPacket_t)) {
                // 计算 16 位累加和 (Checksum)
                uint16_t sum = 0;
                size_t payload_len = sizeof(RobotCmdPacket_t) - sizeof(uint16_t);
                for (size_t i = 0; i < payload_len; i++) {
                    sum += s_rx_buffer[i];
                }

                const RobotCmdPacket_t *pkt = (const RobotCmdPacket_t *)s_rx_buffer;
                if (sum == pkt->checksum) {
                    // 校验完全通过！立即更新系统目标指令
                    s_target_speed = pkt->target_speed;
                    s_target_yaw = pkt->target_yaw;
                    s_motion_mode = pkt->motion_mode;
                    s_last_cmd_tick = xTaskGetTickCountFromISR();
                    s_has_valid_cmd = true;
                }

                // 无论校验成功与否，复位状态机准备捕捉下一包
                s_rx_state = 0;
                s_rx_index = 0;
            }
        }
    }
}

bool bsp_usart2_is_connected(void) {
    if (!s_has_valid_cmd) {
        return false;
    }
    TickType_t now = xTaskGetTickCount();
    // 500ms 超时判定：超过 500ms 未收到任何有效帧则视为上位机掉线
    return (now - s_last_cmd_tick) <= pdMS_TO_TICKS(500);
}

bool bsp_usart2_get_cmd(int16_t *speed_mms, int16_t *yaw_mrads, uint8_t *motion_mode) {
    if (!bsp_usart2_is_connected()) {
        if (speed_mms) *speed_mms = 0;
        if (yaw_mrads) *yaw_mrads = 0;
        if (motion_mode) *motion_mode = 0;
        return false;
    }

    if (speed_mms) *speed_mms = s_target_speed;
    if (yaw_mrads) *yaw_mrads = s_target_yaw;
    if (motion_mode) *motion_mode = s_motion_mode;
    return true;
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

void bsp_usart2_send_bytes(const uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        bsp_usart2_send_char((char)data[i]);
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
