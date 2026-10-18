#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$PROJECT_ROOT/firmware_head/build_host"
mkdir -p "$BUILD"
"${CXX:-clang++}" -std=c++17 -g -O1 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$PROJECT_ROOT/protocol" -I"$PROJECT_ROOT/shared" -I"$PROJECT_ROOT/firmware_head/Inc" \
    "$PROJECT_ROOT/firmware_head/tests/test_head.cpp" \
    "$PROJECT_ROOT/shared/ui/canvas.cpp" "$PROJECT_ROOT/shared/ui/face_engine.cpp" \
    "$PROJECT_ROOT/shared/ui/head_controller.cpp" "$PROJECT_ROOT/firmware_head/Src/head_display.cpp" \
    -o "$BUILD/test_head"
"$BUILD/test_head" "$BUILD/emotions.ppm"
python3 -m unittest discover -s "$PROJECT_ROOT/tools/luckfox" -p 'test_*.py' -v
