#!/usr/bin/env bash
# 只烧录经确认的64KB STM32F1，不调用原来的G473底盘烧录脚本。
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$PROJECT_ROOT/scripts/build_head.sh"
probe="$(st-info --probe)"
if [[ "$probe" != *"Found 1 stlink programmers"* || "$probe" != *"chipid:     0x410"* || "$probe" != *"flash:      65536"* ]]; then
    echo '目标不明确：要求恰好一只ST-Link，连接chipid=0x410、64KB Flash的C8。' >&2
    exit 1
fi
mkdir -p "$PROJECT_ROOT/firmware_head/artifacts"
backup="$PROJECT_ROOT/firmware_head/artifacts/backup_$(date +%Y%m%d_%H%M%S).bin"
st-flash read "$backup" 0x08000000 0x10000
st-flash --reset write "$PROJECT_ROOT/firmware_head/build/firmware_head.bin" 0x08000000
echo "原固件已备份：$backup"
