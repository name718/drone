#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
docker run --rm -v "$PROJECT_ROOT:/workspace" -w /workspace stm32-builder:latest \
    bash -lc 'cmake -S firmware_head -B firmware_head/build -G Ninja && cmake --build firmware_head/build'
