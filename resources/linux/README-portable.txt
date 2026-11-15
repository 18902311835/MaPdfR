MaPdfR 便携版（Linux / 麒麟 aarch64）
=====================================

解压后即可使用，不需要 sudo：

    tar xzf MaPdfR-<版本>-linux-aarch64.tar.gz
    cd MaPdfR-<版本>-linux-aarch64
    ./mapdfr 示例.pdf

也可以直接运行 bin/mapdfr；lib/ 里是随包的 MuPDF 共享库，程序通过相对
运行时路径自动找到它，不依赖系统安装的 MuPDF。

可选：安装到当前用户目录（默认 ~/.local），获得桌面启动项和图标：

    ./install-user.sh

安装后可双击桌面项启动，或在终端执行 mapdfr（~/.local/bin 需在 PATH 中）。
随后可在程序的「帮助 → 设为默认 PDF 打开程序」里把 MaPdfR 设为 PDF 默认应用。

目录内容：
  mapdfr                启动器（调用 bin/mapdfr）
  install-user.sh       安装到当前用户目录（可选）
  bin/mapdfr            主程序（静态 Qt）
  lib/mapdfr/           MuPDF 运行时库
  share/applications/   桌面启动项
  share/icons/          启动器图标
  share/doc/mapdfr/     说明书、README、许可证与第三方组件说明

系统要求：银河麒麟 V10 或兼容的 glibc 2.29+ 发行版，X11 或 Wayland 桌面。
许可证：GNU AGPL v3，源码见 https://github.com/18902311835/MaPdfR
