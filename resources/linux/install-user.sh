#!/bin/sh
# 把 MaPdfR 安装到当前用户目录（无需 sudo）。
#
# 用法：
#   ./install-user.sh              # 安装到 ~/.local
#   PREFIX=/opt/mapdfr ./install-user.sh
#
# 安装后可直接双击桌面项或在终端执行 mapdfr（需 ~/.local/bin 在 PATH 中）。
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
PREFIX="${PREFIX:-$HOME/.local}"

if [ ! -x "$HERE/bin/mapdfr" ]; then
    echo "找不到 $HERE/bin/mapdfr，请在完整解压的目录里运行本脚本。" >&2
    exit 1
fi

mkdir -p "$PREFIX/bin" "$PREFIX/lib/mapdfr" "$PREFIX/share/applications"
install -m 0755 "$HERE/bin/mapdfr" "$PREFIX/bin/mapdfr"

for lib in "$HERE"/lib/mapdfr/libmupdf.so.*; do
    [ -f "$lib" ] || continue
    install -m 0644 "$lib" "$PREFIX/lib/mapdfr/"
done

for size in 16 24 32 48 64 128 256; do
    icon="$HERE/share/icons/hicolor/${size}x${size}/apps/mapdfr.png"
    [ -f "$icon" ] || continue
    mkdir -p "$PREFIX/share/icons/hicolor/${size}x${size}/apps"
    install -m 0644 "$icon" "$PREFIX/share/icons/hicolor/${size}x${size}/apps/mapdfr.png"
done

# 桌面项的 Exec 指向安装后的绝对路径，避免依赖 PATH。
desktop="$PREFIX/share/applications/mapdfr.desktop"
sed -e "s|^Exec=mapdfr|Exec=\"$PREFIX/bin/mapdfr\"|" \
    -e "s|^TryExec=mapdfr|TryExec=$PREFIX/bin/mapdfr|" \
    "$HERE/share/applications/mapdfr.desktop" > "$desktop"
chmod 0644 "$desktop"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$PREFIX/share/applications" >/dev/null 2>&1 || true
fi

echo "MaPdfR 已安装到 $PREFIX"
echo "  程序：$PREFIX/bin/mapdfr"
case ":$PATH:" in
    *":$PREFIX/bin:"*) ;;
    *) echo "提示：$PREFIX/bin 不在 PATH 中，请加入 PATH 或使用完整路径启动。" ;;
esac
echo "可在程序的「帮助 → 注册 PDF 文件类型 / 设为默认 PDF 打开程序」里启用关联。"
