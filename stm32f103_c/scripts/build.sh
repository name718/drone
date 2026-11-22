#!/usr/bin/env bash
# 在 Ubuntu 上增量编译。PlatformIO 会按 platformio.ini 准备交叉编译工具链。
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$HOME/.local/bin:$PATH"
exec pio run
