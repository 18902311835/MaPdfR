# CLAUDE.md

MaPdfR：Windows / macOS（Apple Silicon）/ Linux（麒麟 aarch64）独立 PDF 查看器
（Qt 6 静态构建），阅读部分提取自作者的 QtNotePad 2.36.2；MuPDF 1.27.2 支撑
复制权限、打印、便笺和 Markdown 导出。阅读器另有查找（`QPdfSearchModel`）、
工具栏缩放比例、PDF 文件类型注册（Windows HKCU / Linux .desktop / macOS
LaunchServices）与「关于 MaPdfR」窗口。

## 构建与测试

```bat
bld.bat            :: 配置 + 编译 + ctest（发布前用这个验证）
bld.bat clean      :: 删除 build\ 后全量重建（会重新编译 MuPDF，几分钟）
bld.bat run        :: 构建后启动 MaPdfR.exe
tools\make-release.bat :: 生成 dist\MaPdfR-<版本>\
```

默认路径（均可用环境变量覆盖）：Qt 静态 kit `E:\QT\6.11.1\msvc2022_64-static`
（`QT_PREFIX`），MuPDF 源码 `E:\third_party\mupdf-1.27.2-source`（`MUPDF_DIR`），
VS2022 `vcvars64.bat`（`VCVARS`）。

手动构建时关键参数：

```bat
cmake -S . -B build\msvc2022_64-static -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=E:/QT/6.11.1/msvc2022_64-static ^
      -DMUPDF_SOURCE_DIR=E:/third_party/mupdf-1.27.2-source ^
      -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
```

Linux / 麒麟（aarch64）构建：

```sh
tools/build-mupdf-linux.sh /path/to/mupdf-1.27.2-source   # 生成 libmupdf.so
tools/make-deb-linux.sh                                   # 构建 + 测试 + cpack
# 产物：build/linux-deb/mapdfr_<版本>_arm64.deb
```

- Linux 用 **共享库** `libmupdf.so`（`-Bsymbolic`）：静态链接时 MuPDF 自带的
  libjpeg/zlib/libpng 与 Qt6Pdf 内 PDFium 的副本重复定义。
- MuPDF 源码树在 `MAPDFR_MUPDF_BUILD_DIR`（默认 `<MuPDF>/build/release`），
  字体由 MuPDF Makefile 的 `HAVE_OBJCOPY` 嵌入，不走 `bin2coff`。
- lcms2 前缀头分两份：MuPDF 核心不能前缀 `sscanf`（GCC `<cstdio>` 找不到
  `::sscanf`），见 `tools/build-mupdf-linux.sh`。
- 麒麟 V10 的 CMake 3.16 不够用（Qt 6.11 要 3.22+），脚本优先用
  `~/.local/bin/cmake`；无显示环境测试用 `xvfb-run -a ctest`。
- 静态 Qt 需要静态 OpenSSL：配置时传 `-DOPENSSL_ROOT_DIR=/data/qt-static-deps/openssl`。

macOS（Apple Silicon）构建：

```sh
tools/build-mupdf-macos.sh /path/to/mupdf-1.27.2-source  # build/shared-release/libmupdf.dylib
tools/build-macos.sh                                      # 配置 + 编译 + offscreen ctest
tools/make-release-macos.sh                               # dist/MaPdfR-<版本>-macos-arm64.zip
```

- 默认用静态 Qt `/Volumes/usb1tb/Qt/6.11.1/macos-static`，最低 macOS 13。
- 与 Linux 一样用 MuPDF 动态库隔离 PDFium 的 libjpeg/zlib/lcms2 副本；
  dylib 的 install_name 是 `@rpath/libmupdf.dylib`，随 `.app` 放
  `Contents/Frameworks`。**任何 install_name_tool 改动后必须重新
  `codesign --force --sign -`**，否则 arm64 上无法加载。
