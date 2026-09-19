
/**
 * @file bsp.h
 * @brief 板级支持包总入口 (时钟、滴答定时器延时、心跳 LED)
 */

#ifndef BSP_H
#define BSP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化系统总线时钟 (160MHz) 与 SysTick 1ms 硬件时基
 */
void bsp_init(void);

/**
 * @brief 基于硬件 SysTick 的精准毫秒阻塞延时
 * @param ms 延时毫秒数
 */
void delay_ms(uint32_t ms);

/**
 * @brief 获取开机以来的系统运行总毫秒数 (时钟节拍)
 */
uint32_t bsp_get_ticks_ms(void);

/**
 * @brief 翻转主板蓝色运行状态指示灯 (PC13)
 */
void led_toggle(void);
void led_set(bool on);

#ifdef __cplusplus
}
#endif

#endif  // BSP_H
