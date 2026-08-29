#!/bin/bash
set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BRAIN_DIR="$PROJECT_ROOT/firmware_brain"
BUILD_DIR="$BRAIN_DIR/build"

echo "================================================="
echo "  🚀 ESP32-S3 大脑固件一键烧录脚本"
echo "================================================="

# 1. 检查固件是否存在
BOOTLOADER="$BUILD_DIR/bootloader/bootloader.bin"
PARTITION="$BUILD_DIR/partition_table/partition-table.bin"
APP_BIN="$BUILD_DIR/firmware_brain.bin"

if [ ! -f "$APP_BIN" ] || [ ! -f "$BOOTLOADER" ] || [ ! -f "$PARTITION" ]; then
    echo "❌ 错误: 未找到固件二进制文件！"
    echo "💡 请先进入 firmware_brain 目录执行 esp-build 编译固件。"
    exit 1
fi

# 2. 自动检测串口
PORT="$1"
if [ -z "$PORT" ]; then
    # 自动搜索常见 USB 串口
    PORTS=($(ls /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/cu.usbmodem* 2>/dev/null || true))
    if [ ${#PORTS[@]} -eq 0 ]; then
        echo "❌ 未检测到 ESP32-S3 USB 串口设备！"
        echo "💡 请检查："
        echo "   1. Type-C 数据线是否连接稳固；"
        echo "   2. 数据线是否支持数据传输（非仅充电线）；"
        echo "   3. 是否插在标有 COM / UART 的 Type-C 口。"
        exit 1
    elif [ ${#PORTS[@]} -eq 1 ]; then
        PORT="${PORTS[0]}"
        echo "🔍 自动检测到串口: $PORT"
    else
        echo "🔍 检测到多个串口，默认选择第一个: ${PORTS[0]}"
        PORT="${PORTS[0]}"
    fi
else
    echo "📌 使用指定串口: $PORT"
fi

# 3. 执行高速烧录
echo "⚡ 开始烧录 (波特率: 460800)..."
esptool --chip esp32s3 -p "$PORT" -b 460800 \
    --before default_reset --after hard_reset \
    write_flash \
    --flash_mode dio --flash_size 16MB --flash_freq 80m \
    0x0 "$BOOTLOADER" \
    0x8000 "$PARTITION" \
    0x10000 "$APP_BIN"

echo "================================================="
echo "✅ 固件烧录成功！"
echo "💡 提示: 现在可以打开 VOFA+ 连接串口 [$PORT] (波特率 115200) 查看实时日志！"
echo "================================================="
