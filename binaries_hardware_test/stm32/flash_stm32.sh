#!/usr/bin/env bash
# 一键烧录 STM32 底盘硬件自检固件
set -e
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
echo "🚀 正在通过 ST-Link 烧录 STM32 硬件自检固件..."
st-flash --reset write "$DIR/firmware_chassis.bin" 0x08000000
echo "✅ STM32 烧录完成！"
