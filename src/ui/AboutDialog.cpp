#include "AboutDialog.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QSysInfo>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>

namespace {

// 页面样式跟随应用调色板，浅色 / 深色 Windows 主题下都能看清。
QString pageStyle(const QPalette& palette)
{
    return QStringLiteral(
        "<style>"
        "body { color: %1; }"
        "h3 { color: %2; margin-top: 14px; margin-bottom: 6px;"
        "     border-bottom: 1px solid %3; padding-bottom: 3px; }"
        "p, li { line-height: 1.6; }"
        "a { color: %2; text-decoration: none; }"
        "table { border-collapse: collapse; margin-top: 4px; }"
        "td { padding: 3px 14px 3px 0; vertical-align: top; }"
        "td.label { color: %4; white-space: nowrap; }"
        "code { background: %5; padding: 1px 4px; }"
        "</style>")
        .arg(palette.color(QPalette::Text).name(QColor::HexRgb),
             palette.color(QPalette::Link).name(QColor::HexRgb),
             palette.color(QPalette::Mid).name(QColor::HexRgb),
             palette.color(QPalette::PlaceholderText).name(QColor::HexRgb),
             palette.color(QPalette::AlternateBase).name(QColor::HexRgb));
}

QTextBrowser* makeNoticePage(QWidget* parent, const QString& html)
{
    auto* browser = new QTextBrowser(parent);
    browser->setOpenExternalLinks(true);
    browser->setFrameShape(QFrame::NoFrame);
    browser->document()->setDocumentMargin(12);
    // 默认样式表必须在 setHtml() 之前设置。
    browser->document()->setDefaultStyleSheet(pageStyle(browser->palette()));
    browser->setHtml(html);
    return browser;
}

QString aboutPageHtml()
{
    QString engine =
        QStringLiteral("Qt PDF（PDFium）");
#ifdef MAPDFR_WITH_MUPDF
    engine += QStringLiteral(" + MuPDF 1.27.2（打印、便笺和导出）");
#endif

    return QString::fromUtf8(R"HTML(<h3>软件信息</h3>
<table>
  <tr><td class='label'>名称：</td><td>MaPdfR</td></tr>
  <tr><td class='label'>版本：</td><td>%1</td></tr>
  <tr><td class='label'>作者：</td><td>马德有</td></tr>
  <tr><td class='label'>贡献者：</td><td>18902311835（马德有）、DeepSeek（LLM）、Claude Code</td></tr>
  <tr><td class='label'>联系邮箱：</td><td><a href='mailto:1751515413@qq.com'>1751515413@qq.com</a></td></tr>
  <tr><td class='label'>源码仓库：</td><td><a href='https://github.com/18902311835/MaPdfR'>github.com/18902311835/MaPdfR</a></td></tr>
  <tr><td class='label'>许可证：</td><td>GNU AGPL v3</td></tr>
  <tr><td class='label'>运行平台：</td><td>%2</td></tr>
  <tr><td class='label'>Qt 版本：</td><td>%3（%4 链接）</td></tr>
  <tr><td class='label'>PDF 引擎：</td><td>%5</td></tr>
  <tr><td class='label'>构建日期：</td><td>%6</td></tr>
</table>

<h3>主要功能</h3>
<ul>
  <li>同一窗口的多文档标签，支持切换、单页与连续视图、书签和缩略图</li>
  <li>放大、缩小、实际大小、适合宽度、整页；工具栏实时显示当前缩放比例</li>
  <li>全文查找（Ctrl+F）、页面文本选择、用户说明书（F1）</li>
  <li>加密 PDF 输入密码；「另存为」原子复制</li>
  <li>启用 MuPDF 时可复制所选文本、打印、查看与保存便笺和导出 Markdown</li>
</ul>

<h3>常用快捷键</h3>
<table>
  <tr><td class='label'>Ctrl+O / Ctrl+S</td><td>打开 / 另存为</td></tr>
  <tr><td class='label'>Ctrl+P / Ctrl+M</td><td>打印 / 导出 Markdown（需 MuPDF）</td></tr>
  <tr><td class='label'>F4 / F11</td><td>导航面板 / 全屏</td></tr>
  <tr><td class='label'>Ctrl+F</td><td>查找</td></tr>
  <tr><td class='label'>F3 / Shift+F3</td><td>下一个 / 上一个查找结果</td></tr>
  <tr><td class='label'>Ctrl+ + / Ctrl+ -</td><td>放大 / 缩小</td></tr>
  <tr><td class='label'>PageUp / PageDown</td><td>上一页 / 下一页</td></tr>
</table>)HTML")
        .arg(QStringLiteral(MAPDFR_APP_VERSION),
             QSysInfo::prettyProductName() + QLatin1Char(' ')
                 + QSysInfo::currentCpuArchitecture(),
             QStringLiteral(QT_VERSION_STR),
#ifdef QT_STATIC
             QStringLiteral("静态"),
#else
             QStringLiteral("共享库"),
#endif
             engine,
             QStringLiteral(__DATE__) + QLatin1Char(' ')
                 + QStringLiteral(__TIME__));
}

QString thirdPartyPageHtml()
{
    return QString::fromUtf8(R"HTML(<h3>第三方组件与许可</h3>
<p>本程序使用的第三方组件<b>全部为开源软件</b>。各组件的版权归其作者所有，
谨此致谢。完整的依赖清单、版本与许可文本位置见发行包内的
<code>THIRD_PARTY.md</code>。</p>

<b>Qt 框架及其内嵌库</b>
<ul>
  <li><b>Qt %1</b>（Qt 6，%2）-
      <a href='https://www.qt.io'>https://www.qt.io</a><br>
      跨平台应用开发框架 · <b>LGPL v3</b>（或 Qt 商业许可）<br>
      © The Qt Company Ltd. 及 Qt 社区贡献者<br>
      已链接模块：Core、Gui、Widgets、Network、Pdf、PdfWidgets。</li>
  <li><b>PDFium</b>（随 Qt PDF 模块）-
      <a href='https://pdfium.googlesource.com/pdfium/'>pdfium.googlesource.com/pdfium</a><br>
      PDF 解析与页面渲染 · <b>BSD-3-Clause</b> · © The PDFium Authors。</li>
  <li><b>Qt 内嵌第三方库</b>（随 Qt 库一并链接，具体以所用 Qt 构建为准）：
      FreeType（FTL / GPLv2 双许可）、HarfBuzz（Old MIT）、
      libjpeg-turbo（IJG / BSD-3）、libpng（PNG Reference Library v2）、
      PCRE2（BSD-3-Clause）、zlib（zlib 许可）。</li>
</ul>

<b>MuPDF 及其内置库</b>
<ul>
  <li><b>MuPDF 1.27.2</b>（可选构建组件）-
      <a href='https://github.com/ArtifexSoftware/mupdf'>github.com/ArtifexSoftware/mupdf</a><br>
      PDF → Markdown 的结构化文本与图片提取 · <b>AGPL v3</b>（或 Artifex 商业许可）<br>
      © Artifex Software, Inc. 及贡献者。</li>
  <li>MuPDF 一并编译的第三方库：freetype（FTL / GPLv2）、zlib（zlib 许可）、
      libjpeg（IJG 许可）、openjpeg（BSD-2-Clause）、jbig2dec（AGPL v3）、
      lcms2（MIT）、brotli（MIT）、MuPDF Base14 字体（GPL v3 + 字体例外 / AGPL）。</li>
</ul>

<b>系统组件</b>（随操作系统提供，不随本程序分发）
<ul>
  <li>Windows 系统库（Windows SDK、GDI/GDI+、Shell 等）由操作系统提供。</li>
</ul>

<h3>源代码</h3>
<p>本工程以 GNU AGPL v3 发布。<a href="https://github.com/18902311835/MaPdfR">MaPdfR 源码仓库</a>。</p>
<p>本工程源码包为 <code>MaPdfR-%3-source.zip</code>。对应源码与 Qt 重链接材料的
发行说明见 <code>THIRD_PARTY.md</code> 与 <code>PROFESSIONAL_REVIEW.md</code>。</p>
<p>发行包由 <code>tools\make-release.bat</code> 生成，逐文件校验值见
<code>SHA256SUMS.txt</code>。</p>)HTML")
        .arg(QStringLiteral(QT_VERSION_STR),
#ifdef QT_STATIC
             QStringLiteral("静态链接")
#else
             QStringLiteral("共享库链接")
#endif
             ,
             QStringLiteral(MAPDFR_APP_VERSION));
}

QString licensePageHtml()
{
    return QString::fromUtf8(R"HTML(<h3>版权声明</h3>
<p><b>版权所有 © 2026 马德有。</b></p>
<p>本工程以 GNU Affero General Public License v3（AGPL v3）发布。
许可证全文见 <code>LICENSE.txt</code>。第三方组件保留各自许可证。</p>

<h3>许可与重新链接</h3>
<p>Qt（LGPL v3 或 Qt 商业许可）、PDFium（BSD-3-Clause）及 MuPDF（AGPL v3 或
Artifex 商业许可）的许可与源码说明见 <code>THIRD_PARTY.md</code>。采用静态 Qt
发行时，按所采用的 Qt 许可证准备并验证源码与重链接材料。</p>

<h3>免责声明</h3>
<p>本软件按「现状」提供，不附带任何明示或暗示的担保。作者不对因使用本软件
造成的任何损失承担责任。</p>

<h3>致谢</h3>
<p>感谢 Qt、PDFium、MuPDF 等开源项目及其贡献者。正是这些项目使本工具成为可能。</p>)HTML");
}

} // namespace

