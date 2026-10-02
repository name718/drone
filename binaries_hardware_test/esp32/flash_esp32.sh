#!/usr/bin/env bash
# 一键烧录 ESP32-S3 大脑硬件全项自检固件 (屏幕+云台+喇叭+麦克风)
set -e
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT="${1:-/dev/cu.usbserial-2130}"

echo "🚀 正在通过 $PORT 烧录 ESP32-S3 全项硬件自检固件..."
esptool --chip esp32s3 -p "$PORT" -b 230400 \
    --before default-reset --after hard-reset write-flash \
    --flash-mode dio --flash-size 16MB --flash-freq 80m \
    0x0 "$DIR/bootloader.bin" \
    0x8000 "$DIR/partition-table.bin" \
    0x10000 "$DIR/firmware_brain.bin"
echo "✅ ESP32-S3 烧录完成！"
