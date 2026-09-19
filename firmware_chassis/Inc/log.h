#ifndef LOG_H
#define LOG_H

#include <stdio.h>

#include "bsp.h"

// ANSI 终端彩色高亮控制字符
#define LOG_CLR_RESET "\033[0m"
#define LOG_CLR_RED "\033[31m"
#define LOG_CLR_GREEN "\033[32m"
#define LOG_CLR_YELLOW "\033[33m"
#define LOG_CLR_BLUE "\033[34m"

// 企业级格式化日志输出: [时间戳ms][级别][模块标签] 消息
#define LOG_I(tag, fmt, ...)                                                                                 \
    printf(LOG_CLR_GREEN "[%8lu][I][%s] " fmt LOG_CLR_RESET "\r\n", (unsigned long)bsp_get_ticks_ms(), tag, \
           ##__VA_ARGS__)

#define LOG_W(tag, fmt, ...)                                                                                  \
    printf(LOG_CLR_YELLOW "[%8lu][W][%s] " fmt LOG_CLR_RESET "\r\n", (unsigned long)bsp_get_ticks_ms(), tag, \
           ##__VA_ARGS__)

#define LOG_E(tag, fmt, ...)                                                                               \
    printf(LOG_CLR_RED "[%8lu][E][%s] " fmt LOG_CLR_RESET "\r\n", (unsigned long)bsp_get_ticks_ms(), tag, \
           ##__VA_ARGS__)

// 专用于上位机 (如 VOFA+ 的 FireWater 引擎) 绘图的纯净波形宏 (CSV 格式)
#define LOG_PLOT(fmt, ...) printf(fmt "\r\n", ##__VA_ARGS__)

#endif  // LOG_H
