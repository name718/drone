#!/usr/bin/env bash
# 先编译，再用 Ubuntu 的 stlink-tools 通过 SWD 写入片内 Flash，完成后复位运行。
set -euo pipefail
cd "$(dirname "$0")/.."
command -v st-flash >/dev/null || { echo "请先安装烧录工具：sudo apt install stlink-tools" >&2; exit 1; }
./scripts/build.sh
# STM32F103 从 0x08000000 开始存放固件，与本项目的链接地址一致。
exec st-flash --reset write .pio/build/stm32f103c8/firmware.bin 0x08000000
