#!/bin/bash
# ============================================================
# 在 macOS（Apple Silicon）上构建 MaPdfR 需要的 MuPDF 动态库。
#
# 用法：
#   tools/build-mupdf-macos.sh /path/to/mupdf-1.27.2-source
#
# 产物：
#   <mupdf>/build/shared-release/libmupdf.dylib
#   （install_name 已改为 @rpath/libmupdf.dylib，随 .app 分发）
#
# 说明：
#   - 与 Linux 一致，只保留 PDF 文档代理，关闭 JS / XPS / SVG / CBZ /
#     图片 / HTML / EPUB / FB2 / MOBI / TXT / Office / 条码；
#   - TOFU + TOFU_CJK 只嵌入 Base14 字体；
#   - lcms2 符号加 mpdf_ 前缀。macOS 下 lcms2 也不能给 sscanf 加前缀，
#     否则链接时报 _mpdf_sscanf 未定义，因此核心和 lcms2 都用去掉
#     sscanf 的 lcms_prefix_core.h；
#   - MuPDF 的 Makefile 在 Darwin 上用 hexdump.sh 生成字体 C 源
#     （没有 objcopy）。
# ============================================================
set -e

MUPDF_SRC="${1:-${MUPDF_DIR:-}}"
if [ -z "$MUPDF_SRC" ] || [ ! -f "$MUPDF_SRC/Makefile" ]; then
    echo "用法: $0 <mupdf-1.27.2-source>" >&2
    exit 1
fi

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SYMS="$PROJECT_DIR/cmake/lcms2-symbols.txt"
PREFIX_DIR="${MUPDF_SRC}/build/lcms-prefix"
mkdir -p "$PREFIX_DIR"

tr -d '\r' < "$SYMS" | grep -vx sscanf \
    | awk 'BEGIN{print "#pragma once"} {print "#define " $0 " mpdf_" $0}' \
    > "$PREFIX_DIR/lcms_prefix_core.h"

cd "$MUPDF_SRC"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

make -j"$JOBS" shared=yes build=release OUT=build/shared-release \
    HAVE_X11=no HAVE_GLUT=no HAVE_CURL=no USE_SYSTEM_CURL=no \
    mujs=no html=no xps=no svg=no extract=no archive=no barcode=no \
    tofu=yes tofu_cjk=yes \
    XCFLAGS="-include $PREFIX_DIR/lcms_prefix_core.h -DHAVE_LCMS2MT=1 -DFZ_ENABLE_CBZ=0 -DFZ_ENABLE_IMG=0 -DFZ_ENABLE_FB2=0 -DFZ_ENABLE_MOBI=0 -DFZ_ENABLE_TXT=0 -DFZ_ENABLE_OFFICE=0 -DFZ_ENABLE_OCR_OUTPUT=0" \
    LCMS2_BUILD_CFLAGS="-include $PREFIX_DIR/lcms_prefix_core.h" \
    libs

DYLIB="build/shared-release/libmupdf.dylib"
# 应用包通过 @rpath 找到 Contents/Frameworks 里的副本。
install_name_tool -id @rpath/libmupdf.dylib "$DYLIB"
# install_name_tool 改写了 Mach-O，链接器的 ad-hoc 签名随之失效，
# arm64 上未签名的库无法加载，必须重新签名。
codesign --force --sign - "$DYLIB"
ls -l "$DYLIB"
