#!/bin/bash
set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CHASSIS_DIR="$PROJECT_ROOT/firmware_chassis"
BUILD_DIR="$CHASSIS_DIR/build"
BIN_FILE="$BUILD_DIR/firmware_chassis.bin"

echo "================================================="
echo "  🚗 STM32G473 底盘固件编译与 ST-Link 一键烧录"
echo "================================================="

# 1. 调用 Docker stm32-builder 进行极速编译
echo "⚡ [1/2] 正在调用 Docker stm32-builder 编译工程..."
docker run --rm -v "$CHASSIS_DIR:/workspace" -w /workspace stm32-builder:latest \
    bash -c "cmake -B build -G Ninja && cmake --build build"

if [ ! -f "$BIN_FILE" ]; then
    echo "❌ 错误: 未生成固件二进制文件 $BIN_FILE"
    exit 1
fi

echo "✅ 编译成功! 固件大小: $(ls -lh "$BIN_FILE" | awk '{print $5}')"

# 2. 调用 st-flash 自动烧录并复位芯片
echo "⚡ [2/2] 正在通过 ST-Link 烧录到 STM32G473 Flash (0x08000000)..."
st-flash --reset write "$BIN_FILE" 0x08000000

echo "================================================="
echo "🎉 烧录成功！STM32 已自动复位启动！"
echo "💡 提示: 电机将开始执行动作自检，可连接串口查看编码器脉冲！"
echo "================================================="
