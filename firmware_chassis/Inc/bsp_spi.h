#ifndef BSP_SPI_H
#define BSP_SPI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief 初始化 SPI1 控制器及其硬件引脚 (PA4/PA5/PA6/PA7)
 *        - PA4: CS (软件控制 GPIO 输出)
 *        - PA5: SCK (时钟线，复用 AF5)
 *        - PA6: MISO (数据输入，复用 AF5)
 *        - PA7: MOSI (数据输出，复用 AF5)
 */
void bsp_spi1_init(void);
/**
 * @brief 控制 IMU 的片选引脚 CS (PA4)
 * @param high true: 拉高（取消选中，空闲状态）；false: 拉低（选中芯片，开始通信）
 */
void bsp_spi1_cs_set(bool high);

/**
 * @brief SPI 通信基础原语：全双工同时收发一个字节
 *        (SPI 协议特性：主机在时钟线上推移出 1 字节的同时，从机必定推回 1 字节)
 * @param tx_byte 主机发给从机的字节
 * @return uint8_t 从机实时返回给主机的字节
 */
uint8_t bsp_spi1_swap_byte(uint8_t tx_byte);

/**
 * @brief SPI 批量连续读写传输 (用于高效读取 6 轴加速度和角速度连续数据)
 * @param tx_buf 发送缓冲区指针（若为 NULL 则发送默认填充字节 0xFF）
 * @param rx_buf 接收缓冲区指针（若为 NULL 则只发送不保存接收值）
 * @param len    需要传输的字节长度
 */
void bsp_spi1_transfer(const uint8_t *tx_buf, uint8_t *rx_buf, uint16_t len);
#ifdef __cplusplus
}
#endif
#endif  // BSP_SPI_H