- 静态 Qt 没有自动链接控件，`qcocoa`（窗口）与 `qoffscreen`（无窗口测试）
  由 `qt_import_plugins` 链入；ssh 会话跑 ctest 要 `QT_QPA_PLATFORM=offscreen`。
- macOS 的文件关联走 LaunchServices（bundle id `cn.madeyou.mapdfr`），
  没有可隔离的测试子树，冒烟测试里该项 `QSKIP`。
- 应用包由 `cmake/mapdfr-info.plist.in` 生成 Info.plist，
  图标是 `resources/macos/mapdfr.icns`（由 `resources/linux/icons` 生成）。

- 全部代码用 **MSVC + `/MT`**（静态 Qt 的要求，也是 MuPDF 静态库的要求）。
- 版本号唯一来源是 `CMakeLists.txt` 的 `project(MaPdfR VERSION …)`；通过
  `MAPDFR_APP_VERSION` 编译定义传给 `main.cpp`、`AboutDialog`，同时用于
  Windows 版本资源（`cmake/mapdfr-app.rc.in`）。改版本只需改这一处。
- 测试（`ctest --test-dir …`）：
  - `MaPdfR.ReaderSmoke`：打不开的文件返回 `isLoaded()==false` + 错误文本、
    正常 PDF 可加载、缩放标签显示百分比、查找能命中 fixture 文本、
    `MaPdfRFileType::selfTest()` 的隔离注册表往返；Linux 上改为在临时
    `XDG_DATA_HOME` / `XDG_CONFIG_HOME` 里验证 .desktop 写入、`xdg-mime`
    默认程序写入与注销清理；macOS 该项 `QSKIP`（LaunchServices 无法隔离，
    测试不得改用户真实的默认程序）。
  - `MaPdfR.MarkdownExport`：PDF → Markdown 的内容断言与 MuPDF/PDFium 共存。
- `-DMAPDFR_WITH_MUPDF=OFF` 可跳过 MuPDF，保留阅读、导航、搜索、选区高亮和帮助。
- 正式发行前需把真实链接参数和目标文件接入发行打包，并执行重链接测试。
- **`tools\*.ps1` 必须保存为 UTF-8 with BOM**：Windows PowerShell 5.1 会把无 BOM
  的 UTF-8 当 ANSI，中文注释/字符串会破坏语法。

## 代码结构

| 位置 | 作用 |
|---|---|
| `src/app/main.cpp` | 程序入口、打开文件、错误提示、Qt 中文翻译 |
| `src/app/LocalOpenRelay.{h,cpp}` | 同一登录会话和可执行文件下的本机打开请求转发 |
| `src/ui/PdfWorkspace.{h,cpp}` | 单窗口多标签和打开队列 |
| `src/ui/PdfMainWindow.{h,cpp}` | 阅读器主体（源自 QtNotePad），尽量只增量修改 |
| `src/io/SafeFileCopy.{h,cpp}` | `QSaveFile` 原子复制 |
| `src/convert/PdfToMarkdown.{h,cpp}` | PDF → Markdown 转换器（MuPDF C API） |
| `src/app/FileAssociation.{h,cpp}` | HKCU PDF 文件类型注册 / 系统「打开方式」对话框 |
| `src/ui/AboutDialog.{h,cpp}` | 关于窗口：关于 / 第三方组件 / 版权与许可 |
| `resources/pdf.ico` | `.pdf` 文件类型图标（16–256），注册表 DefaultIcon 引用 |
| `tools/make-release.{bat,ps1}` | 发行打包：运行时 EXE + 文档 + SHA256；`-WithSource` 另出独立源码包 |
| `tools/build-mupdf-linux.sh` | Linux 用 MuPDF Makefile 构建 `libmupdf.so`（lcms2 前缀、`-Bsymbolic`） |
| `tools/build-mupdf-macos.sh` | macOS 构建 `libmupdf.dylib`（install_name `@rpath/`、ad-hoc 签名） |
| `tools/build-macos.sh`、`tools/make-release-macos.sh` | macOS 构建 + 离屏测试 + `.app` 发行包 |
| `cmake/mapdfr-info.plist.in`、`resources/macos/mapdfr.icns` | macOS 应用包 Info.plist 与图标 |
| `tools/make-deb-linux.sh`、`tools/deb/` | 麒麟/Linux 构建 + cpack DEB 打包与 postinst/postrm |
| `tools/make-tar-linux.sh` | 免安装 tar.gz 便携包（`./mapdfr` 直接运行，可选 `install-user.sh`） |
| `resources/linux/mapdfr.desktop`、`resources/linux/icons/` | Linux 桌面启动项与 16–256 像素启动器图标 |
| `tools/relink.{bat,ps1}` | 用修改后的静态 Qt 重新链接应用目标文件（LGPL §4(d)(0)） |
| `licenses/` | LGPL v3 / GPL v3 全文（静态 Qt 随附文本） |
| `cmake/mupdf.cmake` | MuPDF 静态库集成 |
| `tests/data/generate_fixtures.py` | 重新生成 `sample-rich.pdf` / `sample-encrypted.pdf`（需要 PyMuPDF） |

