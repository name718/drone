#ifndef BSP_USART_H
#define BSP_USART_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 USART2 串口外设 (PA2 TX, PA3 RX)
 * @param baud_rate 波特率 (推荐 115200 或 921600)
 */
void bsp_usart2_init(uint32_t baud_rate);

/**
 * @brief 发送单个字符 (阻塞等待发送完成)
 */
void bsp_usart2_send_char(char ch);

/**
 * @brief 发送以 '\0' 结尾的字符串
 */
void bsp_usart2_send_str(const char *str);

#ifdef __cplusplus
}
#endif

#endif  // BSP_USART_H