AboutDialog::AboutDialog(QWidget* parent, Page initialPage)
    : QDialog(parent)
{
    setWindowTitle(tr("关于 MaPdfR"));
#ifdef Q_OS_WIN
    setWindowIcon(QIcon(QStringLiteral(":/app.ico")));
#else
    setWindowIcon(QIcon(QStringLiteral(":/app.png")));
#endif
    setMinimumSize(760, 620);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(16);

    auto* iconLabel = new QLabel(this);
    QPixmap iconPixmap(QStringLiteral(":/app.png"));

    if (iconPixmap.isNull())
        iconPixmap = QPixmap(QStringLiteral(":/app.ico"));

    if (!iconPixmap.isNull()) {
        iconLabel->setPixmap(iconPixmap.scaled(
            72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        iconLabel->setText(QStringLiteral("PDF"));
        QFont iconFont = iconLabel->font();
        iconFont.setPointSize(28);
        iconFont.setBold(true);
        iconLabel->setFont(iconFont);
    }

    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setFixedSize(80, 80);
    headerLayout->addWidget(iconLabel);

    auto* titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(4);

    auto* nameLabel = new QLabel(QStringLiteral("MaPdfR"), this);
    QFont nameFont = nameLabel->font();
    nameFont.setPointSize(20);
    nameFont.setBold(true);
    nameLabel->setFont(nameFont);

    auto* versionLabel = new QLabel(
        tr("版本 %1").arg(QStringLiteral(MAPDFR_APP_VERSION)), this);
#ifdef Q_OS_WIN
    const QString description = tr("Windows 独立 PDF 查看器 · 作者 马德有");
#else
    const QString description = tr("Linux 独立 PDF 查看器 · 作者 马德有");
#endif
    auto* descLabel = new QLabel(description, this);

    titleLayout->addWidget(nameLabel);
    titleLayout->addWidget(versionLabel);
    titleLayout->addWidget(descLabel);
    titleLayout->addStretch();

    headerLayout->addLayout(titleLayout, 1);
    mainLayout->addLayout(headerLayout);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(makeNoticePage(tabs, aboutPageHtml()), tr("关于"));
    tabs->addTab(makeNoticePage(tabs, thirdPartyPageHtml()),
                 tr("第三方组件"));
    tabs->addTab(makeNoticePage(tabs, licensePageHtml()),
                 tr("版权与许可"));
    tabs->setCurrentIndex(static_cast<int>(initialPage));
    mainLayout->addWidget(tabs, 1);

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);

    auto* qtAboutButton = new QPushButton(tr("关于 Qt"), this);
    qtAboutButton->setToolTip(tr("显示 Qt 库版本和许可信息"));
    connect(qtAboutButton, &QPushButton::clicked,
            qApp, &QApplication::aboutQt);
    buttonLayout->addWidget(qtAboutButton);
    buttonLayout->addStretch();

    auto* okButton = new QPushButton(tr("确定"), this);
    okButton->setDefault(true);
    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(okButton);

    mainLayout->addLayout(buttonLayout);
}
