#!/bin/bash
# ============================================================
# 生成 Linux / 麒麟免安装（免 sudo）的 tar.gz 便携包。
#
# 用法：
#   tools/make-tar-linux.sh
#
# 可用环境变量覆盖：
#   BUILD_DIR   已完成构建的目录（默认 build/linux-deb）
#   OUT_DIR     输出目录（默认与 BUILD_DIR 相同）
#   STAGE_DIR   中间暂存目录（默认 <BUILD_DIR>/portable）
#
# 产物：
#   <OUT_DIR>/MaPdfR-<版本>-linux-<arch>.tar.gz
#   <OUT_DIR>/MaPdfR-<版本>-linux-<arch>.tar.gz.sha256
#
# 解压后直接运行 ./mapdfr；可选 ./install-user.sh 安装到 ~/.local。
# ============================================================
set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
if [ -x "${HOME}/.local/bin/cmake" ]; then
    PATH="${HOME}/.local/bin:${PATH}"
    export PATH
fi

BUILD_DIR="${BUILD_DIR:-${PROJECT_DIR}/build/linux-deb}"
OUT_DIR="${OUT_DIR:-${BUILD_DIR}}"
STAGE_ROOT="${STAGE_DIR:-${BUILD_DIR}/portable}"
ARCH="${ARCH:-$(uname -m)}"
VERSION="$(sed -n 's/^project(MaPdfR VERSION \([0-9][0-9.]*\).*/\1/p' "${PROJECT_DIR}/CMakeLists.txt")"

if [ -z "${VERSION}" ]; then
    echo "无法从 CMakeLists.txt 读取版本号" >&2
    exit 1
fi

NAME="MaPdfR-${VERSION}-linux-${ARCH}"
STAGE="${STAGE_ROOT}/${NAME}"

if [ ! -x "${BUILD_DIR}/mapdfr" ]; then
    echo "找不到 ${BUILD_DIR}/mapdfr，请先完成构建：" >&2
    echo "  tools/make-deb-linux.sh" >&2
    exit 1
fi

rm -rf "${STAGE}"
mkdir -p "${STAGE}"

# FHS 布局安装到暂存目录：bin/mapdfr、lib/mapdfr、share/...
# 相对 RPATH $ORIGIN/../lib/mapdfr 在这个布局下同样有效。
cmake --install "${BUILD_DIR}" --prefix "${STAGE}"

install -m 0755 "${PROJECT_DIR}/resources/linux/mapdfr-launcher.sh" "${STAGE}/mapdfr"
install -m 0755 "${PROJECT_DIR}/resources/linux/install-user.sh" "${STAGE}/install-user.sh"
install -m 0644 "${PROJECT_DIR}/resources/linux/README-portable.txt" "${STAGE}/README-portable.txt"

tar czf "${OUT_DIR}/${NAME}.tar.gz" -C "${STAGE_ROOT}" "${NAME}"
(cd "${OUT_DIR}" && sha256sum "${NAME}.tar.gz" > "${NAME}.tar.gz.sha256")

ls -l "${OUT_DIR}/${NAME}.tar.gz" "${OUT_DIR}/${NAME}.tar.gz.sha256"
