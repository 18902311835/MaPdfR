# 第三方运行时依赖

本文件说明 MaPdfR 的第三方运行时组件。

- 本工程以 **GNU AGPL v3** 发布，源码仓库：
  <https://github.com/18902311835/MaPdfR>；版权与许可说明见 `COPYRIGHT.md`，
  许可证全文见 `LICENSE.txt`。
- 本工程源码以 AGPL v3 提供；第三方依赖在构建时静态链接进 `MaPdfR.exe`，
  各自保留其原有许可证。
- 分发二进制时须保留各组件的版权与许可声明，并按 AGPL v3 提供完整对应源码。

| 组件 | 版本 | 本构建中的用途 | 许可证 | 许可文本位置 |
|---|---|---|---|---|
| Qt | 6.11.1（静态，本机构建） | GUI、网络、PDF 文档模型、`QPdfView` 与 Print Support | LGPL v3 或 Qt 商业许可 | Qt 安装目录（`E:/QT/6.11.1/msvc2022_64-static`） |
| PDFium | 随所用 Qt 的 Qt PDF 模块 | PDF 解析与页面渲染（`QPdfDocument` 的底层引擎） | BSD-3-Clause | Qt 安装目录的 `sbom/` 与 Qt 源码 `src/3rdparty/pdfium` |
| qtbase_zh_CN.qm | 随所用 Qt 6.11.1 | Qt 标准按钮与对话框的中文翻译（已嵌入资源） | LGPL v3（与 Qt 相同） | Qt 安装目录 `translations/` |
| MuPDF | 1.27.2（静态，源码编译） | 复制权限、打印栅格化、PDF 便笺与 Markdown 导出 | **AGPL v3**（或 Artifex 商业许可） | `E:/third_party/mupdf-1.27.2-source/COPYING` |
| MuPDF 内置第三方库 | 随 MuPDF 1.27.2 源码 | MuPDF 的解析 / 字体 / 图像依赖 | 见下方 | `E:/third_party/mupdf-1.27.2-source/thirdparty/` |

MuPDF 静态链接时一并编译的第三方库（均来自 MuPDF 源码树的
`thirdparty/` 目录，仅选用 PDF 所需部分）：

| 组件 | 用途 | 许可证 |
|---|---|---|
| freetype | 字体解析与栅格化 | FreeType License 或 GPL v2 |
| zlib | Flate 压缩 | zlib License |
| libjpeg | JPEG 解码 | IJG License |
| openjpeg | JPEG 2000 解码 | BSD-2-Clause |
| jbig2dec | JBIG2 解码 | AGPL v3（Artifex） |
| lcms2 | ICC 色彩管理 | MIT |
| brotli | Brotli 压缩 | MIT |
| MuPDF Base14 字体（URW） | 标准 14 字体的替代字形 | GPL v3 + 字体例外，或 AGPL |

Qt 静态构建内嵌的第三方库（FreeType、HarfBuzz、libjpeg-turbo、libpng、
PCRE2、zlib 等）以所用 Qt 构建为准，其许可文本随 Qt 一同提供。

Qt 为 LGPL 组件，本工程以**静态链接**方式使用它。LGPL 要求向使用者提供
替换该库所需的形式（可重新链接的目标文件，或相应库的源码）：

- 静态 Qt 发行所需的重链接材料包括本程序全部 `.obj` / `.res`、MuPDF 静态库、
  `link-libs.txt` 与 `relink.bat` / `relink.ps1`，以便用修改后的 Qt 重新链接
  （LGPL v3 §4(d)(0)）；
- 发行包 `licenses\` 附带 LGPL v3 与 GPL v3 全文（LGPL v3 §4(b)）；
- 也可以用 `bld.bat` 指向修改后的 Qt 完整重编。

- Qt 源码：<https://www.qt.io>
- PDFium 源码：<https://pdfium.googlesource.com/pdfium/>

若不需要承担该义务，也可改用 Qt 商业许可，或改为共享（动态）Qt 构建
（`-DCMAKE_PREFIX_PATH` 指向动态 Qt kit；此时需用 `windeployqt` 部署
Qt DLL 与插件）。

## MuPDF 与 AGPL

MuPDF 以 **GNU Affero GPL v3** 发布，并静态链接进 `MaPdfR.exe`。按照 AGPL
的要求，**若对外分发本程序的二进制**，必须同时向接收者提供完整的对应源码
（以 AGPL 条款授权），包括本工程的 PDF → Markdown 部分。仅在本机个人使用
不触发分发义务。

如果本程序不允许以 AGPL 源码形式分发，可以：

- 从 Artifex 购买 MuPDF 商业许可（<https://artifex.com/licensing/>）；
- 或使用 `-DMAPDFR_WITH_MUPDF=OFF` 构建不含 MuPDF 的版本（没有
  PDF → Markdown 功能，但阅读功能不受影响）。

jbig2dec 与 MuPDF 自带字体为 Artifex 的 AGPL 组件；若采用商业许可，
需确认许可范围覆盖这些组件。

MuPDF 源码：<https://github.com/ArtifexSoftware/mupdf>（1.27.2）

## 发行包中的源码

本工程源码仓库为 <https://github.com/18902311835/MaPdfR>。
打包脚本会生成本工程源码 ZIP。找到 MuPDF 源码树时，它还会归档该树。
正式发行前核验源码、记录的版本、库配置和最终二进制一致。保留随附许可文本。
上传包所附 Qt 重链接脚本保留在 `tools/`，正式发行包需生成其所需的目标文件、
静态库和 `link-libs.txt`，然后验证从发行目录重链接和运行。详见
[正式发行前评审](PROFESSIONAL_REVIEW.md)。
