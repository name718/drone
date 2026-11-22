#!/usr/bin/env bash
# 在电脑上构建纯 Python wheel，再按 ROS 2 的包索引和可执行文件布局打包。
set -euo pipefail
workspace=$(cd "$(dirname "$0")/.." && pwd)
cd "$workspace"
# 非交互 SSH 会话也能找到当前用户安装的 uv。
export PATH="$PATH:$HOME/.local/bin"
command -v uv >/dev/null || { echo "请先安装 Python 构建工具 uv。" >&2; exit 1; }
mkdir -p build
stage=$(mktemp -d "$workspace/build/package.XXXXXX")
trap 'rm -rf "$stage"' EXIT
cp -R src/luckfox_learning "$stage/source"
uv build --no-config --wheel --out-dir "$stage/wheels" "$stage/source"

# 使用标准 wheel 安装器生成 Linux 启动脚本。保留 share 下的 ament 索引和包清单。
uv run --isolated --no-project --with 'installer>=0.7,<1' python - "$stage" <<'PY'
from pathlib import Path
import sys

from installer import install
from installer.destinations import SchemeDictionaryDestination
from installer.sources import WheelFile

stage = Path(sys.argv[1])
wheel, = (stage / "wheels").glob("*.whl")
if not wheel.name.endswith("-py3-none-any.whl"):
    sys.exit("当前脚本只部署跨架构的纯 Python 包。")
paths = {
    "purelib": "/python", "platlib": "/python", "data": "/",
    "scripts": "/lib/luckfox_learning", "headers": "/include",
}
destination = SchemeDictionaryDestination(
    paths, "/usr/bin/python3", "posix", destdir=str(stage / "prefix")
)
with WheelFile.open(wheel) as source:
    install(source, destination, {})
PY

cat > "$stage/prefix/setup.sh" <<'ENV'
# 在板上用点命令加载本文件，使 ROS 2 能找到包，Python 能导入模块。
export AMENT_PREFIX_PATH="/userdata/luckfox_learning/current${AMENT_PREFIX_PATH:+:$AMENT_PREFIX_PATH}"
export PYTHONPATH="/userdata/luckfox_learning/current/python${PYTHONPATH:+:$PYTHONPATH}"
ENV
COPYFILE_DISABLE=1 tar -czf "$stage/luckfox_learning.tar.gz" -C "$stage/prefix" .
mv "$stage/luckfox_learning.tar.gz" build/luckfox_learning.tar.gz
echo "部署包已生成：$workspace/build/luckfox_learning.tar.gz"
