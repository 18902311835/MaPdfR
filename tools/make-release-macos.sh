#!/bin/bash
# ============================================================
# macOS 发行打包：MaPdfR.app + 文档 + 许可 + SHA256。
#
# 用法：
#   tools/build-macos.sh                 # 先构建并测试
#   tools/make-release-macos.sh          # 生成 dist/
#
# 产物：
#   dist/MaPdfR-<版本>-macos-arm64/      运行时目录
#   dist/MaPdfR-<版本>-macos-arm64.zip   压缩包
#   dist/MaPdfR-<版本>-macos-arm64.zip.sha256
#
# 与 Windows / Linux 运行时包一致：只放程序与文档，不含源码。
# ============================================================
set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_DIR/build/macos-arm64}"
CMAKE_BIN="${CMAKE_BIN:-/Volumes/usb1tb/Qt/Tools/CMake/CMake.app/Contents/bin/cmake}"
VERSION="$(sed -n 's/^project(MaPdfR VERSION \([0-9.]*\).*/\1/p' "$PROJECT_DIR/CMakeLists.txt")"

if [ -z "$VERSION" ]; then
    echo "无法从 CMakeLists.txt 读取版本号" >&2
    exit 1
fi

if [ ! -d "$BUILD_DIR/MaPdfR.app" ]; then
    echo "找不到 $BUILD_DIR/MaPdfR.app，请先运行 tools/build-macos.sh" >&2
    exit 1
fi

NAME="MaPdfR-$VERSION-macos-arm64"
DIST="$PROJECT_DIR/dist"
STAGE="$DIST/$NAME"

rm -rf "$STAGE" "$DIST/$NAME.zip" "$DIST/$NAME.zip.sha256"
mkdir -p "$DIST"

"$CMAKE_BIN" --install "$BUILD_DIR" --prefix "$STAGE"

if [ -f "$PROJECT_DIR/resources/help/user-guide.zh_CN.html" ]; then
    mkdir -p "$STAGE/docs"
    cp "$PROJECT_DIR/resources/help/user-guide.zh_CN.html" "$STAGE/docs/"
fi

{
    echo "MaPdfR $VERSION (macOS arm64)"
    echo "构建时间: $(date '+%Y-%m-%d %H:%M:%S %z')"
    echo "构建主机: $(sw_vers -productName) $(sw_vers -productVersion) ($(uname -m))"
    echo "Qt: 6.11.1 静态构建 (Clang, macOS)"
    echo "MuPDF: 1.27.2 (AGPL v3)"
    echo "许可证: GNU AGPL v3，完整文本见 LICENSE.txt 与 licenses/"
} > "$STAGE/SOURCE-INFO.txt"

# 复制/安装过程中 Mach-O 没有有效签名时无法执行，统一做 ad-hoc 签名。
codesign --force --deep --sign - "$STAGE/MaPdfR.app"

(
    cd "$STAGE"
    find MaPdfR.app -type f | sort | xargs shasum -a 256 > SHA256SUMS
)

(cd "$DIST" && zip -qry "$NAME.zip" "$NAME")
shasum -a 256 "$DIST/$NAME.zip" > "$DIST/$NAME.zip.sha256"

echo ">>> 发布目录: $STAGE"
echo ">>> 压缩包:   $DIST/$NAME.zip"
