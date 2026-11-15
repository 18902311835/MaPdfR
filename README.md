# MaPdfR — 独立 PDF 阅读器

MaPdfR 1.0.0 —— Windows 与麒麟（Linux aarch64）独立 PDF 阅读器（Qt 6.11.1 静态构建）。查找、打印、便笺与 PDF → Markdown 导出；AGPL v3，附源码与重链接材料

MaPdfR 是从作者开发 **QtNotePad 2.36.2** 中提取 PDF 阅读器代码后重新搭建的独立
Qt 桌面程序。阅读界面基于 Qt 6 的
`QPdfDocument` / `QPdfView`，文件另存使用 `SafeFileCopy`。

启用 **MuPDF 1.27.2** 后可打印、添加和查看原生 PDF 便笺、复制所选文本或整页文本及导出
Markdown。Adobe Reader 9.1 中文版参照与测试范围见 [专业应用评审](PROFESSIONAL_REVIEW.md)。

## 许可证与源码

本工程以 **GNU AGPL v3** 发布，源码托管在 GitHub：
<https://github.com/18902311835/MaPdfR>

- 许可证全文：`LICENSE.txt`
- 版权、第三方组件与源码获取：`COPYRIGHT.md`、`THIRD_PARTY.md`
- 选择 AGPL v3 是因为本程序静态链接了同为 AGPL v3 的 MuPDF，整份二进制
  须按 AGPL v3 提供源码。

## 贡献者

