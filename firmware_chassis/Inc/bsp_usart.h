/**
 * @file bsp_usart.h
 * @brief STM32G473 底盘串口通信驱动头文件 (USART2 PA2-TX / PA3-RX)
 *
 * 【系统架构职责】：
 *  1. 负责 STM32G473 与 ESP32-S3 大脑之间的高速双向串口通信 (460800 波特率)；
 *  2. 采用硬件中断接收 + 有限状态机 (FSM) 实时解包上位机控制帧 (RobotCmdPacket_t)；
 *  3. 提供通信超时看门狗 (Heartbeat Watchdog)，上位机异常掉线时自动紧急刹车停机；
 *  4. 承载 printf / puts 重定向输出，方便开发调试与 VOFA+ 波形监控。
 */
#ifndef BSP_USART_H
#define BSP_USART_H

#include <stdbool.h>
#include <stdint.h>
#include "robot_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 USART2 串口外设 (PA2 TX, PA3 RX) 与接收中断
 * @param baud_rate 通信波特率 (标准 460800)
 */
void bsp_usart2_init(uint32_t baud_rate);

/**
 * @brief 发送单个字符 (阻塞等待发送缓冲区就绪)
 * @param ch 目标字符
 */
void bsp_usart2_send_char(char ch);

/**
 * @brief 发送以 '\0' 结尾的字符串
 * @param str 字符串指针
 */
void bsp_usart2_send_str(const char *str);

/**
 * @brief 发送定长二进制字节流
 * @param data 待发送数据起始指针
 * @param len 字节长度
 */
void bsp_usart2_send_bytes(const uint8_t *data, uint16_t len);

/**
 * @brief 获取来自上位机的大脑最新控制指令
 * @param speed_mms 输出目标线速度 (单位: mm/s，前正后负)
 * @param yaw_mrads 输出目标角速度 (单位: mrad/s，左正右负)
 * @param motion_mode 输出运行模式 (0=待机/锁死, 1=使能运行, 2=紧急停机)
 * @return bool true: 成功获取且通信在线; false: 离线或无有效指令
 */
bool bsp_usart2_get_cmd(int16_t *speed_mms, int16_t *yaw_mrads, uint8_t *motion_mode);

/**
 * @brief 检查上位机通信心跳连接是否处于在线激活状态
 * @return bool true: 500ms 内收到有效控制帧; false: 掉线超时 (需立即停机保护)
 */
bool bsp_usart2_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif  // BSP_USART_H
