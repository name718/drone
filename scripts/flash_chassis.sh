#!/bin/bash
set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_FILE="$PROJECT_ROOT/firmware_chassis/build/firmware_chassis.bin"

# 1. 触发编译构建
"$PROJECT_ROOT/scripts/build_chassis.sh"

# 2. 调用 st-flash 自动烧录并复位芯片
echo "⚡ 正在通过 ST-Link 烧录到 STM32G473 Flash (0x08000000)..."
st-flash --reset write "$BIN_FILE" 0x08000000

echo "================================================="
echo "🎉 烧录成功！STM32 已自动复位启动！"
echo "================================================="
