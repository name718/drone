#!/usr/bin/env bash
# 用 USB ADB 更新 /userdata 下的学习包。可用第一个参数指定 ADB 序列号。
set -euo pipefail
script_dir=$(cd "$(dirname "$0")" && pwd)
if (( $# > 1 )); then echo "用法：$0 [ADB序列号]" >&2; exit 2; fi
adb_bin=${ADB:-adb}
# 可以把本机已有的 adb 放在工作区 build/tools，工具本身不纳入版本控制。
if [[ -z "${ADB:-}" ]] && ! command -v adb >/dev/null && [[ -x "$script_dir/../build/tools/adb" ]]; then
    adb_bin="$script_dir/../build/tools/adb"
fi
command -v "$adb_bin" >/dev/null || { echo "需要 Android platform-tools。也可用 ADB=/完整路径/adb 指定。" >&2; exit 1; }
# -d 只选择唯一 USB 设备。有多块板时用 -s 明确指定。
device=("$adb_bin" -d)
if [[ -n "${1:-}" ]]; then device=("$adb_bin" -s "$1"); fi
model=$("${device[@]}" shell 'cat /proc/device-tree/model' | tr -d '\000\r\n')
case "$model" in
    *Luckfox*Mini*) echo "目标板：$model" ;;
    *) echo "目标型号为 '$model'，请连接项目使用的 Luckfox Pico Mini。" >&2; exit 1 ;;
esac
"${device[@]}" shell '/usr/bin/python3 --version'
# USB 接到 Mac 时，直接部署虚拟机打好的归档。
archive=${LUCKFOX_ARCHIVE:-"$script_dir/../build/luckfox_learning.tar.gz"}
if [[ -z "${LUCKFOX_ARCHIVE:-}" ]]; then
    "$script_dir/build.sh"
fi
[[ -s "$archive" ]] || { echo "部署包不存在：$archive" >&2; exit 1; }

# 每次在独立目录解包。只有传输完成、包可导入时，才切换 current。
root=/userdata/luckfox_learning
stage=$("${device[@]}" shell "mkdir -p '$root' && mktemp -d '$root/release.XXXXXX'" | tr -d '\r')
[[ "$stage" =~ ^/userdata/luckfox_learning/release\.[a-zA-Z0-9]+$ ]] || { echo "板端临时目录无效" >&2; exit 1; }
"${device[@]}" push "$archive" "$stage/package.tar.gz"
"${device[@]}" shell -T sh -s -- "$stage" <<'REMOTE'
set -eu
stage=$1
cd "$stage"
tar -xzf package.tar.gz
rm package.tar.gz
test -f share/ament_index/resource_index/packages/luckfox_learning
test -x lib/luckfox_learning/learning
PYTHONPATH="$stage/python" python3 -c 'import luckfox_learning'
# 原子替换软链接。传输中断时，之前启用的版本仍可运行。
python3 - "$stage" <<'PY'
from pathlib import Path
import os
import sys

release = Path(sys.argv[1])
link = release / ".activate"
link.symlink_to(release)
os.replace(link, release.parent / "current")
PY
REMOTE
echo "部署成功。板上加载 ROS 2 环境后执行："
echo '. /userdata/luckfox_learning/current/setup.sh && ros2 run luckfox_learning learning'