## 界面与功能约定

- 工作区持有每个打开的阅读器。只在 PDF 加载成功后加入标签。密码提示和其他模态对话框期间排队处理打开请求。
- 本机打开请求通过用户范围的本地通道转发。实例锁按用户、桌面会话和可执行文件定位。Qt-only 构建支持同样的文档标签。

- 查找栏（`ReaderFindToolBar`）默认隐藏；`Ctrl+F`（`QKeySequence::Find`）显示并
  聚焦搜索框，`F3` / `Shift+F3` 上下切换，`Esc` 关闭并清空高亮。搜索走
  `QPdfSearchModel` 逐页异步扫描，状态标签显示「当前 / 总数」或「无结果」；
  命中由 `QPdfView::setSearchModel` + `currentSearchResultIndex` 高亮并滚动。
- 缩放比例标签 `ReaderZoomLabel` 由 `QPdfView::zoomFactorChanged` 驱动，
  适合宽度 / 整页时也实时更新；「实际大小」动作不要重新命名为「100%」，
  避免与比例显示重复。
- 菜单「帮助」：用户说明书（`F1`）、注册 / 取消注册 PDF 文件类型、设为默认打开程序、关于。
  文件类型注册**只在用户点菜单时执行**，不在启动时写注册表：
  - 只写 `HKCU\Software\Classes`（ProgID `MaPdfR.PDFDocument`、`DefaultIcon`、
    `shell\open\command`、`Applications\MaPdfR.exe`、`.pdf\OpenWithProgids`）；
  - Windows 8+ 不允许静默改默认程序，统一用 `SHOpenWithDialog` 让用户确认；
  - 取消注册只删本程序写入的值，不动 `.pdf` 原有处理程序；
  - `MaPdfRFileType::selfTest()` 在 `HKCU\Software\MaPdfR\SelfTest\<uuid>` 做
    注册→查询→注销往返，测试不得触碰真实关联。
- Linux 的关联实现：注册 = 写 `~/.local/share/applications/mapdfr.desktop`
  （`MimeType=application/pdf;` 与 `Exec="<绝对路径>" %F`）；设为默认 =
  `xdg-mime default mapdfr.desktop application/pdf`；取消注册 = 删除该文件并
  从 `~/.config/mimeapps.list` 摘掉条目。deb 安装的桌面项在
  `/usr/share/applications`，两者都参与系统「打开方式」。
- 二进制在 Linux 上叫 `mapdfr`（`OUTPUT_NAME`），安装 RPATH 为
  `$ORIGIN/../lib/mapdfr`；`QGuiApplication::setDesktopFileName("mapdfr")`
  让窗口归到启动器图标下。
