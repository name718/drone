#!/usr/bin/env bash
# 先增量编译，再通过本机串口写入 ESP32-S3。可用第一个参数指定串口。
set -euo pipefail
script_dir=$(cd "$(dirname "$0")" && pwd)
if (( $# > 1 )); then
    echo "用法：$0 [串口路径]" >&2
    exit 2
fi
port=${1:-${ESPPORT:-}}
if [[ -z "$port" ]]; then
    shopt -s nullglob
    ports=(/dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/ttyACM* /dev/ttyUSB*)
    if (( ${#ports[@]} != 1 )); then
        echo "自动选择需要恰好一个 USB 串口，当前找到 ${#ports[@]} 个。可用 $0 串口路径 指定。" >&2
        if (( ${#ports[@]} )); then printf '  %s\n' "${ports[@]}" >&2; fi
        exit 1
    fi
    port=${ports[0]}
fi
[[ -c "$port" ]] || { echo "串口设备不存在：$port" >&2; exit 1; }
# Mac USB 入口会传入从 Ubuntu 取回的固件目录。在 Ubuntu 直接运行时先编译。
build_dir=${ESP_FLASH_DIR:-"$script_dir/../build"}
if [[ -z "${ESP_FLASH_DIR:-}" ]]; then
    "$script_dir/build.sh"
fi
if ! command -v esptool >/dev/null && [[ $(uname -s) == Linux ]]; then
    # 加载 ESP-IDF 的 Python 环境，找到该版本自带的 esptool。
    set +u
    if [[ -n "${IDF_PATH:-}" ]]; then
        . "$IDF_PATH/export.sh"
    else
        . "$HOME/toolchains/drone/esp-env.sh"
    fi
    set -u
fi
command -v esptool >/dev/null || { echo "需要 esptool 5 才能烧录。" >&2; exit 1; }
cd "$build_dir"
# flash_args 由本次构建生成，包含引导程序、分区表和应用的地址及文件名。
exec esptool --chip esp32s3 --port "$port" --baud 115200 write-flash @flash_args
