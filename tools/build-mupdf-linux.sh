#!/bin/bash
# ============================================================
# 在 Linux（含麒麟 aarch64）上构建 MaPdfR 需要的 MuPDF 共享库。
#
# 用法：
#   tools/build-mupdf-linux.sh /path/to/mupdf-1.27.2-source
#
# 产物：
#   <mupdf>/build/release/libmupdf.so
#
# 说明：
#   - 只保留 PDF 文档代理，关闭 JS / XPS / SVG / CBZ / 图片 / HTML /
#     EPUB / FB2 / MOBI / TXT / Office / 条码输出；
#   - TOFU + TOFU_CJK 只嵌入 Base14 字体；
#   - lcms2 符号加 mpdf_ 前缀，避免与 Qt6Pdf 静态链接的 PDFium 自带
#     lcms2 冲突；MuPDF 核心不能给 sscanf 加前缀，否则 GCC 的
#     <cstdio> 找不到 ::sscanf。
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

# lcms2 自身：全部符号加前缀（含 sscanf，避免覆盖 libc 的 sscanf）。
tr -d '\r' < "$SYMS" | awk 'BEGIN{print "#pragma once"} {print "#define " $0 " mpdf_" $0}' \
    > "$PREFIX_DIR/lcms_prefix.h"

# MuPDF 核心：保留 libc 的 sscanf。
tr -d '\r' < "$SYMS" | grep -vx sscanf \
    | awk 'BEGIN{print "#pragma once"} {print "#define " $0 " mpdf_" $0}' \
    > "$PREFIX_DIR/lcms_prefix_core.h"

cd "$MUPDF_SRC"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
# 共享库：避免 MuPDF 自带的 libjpeg/zlib/libpng 与 Qt6Pdf 内 PDFium 的副本
# 在静态链接时符号重复定义；-Bsymbolic 让 MuPDF 内部调用绑定到自己的副本。
make -j"$JOBS" shared=yes build=release OUT=build/release \
    HAVE_X11=no HAVE_GLUT=no HAVE_CURL=no USE_SYSTEM_CURL=no \
    mujs=no html=no xps=no svg=no extract=no archive=no barcode=no \
    tofu=yes tofu_cjk=yes \
    XLIB_LDFLAGS="-Wl,-Bsymbolic" \
    XCFLAGS="-include $PREFIX_DIR/lcms_prefix_core.h -DHAVE_LCMS2MT=1 -DFZ_ENABLE_CBZ=0 -DFZ_ENABLE_IMG=0 -DFZ_ENABLE_FB2=0 -DFZ_ENABLE_MOBI=0 -DFZ_ENABLE_TXT=0 -DFZ_ENABLE_OFFICE=0 -DFZ_ENABLE_OCR_OUTPUT=0" \
    LCMS2_BUILD_CFLAGS="-include $PREFIX_DIR/lcms_prefix.h" \
    libs

ls -l build/release/libmupdf.so
