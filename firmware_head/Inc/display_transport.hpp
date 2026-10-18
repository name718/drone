// 仅声明屏幕需要的传输接口，绘图库不包含ARM寄存器头，便于在电脑上测试。
#pragma once
#include <cstddef>
#include <cstdint>
namespace Board {
bool spiWrite(const uint8_t *, size_t);
void lcdSelect(bool);
void lcdData(bool);
void lcdReset(bool);
void delay(uint32_t);
void service();
}  // namespace Board
