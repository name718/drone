#!/bin/bash
set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BRAIN_DIR="$PROJECT_ROOT/firmware_brain"
BUILD_DIR="$BRAIN_DIR/build"
BIN_FILE="$BUILD_DIR/firmware_brain.bin"

# 支持 clean 参数：./scripts/build_brain.sh clean
if [ "$1" == "clean" ]; then
    echo "🧹 清理 ESP32-S3 大脑固件构建缓存..."
    rm -rf "$BUILD_DIR"
    echo "✅ 清理完成！"
    exit 0
fi

echo "================================================="
echo "  🔨 ESP32-S3 大脑固件 Docker 极速构建"
echo "================================================="

docker run --rm -u $(id -u):$(id -g) -e HOME=/tmp -e IDF_TOOLS_PATH=/opt/esp-idf-tools \
    -v "$BRAIN_DIR:/workspace" -w /workspace esp32-builder:latest \
    bash -c "git config --global --add safe.directory '*' && . /opt/esp-idf/export.sh && idf.py build"

if [ -f "$BIN_FILE" ]; then
    echo "================================================="
    echo "✅ 固件构建成功!"
    echo "📍 BIN 路径 : $BIN_FILE"
    echo "📦 固件大小 : $(ls -lh "$BIN_FILE" | awk '{print $5}')"
    echo "================================================="
else
    echo "❌ 构建失败: 未生成目标文件 $BIN_FILE"
    exit 1
fi
