#!/bin/bash
# ============================================================
# macOS（Apple Silicon）构建 + 测试。
#
# 前置：
#   tools/build-mupdf-macos.sh <mupdf-1.27.2-source>   # 生成 libmupdf.dylib
#
# 可用环境变量覆盖：
#   QT_PREFIX         静态 Qt 安装前缀
#   MUPDF_SOURCE_DIR  MuPDF 源码（含 build/shared-release/libmupdf.dylib）
#   BUILD_DIR         构建目录
#   MAPDFR_WITH_MUPDF ON/OFF
#
# 产物：build/macos-arm64/MaPdfR.app
# ============================================================
set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
QT_PREFIX="${QT_PREFIX:-/Volumes/usb1tb/Qt/6.11.1/macos-static}"
CMAKE_BIN="${CMAKE_BIN:-/Volumes/usb1tb/Qt/Tools/CMake/CMake.app/Contents/bin/cmake}"
NINJA="${NINJA:-/Volumes/usb1tb/Qt/Tools/Ninja/ninja}"
MUPDF_SOURCE_DIR="${MUPDF_SOURCE_DIR:-$HOME/third_party/mupdf-1.27.2}"
BUILD_DIR="${BUILD_DIR:-$PROJECT_DIR/build/macos-arm64}"

"$CMAKE_BIN" -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
    -DCMAKE_MAKE_PROGRAM="$NINJA" \
    -DMUPDF_SOURCE_DIR="$MUPDF_SOURCE_DIR" \
    -DMAPDFR_WITH_MUPDF="${MAPDFR_WITH_MUPDF:-ON}"

"$CMAKE_BIN" --build "$BUILD_DIR" --parallel "$(sysctl -n hw.ncpu)"

# ssh 会话没有窗口服务器，测试用 offscreen 平台插件（静态 Qt 已链入）。
cd "$BUILD_DIR"
QT_QPA_PLATFORM=offscreen "$CMAKE_BIN" --build "$BUILD_DIR" --target test

echo ">>> MaPdfR.app: $BUILD_DIR/MaPdfR.app"
