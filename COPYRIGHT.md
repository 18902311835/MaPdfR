# 版权与许可

**MaPdfR — Windows 独立 PDF 查看器**

- 版权所有 © 2026 马德有
- 源码仓库：<https://github.com/18902311835/MaPdfR>
- 联系邮箱：<1751515413@qq.com>

## 贡献者

1. [18902311835](https://github.com/18902311835)（马德有）
2. DeepSeek（LLM）
3. Claude Code

## 本工程许可证

本工程（源代码、可执行程序、图标资源及文档）以 **GNU Affero General Public
License v3（AGPL v3）** 发布，许可证全文见 `LICENSE.txt`。你可以自由使用、
修改和分发本软件，但分发时必须遵守 AGPL v3 的条款，包括向接收者提供完整
的对应源码。

选择 AGPL v3 是因为本程序静态链接了同样采用 AGPL v3 的 MuPDF，整份二进制
必须按 AGPL v3 提供源码。

## 第三方组件

本程序使用的第三方组件均为开源软件，各自依据其许可证授权：

| 组件 | 许可证 | 说明 |
|---|---|---|
| Qt 6.11.1（静态） | LGPL v3 / Qt 商业许可 | GUI、PDF 文档模型与 `QPdfView` |
| PDFium（随 Qt PDF） | BSD-3-Clause | PDF 解析与渲染 |
| MuPDF 1.27.2（静态） | AGPL v3 / Artifex 商业许可 | PDF → Markdown 导出 |
| MuPDF 内置库（freetype、zlib、libjpeg、openjpeg、jbig2dec、lcms2、brotli 等） | 各自开源许可 | 见 `THIRD_PARTY.md` |

完整清单、版本、许可文本位置以及源码获取方式见 `THIRD_PARTY.md`。
MuPDF 固定版本 1.27.2 的对应源码与获取方式见 `THIRD_PARTY.md`。

## Qt 静态链接与可重新链接材料

采用 LGPL v3 的静态 Qt 发行需准备可重链接材料，包括应用目标文件
（`.obj` / `.res`）、MuPDF 静态库与 `link-libs.txt`。重链接脚本
`tools/relink.bat` / `tools/relink.ps1` 随源码提供。生成发行所需的目标文件
和库清单后，需验证用匹配的 Qt 执行重链接。`licenses\` 附带 LGPL v3 与 GPL v3 全文
（§4(b)）。也可以直接用 `bld.bat` 指向修改后的 Qt 完整重编。

## 源出处

`PdfMainWindow`、`SafeFileCopy` 等阅读部分提取自同一作者的 QtNotePad 2.36.2，
现在也在本仓库中以 AGPL v3 维护。