- macOS 的关联实现：Info.plist 声明 `com.adobe.pdf` 文档类型（角色 Viewer）；
  注册 = `LSRegisterURL` 登记当前 `.app`；设为默认 = 用 bundle id
  `cn.madeyou.mapdfr` 调 `LSSetDefaultRoleHandlerForContentType`；取消注册只在
  当前默认处理程序是 MaPdfR 时清除。Finder 的「打开方式」会产生
  `QFileOpenEvent`，`main.cpp` 的 `MacOpenEventFilter` 把它转给
  `PdfWorkspace::requestOpen`（含已运行实例）。
- 测试依赖的 objectName：`ReaderPageSpin`、`ReaderPdfView`、`ReaderZoomLabel`、
  `ReaderSearchEdit`、`ReaderSearchStatus`、`ReaderFindToolBar`。

## MuPDF 集成要点

- Windows 用静态库：源文件清单在 configure 时从 MuPDF 自带的
  `platform/win32/libmupdf.vcxproj` / `libthirdparty.vcxproj` 解析，不做手工列表。
- Linux/macOS 用 MuPDF 自带 Makefile 构建的共享库（`libmupdf.so` /
  `libmupdf.dylib`），两边都用 `-DHAVE_LCMS2MT` + 去掉 `sscanf` 的 lcms2
  前缀头；macOS 的产物在 `build/shared-release`，由
  `tools/build-mupdf-macos.sh` 生成。
- 只启用 PDF：`FZ_ENABLE_JS/XPS/SVG/CBZ/IMG/HTML/EPUB/FB2/MOBI/TXT/OFFICE/
  BARCODE/OCR/DOCX/ODT = 0`，`TOFU` + `TOFU_CJK` 只保留 Base14 字体。
- 字体资源由 `scripts/bin2coff.c` 生成 COFF `.obj`，符号名
  `_binary_<Name>_cff` 必须与 `source/fitz/noto.c` 引用一致（连字符转下划线）。
- `WIN32_LEAN_AND_MEAN` / `NOMINMAX` **不能**做成全局定义：MuPDF `time.c` 依赖
  `windows.h` 带进的 `timeval`。它们只加在 `MAPDFR_PLATFORM_DEFINITIONS`。
- MuPDF 目标用 `/W0 /bigobj`；改编译选项时注意别破坏 `/MT` 一致性。
- `thirdparty/lcms2` 单独编成 `mupdf_lcms`，并用前缀头把符号改成 `mpdf_*`
  （清单 `cmake/lcms2-symbols.txt`）。Qt6Pdf 静态链接的 PDFium 自带一份 lcms2，
  同名符号会让链接器把 MuPDF 的调用绑到不兼容的那一份上，GUI 里会在
  `fz_new_context()` 崩溃。完整构建的两个测试目标都链接 Qt6Pdf。升级 MuPDF 后如果出现 `LNK2005` 重定义，用 `dumpbin /symbols`
  重新列出 `mupdf_lcms.lib` 的全局符号（见 `cmake/mupdf.cmake` 里的说明）。

## 转换器约定

- 两遍扫描：第一遍统计正文字号直方图与页眉页脚重复行，第二遍输出。
  进度回调 `total = 页数 * 2`，返回 `false` 表示取消。
- 标题阈值（相对正文字号）：≥1.9 → `#`，≥1.35 → `##`，≥1.15 → `###`。
- `fz_try` / `fz_catch` 基于 `setjmp/longjmp`：try 块内只放 C 指针，
  不要在其中构造需要析构的 C++ 对象（`QString` 等请在 try 外处理）。
- 输出用 `QSaveFile` 原子写入（UTF-8）；图片写到 `<md 文件名>_assets/`，
  失败/取消时清理本次写出的文件，但不删除用户原有目录。
- 新增/修改测试时，断言尽量基于 `tests/data` 里的固定 fixture；
  改 fixture 前先跑 `generate_fixtures.py` 并同步更新断言。

## 发行打包

