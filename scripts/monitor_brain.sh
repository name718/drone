#!/bin/bash

# 自动检测串口
PORT="$1"
if [ -z "$PORT" ]; then
    PORTS=($(ls /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/cu.usbmodem* 2>/dev/null || true))
    if [ ${#PORTS[@]} -eq 0 ]; then
        echo "未检测到串口设备！"
        exit 1
    else
        PORT="${PORTS[0]}"
    fi
fi

BAUD="${2:-115200}"

echo "================================================="
echo "  连接 ESP32-S3 串口日志监控: $PORT ($BAUD)"
echo "  按 Ctrl+] 或 Ctrl+C 退出"
echo "================================================="

if [ -f /opt/homebrew/Cellar/esptool/5.3.1/libexec/bin/python ]; then
    /opt/homebrew/Cellar/esptool/5.3.1/libexec/bin/python -m serial.tools.miniterm "$PORT" "$BAUD"
elif python3 -m serial.tools.miniterm --help &> /dev/null; then
    python3 -m serial.tools.miniterm "$PORT" "$BAUD"
else
    screen "$PORT" "$BAUD"
fi
