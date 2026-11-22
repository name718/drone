#!/usr/bin/env bash
# 在 Ubuntu 仓库中增量编译 ESP32-S3 固件，产物保存在项目的 build 目录。
set -eo pipefail
cd "$(dirname "$0")/.."
if ! command -v idf.py >/dev/null; then
    if [[ -n "${IDF_PATH:-}" ]]; then
        . "$IDF_PATH/export.sh"
    else
        . "$HOME/toolchains/drone/esp-env.sh"
    fi
fi
exec idf.py build