`tools\make-release.bat` 输出运行时包 `dist\MaPdfR-<版本>\`（程序、说明书、
许可证、`SOURCE-INFO.txt` 与校验值），**不含源代码**。加 `-WithSource` 时另外
在 `dist\` 生成 `MaPdfR-<版本>-source.zip`、`MuPDF-1.27.2-source.zip` 及
`.sha256`，作为独立附件发布。静态 Qt 的重链接脚本随源码提供，正式发布包仍需
加入全部目标文件、库清单和 MuPDF 静态库，并从发行目录执行重链接测试。

麒麟/Windows/macOS 运行时包都不放源码：deb、tar.gz 与 macOS zip 只含程序、
MuPDF 共享库、桌面项/应用包、图标和文档。macOS 包由
`tools/make-release-macos.sh` 生成，`libmupdf.dylib` 在
`MaPdfR.app/Contents/Frameworks`，安装后统一 ad-hoc 签名。

发布渠道（1.0.0 的既有做法，换版本时照此同步）：

- GitHub Release <https://github.com/18902311835/MaPdfR/releases>：
  资产以 `MaPdfR-<版本>-...` 命名（`windows-x64.zip`、`macos-arm64.zip`、
  `linux-aarch64.tar.gz`、`source.zip`，以及麒麟的 `mapdfr_<版本>_arm64.deb`），
  每个包配 `.sha256`。MuPDF 源码保留自身版本名
  `MuPDF-1.27.2-source.zip`，不要改成发行版本号。
- 网站 <https://www.madeyou.cn/software/> 由服务器的
  `ecshop/software/` 提供，只放运行包与校验值；`site/software/index.html`
  是下载页源码副本。上传后在该目录用 `sha256sum -c` 核对。
- 改版本需要同时更新：`CMakeLists.txt` 的版本、下载页、GitHub Release
  资产和服务器上的包。GitHub 的 Packages 计数与 Release 附件无关，保持 0
  属正常。

`pdf.ico` 由 CMake POST_BUILD 复制到 EXE 旁；注册表 `DefaultIcon` 引用它，
缺失时从 `:/pdf.ico` 释放到 `AppLocalDataLocation`，再回退 `MaPdfR.exe,0`。

## 代码风格

- 4 空格缩进；函数体大括号换行（Allman），控制语句大括号同一行；
  `#include` 按现有文件顺序排列。新文件可用 clang-format：
  `BreakBeforeBraces: Custom`（`AfterFunction/Class/Struct/Enum: true`）、
  `AccessModifierOffset: -4`、`ColumnLimit: 100`。
- 用户可见文本用 `tr()` 中文；命令行 / 文档也用中文。
- 编译告警（`/W4`）需处理；第三方代码的告警用 `#pragma warning` 局部屏蔽。

## 许可注意事项

- 本工程以 **GNU AGPL v3** 发布，源码托管在
  <https://github.com/18902311835/MaPdfR>；`LICENSE.txt` 是 AGPL v3 全文，
  `COPYRIGHT.md` 记录版权与第三方组件摘要。不要擅自改许可证文本。
- MuPDF 同为 **AGPL v3**：Windows 版静态链接进 `MaPdfR.exe`，Linux/macOS 版
  随包分发 `libmupdf.so` / `libmupdf.dylib`；两种情况整份二进制都按 AGPL v3
  提供源码。正式发行前核验 MuPDF 对应源码和获取方式。
- Qt 是 **LGPL v3 + 静态链接**（Windows EXE 与 macOS `.app`）：发行包必须保留
  `source\relink\` 目标文件与 `licenses\LGPL-3.0.txt`、`GPL-3.0.txt`；不要删除
  这两部分。macOS 包目前随附许可文本，重链接材料仍需按实际 Qt 版本补齐。
- 第三方组件变动时同步更新 `THIRD_PARTY.md`、`COPYRIGHT.md`、README
  的已知限制与关于窗口的第三方页面。
