#!/bin/bash
    set -e

    PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    CHASSIS_DIR="$PROJECT_ROOT/firmware_chassis"
    BUILD_DIR="$CHASSIS_DIR/build"
    BIN_FILE="$BUILD_DIR/firmware_chassis.bin"
    ELF_FILE="$BUILD_DIR/firmware_chassis.elf"

    # 支持 clean 参数：./scripts/build_chassis.sh clean
    if [ "$1" == "clean" ]; then
        echo "🧹 清理底盘构建缓存..."
        rm -rf "$BUILD_DIR"
        echo "✅ 清理完成！"
        exit 0
    fi

    echo "================================================="
    echo "  🔨 STM32G473 底盘固件 Docker 极速构建"
    echo "================================================="

    # 调用本地 Docker 交叉编译镜像
    docker run --rm -v "$CHASSIS_DIR:/workspace" -w /workspace stm32-builder:latest \
        bash -c "cmake -B build -G Ninja && cmake --build build"

    if [ -f "$BIN_FILE" ]; then
        echo "================================================="
        echo "✅ 固件构建成功!"
        echo "📍 ELF 路径 : $ELF_FILE"
        echo "📍 BIN 路径 : $BIN_FILE"
        echo "📦 固件大小 : $(ls -lh "$BIN_FILE" | awk '{print $5}')"
        echo "================================================="
    else
        echo "❌ 构建失败: 未生成目标文件 $BIN_FILE"
        exit 1
    fi
