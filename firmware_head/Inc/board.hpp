/** @file board.hpp @brief C8T6 引脚及硬件边界。A5 即 PA5，不是排针的第5脚。 */
#pragma once
#include <cstddef>
#include <cstdint>

#include "stm32f103xb.h"
namespace Board {
// SPI1: PA5=SCLK、PA7=MOSI、PA4=CS；PB0=DC、PB1=RST；BLK 固定接3.3V。
// TIM2: PA0=Pan、PA1=Tilt；USART1: PA9=TX、PA10=RX，115200 8N1。
// 摄像头：PB11=RESET(低有效)，PB12=PWDN(高有效)。
constexpr uint32_t UART_BAUD = 115200;
void init();
uint32_t millis();
void delay(uint32_t ms);
void service();  // 主循环和逐行刷屏时调用，保证通信、舵机状态机不被整屏刷新饿死。
void setServiceHook(void (*hook)());
bool hseClock();
bool spiWrite(const uint8_t *data, size_t size);
void lcdSelect(bool selected);
void lcdData(bool data);
void lcdReset(bool high);
void lcdDiagnosticMode(bool software_spi);  // 仅供SWD排查：同一组引脚的低速软件SPI。
bool uartRead(uint8_t &byte);
bool uartWrite(const uint8_t *data, size_t size);
uint32_t rxBytes();
uint32_t txBytes();
uint32_t uartErrors();
uint32_t txDrops();
void servo(bool enabled, uint16_t pan_us, uint16_t tilt_us);
void camera(bool reset_high, bool power_down);
void led(bool on);
void watchdogKick();
}  // namespace Board