1. 马德有[18902311835](https://github.com/18902311835)
2. DeepSeek（LLM API provider）
3. Claude Code (Agent Tool provider)

## 功能

- 打开 PDF，支持命令行参数、窗口拖放和「打开」对话框
- 多文档：同一个窗口显示多个 PDF 标签。切换时保留各自的页码、缩放、查找和导航状态
- 资源管理器再次打开 PDF 时，同一登录会话中的同一份 MaPdfR 会复用已运行窗口。重新打开同一路径会切到已有标签
- 单页与连续视图、首页／末页、指定页码、前进／后退、全屏
- 导航面板，含树形书签、按需渲染的页面缩略图和 PDF 页码标签
- 文档属性，含标题、作者、生成器、创建与修改时间等元数据
- 缩放：放大、缩小、实际大小、适合宽度、整页；工具栏实时显示当前缩放比例
- 全文查找，支持搜索进度、上下文结果列表、页内高亮、上下一个命中
- 打印全部、当前页或页码范围，使用系统打印设置。受限打印按文档权限控制清晰度
- 审阅菜单可查看当前页便笺，在页面预览中定位便笺并保存原生 PDF 批注副本
- 在页面拖选文字，按 `Ctrl+C` 或右击复制所选文本。「编辑」也可复制当前页文本
- 在「帮助 → 用户说明书」或按 `F1` 打开内置中文说明书
- PDF 文件类型注册：`帮助 → 注册 PDF 文件类型…` 把 MaPdfR 加入 `.pdf`
  的「打开方式」列表，并可经 Windows 官方对话框设为默认；随附专用 PDF 文件图标
- `帮助 → 关于 MaPdfR…`：版本、作者、第三方组件（全部开源）及许可说明
- 加密 PDF：弹出密码输入框，最多重试 3 次
- 「另存为」：以 `QSaveFile` 原子复制，不会留下半截文件
- 「导出 Markdown」：菜单 / 工具栏 / `Ctrl+M`，带进度对话框，可中途取消；
  图片写到与 `.md` 同级的 `<文件名>_assets` 目录
- 嵌入 Qt 中文翻译，标准对话框按钮为中文
- 静态 Qt + 静态 MuPDF 构建：最终只有一个自包含的 `MaPdfR.exe`
  （按项目的 Windows 静态 kit 构建）

## 源码来源

| 本工程文件 | 来源 | 说明 |
|---|---|---|
| `src/ui/PdfMainWindow.{h,cpp}` | `QtNotePad/src/Readers/PdfMainWindow.*` | 阅读器主体和文件操作入口 |
| `src/ui/PdfWorkspace.{h,cpp}` | 新增 | 文档标签、切换与文件打开队列 |
| `src/app/LocalOpenRelay.{h,cpp}` | 新增 | 向同一个已运行窗口转发打开请求 |
| `src/ui/ReaderNavigation.{h,cpp}` | 新增 | 书签、缩略图与搜索结果面板 |
| `src/io/PdfDocumentOperations.{h,cpp}` | 新增 | 打印、页文本、便笺与文档权限 |
| `src/ui/PdfDocumentDialogs.{h,cpp}` | 新增 | 打印和便笺对话框 |
| `src/io/SafeFileCopy.{h,cpp}` | `QtNotePad/src/Readers/SafeFileCopy.*` | 原样复制 |
| `src/convert/PdfToMarkdown.{h,cpp}` | 新增 | 基于 MuPDF 的 PDF → Markdown 转换器 |
| `src/app/main.cpp` | 新增 | 程序入口、打开文件、错误提示 |
| `src/app/FileAssociation.{h,cpp}` | 新增（参考 VSDX 的 OleRegistration） | HKCU PDF 文件类型注册、系统「打开方式」对话框 |
| `src/ui/AboutDialog.{h,cpp}` | 新增（版式参考 VSDX 的 AboutDialog） | 关于 / 第三方组件 / 版权与许可 |
| `tools/make-release.{bat,ps1}` | 新增 | 生成发行目录（EXE + 文档 + 附带源码 + 校验值） |
| `cmake/mupdf.cmake` | 新增 | Windows 静态 MuPDF 集成及 PDF 回退字体 |
| `CMakeLists.txt`、`bld.bat` | 按 MaPstViewer / DYBANA_Qt 的约定新写 | 静态 Qt + 静态 MuPDF + MSVC |

## 目录结构

```
MaPdfR/
├── CMakeLists.txt
├── bld.bat                  # 一键配置 + 编译 + ctest
├── README.md  PROFESSIONAL_REVIEW.md  THIRD_PARTY.md  COPYRIGHT.md  LICENSE.txt
├── licenses/                  # LGPL v3 与 GPL v3 全文
├── cmake/
│   ├── mapdfr-app.rc.in     # Windows 图标 / 版本资源
│   └── mupdf.cmake          # MuPDF 静态库集成
├── resources/
│   ├── app.ico  app.png           # 程序图标
│   ├── pdf.ico  pdf.png           # .pdf 文件类型图标（多尺寸）
│   └── app.qrc
├── tools/
│   ├── make-release.bat/.ps1      # 发行打包（含附带源码）
│   └── relink.bat/.ps1            # Qt 静态重链接入口
├── src/
│   ├── app/main.cpp
│   ├── convert/PdfToMarkdown.{h,cpp}
│   ├── io/                     # 原子另存与 MuPDF 文档操作
│   └── ui/                     # 主窗口、导航和操作对话框
└── tests/
    ├── data/
    │   ├── sample-*.pdf          # 导航、权限、签名字段和导出样本
    │   └── generate_fixtures.py   # 重新生成测试 PDF（需要 PyMuPDF）
    ├── reader_smoke.cpp
    └── markdown_export.cpp
```

## 构建

要求：Visual Studio 2022（x64 C++ 工具链）、CMake ≥ 3.16、Ninja、
含 **Qt PDF / Qt PDF Widgets / Qt Print Support / Qt Test** 的 Qt 6.8 或以上（Windows 默认静态 kit
`E:\QT\6.11.1\msvc2022_64-static`），以及 **MuPDF 1.27.2 源码树**
（默认 `E:\third_party\mupdf-1.27.2-source`）。

```bat
bld.bat          :: 配置 + 编译 + 运行测试
bld.bat clean    :: 删除 build\ 后重新构建
bld.bat run      :: 构建完成后启动 MaPdfR.exe
```

可用环境变量覆盖默认路径：`QT_PREFIX`、`MUPDF_DIR`、`VCVARS`、
`CMAKE_BIN`、`NINJA_BIN`。

手动构建：

```bat
cmake -S . -B build\msvc2022_64-static -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=E:/QT/6.11.1/msvc2022_64-static ^
      -DMUPDF_SOURCE_DIR=E:/third_party/mupdf-1.27.2-source
cmake --build build\msvc2022_64-static --parallel
ctest --test-dir build\msvc2022_64-static --output-on-failure
```

`cmake/mupdf.cmake` 直接读取 MuPDF 自带的 MSVC 工程
（`platform/win32/libmupdf.vcxproj` / `libthirdparty.vcxproj`）来生成源文件
清单，只编译 PDF 需要的部分（关闭 HTML / XPS / SVG / CBZ / JavaScript /
OCR / Office 输出 / 条码），并用 MuPDF 自带的 `scripts/bin2coff.c` 把
Base14 回退字体嵌入静态库。

可用 `-DMAPDFR_WITH_MUPDF=OFF` 构建 Qt-only 阅读器。此构建保留阅读、
导航、搜索、文本选择高亮、另存与帮助。打印、便笺、复制所选文本或整页文本和 Markdown 导出需要启用 MuPDF。

Linux 上可使用 Qt 6.8 或以上进行 Qt-only 构建和测试：

```sh
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DMAPDFR_WITH_MUPDF=OFF
cmake --build build/linux --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux --output-on-failure
```

### 麒麟 / Linux 完整构建与 deb 打包

完整功能需要先用 MuPDF 自带 Makefile 生成共享库 `libmupdf.so`：

```sh
tools/build-mupdf-linux.sh /path/to/mupdf-1.27.2-source
```

脚本只编译 PDF 需要的部分，并给 MuPDF 自带的 lcms2 符号加 `mpdf_` 前缀，
避免与静态 Qt 里 PDFium 的副本冲突。随后构建应用并生成 deb：

```sh
tools/make-deb-linux.sh
# 产物：build/linux-deb/mapdfr_<版本>_arm64.deb
```

默认使用静态 Qt `/data/Qt/6.11.1-aarch64-static`、MuPDF 源码
`/usb1tb/202611OK/mupdf-1.27.2-source`；可用 `QT_PREFIX`、`MUPDF_DIR`、
`OPENSSL_ROOT_DIR`、`BUILD_DIR` 覆盖。麒麟 V10 自带的 CMake 3.16 低于
Qt 6.11 要求的 3.22，脚本会优先使用 `~/.local/bin/cmake`。

应用用共享库 `libmupdf.so` 而不是静态库：静态链接时 MuPDF 自带的
libjpeg / zlib / libpng 会与 `Qt6Pdf` 里 PDFium 的副本重复定义。共享库以
`-Bsymbolic` 构建，两边互不干扰。

deb 安装布局：

| 路径 | 内容 |
|---|---|
| `/usr/bin/mapdfr` | 主程序（静态 Qt + MuPDF 接口，RPATH `$ORIGIN/../lib/mapdfr`） |
| `/usr/lib/mapdfr/libmupdf.so.27.2` | MuPDF 运行时库 |
| `/usr/share/applications/mapdfr.desktop` | 桌面启动项，`MimeType=application/pdf` |
| `/usr/share/icons/hicolor/*/apps/mapdfr.png` | 16–256 像素启动器图标 |
| `/usr/share/doc/mapdfr/` | 说明书、README、许可证与第三方组件说明 |

系统依赖由 `dpkg-shlibdeps` 计算（X11 / glib / fontconfig / ICU 等），
无需安装 Qt 或 MuPDF 的开发包即可运行。无显示环境跑测试时用
`xvfb-run -a ctest`。

不需要安装（也不用 sudo）时，用便携包：

```sh
tools/make-tar-linux.sh
# 产物：build/linux-deb/MaPdfR-<版本>-linux-aarch64.tar.gz
tar xzf MaPdfR-<版本>-linux-aarch64.tar.gz
cd MaPdfR-<版本>-linux-aarch64 && ./mapdfr 示例.pdf
./install-user.sh          # 可选：安装到 ~/.local，带桌面项与图标
```

产物（Windows）：`build\msvc2022_64-static\MaPdfR.exe`。

## 使用

```bat
MaPdfR.exe                          :: 打开阅读窗口或激活已有窗口
MaPdfR.exe D:\docs\manual.pdf       :: 直接打开
MaPdfR.exe a.pdf b.pdf              :: 各文件在同一窗口的标签中打开
```

支持用「打开」一次选取多份 PDF，也可把多个 PDF 拖入窗口。点击顶部标签切换文档。
关闭最后一个标签后，可以在原窗口继续打开 PDF。

| 快捷键 | 功能 |
|---|---|
| `Ctrl+O` | 打开 |
| `Ctrl+S` | 另存为 |
| `Ctrl+P` / `Ctrl+M` | 打印 / 导出 Markdown（需 MuPDF） |
| `Ctrl+C` | PDF 页面取得焦点时复制选区（需 MuPDF） |
| `Ctrl+D` / `Ctrl+G` | 文档属性 / 定位到页码输入框 |
| `F4` / `F11` | 显示或隐藏导航 / 全屏 |
| `Ctrl+ +` / `Ctrl+ -` | 放大 / 缩小 |
| `Ctrl+F` / `Ctrl+Shift+F` | 查找 / 打开结果列表 |
| `F3` / `Shift+F3` | 下一个 / 上一个查找结果 |
| `Esc` | 关闭查找栏（查找栏可见时） |
| `PageUp` / `PageDown` | 上一页 / 下一页 |
| `Alt+←` / `Alt+→` | 上一个 / 下一个视图 |
| `F1` | 用户说明书 |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | 切换到下一个 / 上一个 PDF 标签 |
| `Ctrl+W` | 关闭当前 PDF 标签 |
| `Ctrl+Q` | 退出 |

## 文件关联与关于窗口

- 「帮助 → 注册 PDF 文件类型…」：在 `HKCU\Software\Classes` 写入
  `MaPdfR.PDFDocument`（ProgID、`DefaultIcon`、`shell\open\command`、
  `Applications\MaPdfR.exe`），并把 ProgID 加进 `.pdf` 的 `OpenWithProgids`。
  随后可点「是」打开 Windows 的「打开方式」对话框，勾选「始终使用此应用」。
- 「帮助 → 取消 PDF 文件类型注册」：只删除本程序写入的项，不改 `.pdf`
  原有的默认处理程序。
- 「帮助 → 设为默认 PDF 打开程序…」：注册后调用系统对话框（Windows 8+
  禁止程序静默修改默认程序）。
- Linux / 麒麟上三个菜单项做对应的事：「注册」把带 `MimeType=application/pdf`
  的用户桌面项写到 `~/.local/share/applications/mapdfr.desktop`；「取消注册」
  删除该文件并清理 `~/.config/mimeapps.list` 里的条目；「设为默认」调用
  `xdg-mime default`。deb 安装的桌面项在 `/usr/share/applications`，同样参与
  系统的「打开方式」。
- 「帮助 → 用户说明书」（`F1`）可离线打开中文使用说明。
- 「帮助 → 关于 MaPdfR…」：版本、构建信息、第三方组件与许可、
  源码附随说明。

## 发布打包

```bat
bld.bat                     :: 先构建并跑测试
tools\make-release.bat      :: 生成运行时包 dist\MaPdfR-<版本>\（不含源码）
tools\make-release.bat -WithSource   :: 另外在 dist\ 生成源码包
tools\make-release.bat -WithSource -SkipMuPdfSource   :: 不打包 MuPDF 源码
```

发行目录（运行时包）包含：

| 内容 | 说明 |
|---|---|
| `MaPdfR.exe`、`pdf.ico`、`app.ico` | 程序与文件类型图标 |
| `README.md`、`PROFESSIONAL_REVIEW.md`、`THIRD_PARTY.md`、`COPYRIGHT.md`、`LICENSE.txt` | 说明、评审、版权与许可 |
| `user-guide.zh_CN.html`、`licenses/` | 离线说明书及随附许可文本 |
| `SOURCE-INFO.txt` | 第三方组件许可与源码获取 / 重建说明 |
| `SHA256SUMS.txt` | 逐文件校验值 |

运行时包**不包含源代码**。源码作为独立附件发布：

| 内容 | 说明 |
|---|---|
| `dist\MaPdfR-<版本>-source.zip` | 本工程源码、构建脚本与测试 |
| `dist\MuPDF-1.27.2-source.zip` | MuPDF 完整源码（排除 VS 构建中间产物） |
| `dist\*.sha256` | 上述源码包的校验值 |

`MUPDF_DIR` 指向的源码树若不存在，脚本会给出警告并跳过 MuPDF 源码包；
正式发行前由发行方核验对应源码的获取方式、版本与实际构建的一致性。
上传包中的 `tools\relink.bat` 与 `tools\relink.ps1` 已随源码保留。
当前打包脚本还需生成并验证其所需的 `source\relink\obj` 和 `link-libs.txt`。

Linux / 麒麟发行包用 `tools/make-deb-linux.sh` 生成
`build/linux-deb/mapdfr_<版本>_arm64.deb`，同一目录下还有 `.sha256`
校验值。deb 按 FHS 安装程序、MuPDF 共享库、桌面项、图标、说明书和许可证。

## PDF 转 Markdown

菜单「文件 → 导出 MD...」、工具栏「导出 MD...」或 `Ctrl+M`。转换规则：

| PDF 内容 | Markdown 输出 |
|---|---|
| 字号明显大于正文的短段落 | `#` / `##` / `###` 标题（按与正文字号的比例判定） |
| 正文 | 段落；同段内多行按阅读顺序合并，中文 / 日文之间不补空格 |
| 粗体 / 斜体 | `**粗体**`、`*斜体*`（依据字体标记与字体名判断） |
| 项目符号 / 编号行 | `- 条目`、`1. 条目` |
| 图片 | 写入 `<文件名>_assets/`，正文中插入 `![](<assets/image-0001.png>)` |
| 在多数页面顶部 / 底部重复出现的行 | 视为页眉页脚，丢弃（页码按数字归一化比较） |

Markdown 用 `QSaveFile` 原子写入。图片目录已存在时，本次导出会使用带后缀的新目录。
取消只清理本次导出的资源。加密 PDF 复用打开时输入的密码。

限制：扫描件只会导出页面图片，不做 OCR；表格按普通文本行输出（不生成
Markdown 表格）；公式、多栏排版、脚注的还原效果取决于原 PDF 的结构。

## 已知限制

- 只显示 PDF；DOCX/XLSX/PPTX 的原生阅读器、Office→PDF 转换都不在本工程内。
- 选区以单页为范围。数字签名验证、交互表单填写和附件浏览尚未实现。
- 主阅读区使用 Qt PDF。原生便笺正文可在「审阅 → 查看当前页便笺」中查看。
- 打印为适应纸张的栅格输出，普通权限最高 300 dpi，低清权限最高 150 dpi，超大页面按像素预算进一步缩放。
- 查找基于 PDF 文本层：扫描件（整页图片）没有可搜索的文本，结果会显示「无结果」。
- 静态 Qt 下 `MaPdfR.exe` 体积较大（PDFium + MuPDF 引擎都静态链接），属正常现象。
- **MuPDF 使用 AGPL v3**：静态链接后的二进制若对外分发，需要按 AGPL 提供
  完整对应源码，或另行取得 Artifex 的商业许可；详见 `THIRD_PARTY.md`。
