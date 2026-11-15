#!/bin/bash
# ============================================================
# 在 Linux / 麒麟上构建 MaPdfR 并生成 .deb 安装包。
#
# 用法：
#   tools/make-deb-linux.sh [额外的 CMake 参数...]
#
# 可用环境变量覆盖：
#   BUILD_DIR   构建目录（默认 build/linux-deb）
#   QT_PREFIX   静态 Qt 安装前缀（默认 /data/Qt/6.11.1-aarch64-static）
#   MUPDF_DIR   MuPDF 源码树（默认 /usb1tb/202611OK/mupdf-1.27.2-source）
#   OPENSSL_ROOT_DIR  静态 OpenSSL（默认 /data/qt-static-deps/openssl，存在才用）
#
# 前置条件：
#   - 已用 tools/build-mupdf-linux.sh 生成 <MuPDF>/build/release/libmupdf.so
#   - 已安装 dpkg-dev / fakeroot（CMake 需要 dpkg-shlibdeps 计算依赖）
# ============================================================
set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
# 麒麟 V10 自带的 CMake 3.16 低于 Qt 6.11 要求的 3.22；
# 如果用户把新版 CMake 装在 ~/.local/bin，优先用它。
if [ -x "${HOME}/.local/bin/cmake" ]; then
    PATH="${HOME}/.local/bin:${PATH}"
    export PATH
fi
BUILD_DIR="${BUILD_DIR:-${PROJECT_DIR}/build/linux-deb}"
QT_PREFIX="${QT_PREFIX:-/data/Qt/6.11.1-aarch64-static}"
MUPDF_DIR="${MUPDF_DIR:-/usb1tb/202611OK/mupdf-1.27.2-source}"
OPENSSL_ROOT_DIR="${OPENSSL_ROOT_DIR:-/data/qt-static-deps/openssl}"

if [ ! -f "${MUPDF_DIR}/build/release/libmupdf.so" ]; then
    echo "缺少 ${MUPDF_DIR}/build/release/libmupdf.so" >&2
    echo "先执行: tools/build-mupdf-linux.sh ${MUPDF_DIR}" >&2
    exit 1
fi

OPENSSL_ARG=()
if [ -d "${OPENSSL_ROOT_DIR}" ]; then
    OPENSSL_ARG=(-DOPENSSL_ROOT_DIR="${OPENSSL_ROOT_DIR}")
fi

cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${QT_PREFIX}" \
    "${OPENSSL_ARG[@]}" \
    -DMAPDFR_WITH_MUPDF=ON \
    -DMUPDF_SOURCE_DIR="${MUPDF_DIR}" \
    "$@"

cmake --build "${BUILD_DIR}" --parallel "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

if [ "${SKIP_TESTS:-0}" != "1" ]; then
    # 在构建目录里跑测试：麒麟自带的 ctest 3.16 对 --test-dir 处理不一致。
    cd "${BUILD_DIR}"
    if [ -z "${DISPLAY}" ] && command -v xvfb-run >/dev/null 2>&1; then
        xvfb-run -a ctest --output-on-failure
    else
        ctest --output-on-failure
    fi
    cd "${PROJECT_DIR}"
fi

cd "${BUILD_DIR}"
cpack -G DEB
ls -l ./*.deb
