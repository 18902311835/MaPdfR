#include "PdfToMarkdown.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPdfDocument>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QString testData(const QString& name)
{
    return QStringLiteral(MAPDFR_TEST_DATA_DIR "/") + name;
}

bool readUtf8(const QString& path, QString* text)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    *text = QString::fromUtf8(file.readAll());
    return true;
}

} // namespace

class MarkdownExport : public QObject
{
    Q_OBJECT

private slots:
    void missingInputFails();
    void corruptInputFails();
    void samplePdfConverts();
    void richPdfConverts();
    void encryptedPdfRequiresPassword();
    void unicodePathConverts();
    void cancelStopsExport();
    void repeatExportPreservesExistingImages();
    void coexistsWithQtPdf();
};

void MarkdownExport::missingInputFails()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Reader::MarkdownExportResult result;
    QString error;

    QVERIFY(!Reader::exportPdfToMarkdown(QStringLiteral("Z:/__mapdfr_no_such_file__.pdf"),
                                         dir.filePath(QStringLiteral("out.md")), QString(),
                                         Reader::MarkdownProgressFn(), &result, &error));

    QVERIFY(!error.isEmpty());
}

void MarkdownExport::corruptInputFails()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString pdfPath = dir.filePath(QStringLiteral("broken.pdf"));

    {
        QFile file(pdfPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("this is not a PDF document");
    }

    Reader::MarkdownExportResult result;
    QString error;

    QVERIFY(!Reader::exportPdfToMarkdown(pdfPath, dir.filePath(QStringLiteral("out.md")), QString(),
                                         Reader::MarkdownProgressFn(), &result, &error));

    QVERIFY(!error.isEmpty());
}

void MarkdownExport::samplePdfConverts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString output = dir.filePath(QStringLiteral("sample.md"));

    Reader::MarkdownExportResult result;
    QString error;

    int lastDone = 0;
    int lastTotal = 0;

    const bool ok = Reader::exportPdfToMarkdown(
        testData(QStringLiteral("sample.pdf")), output, QString(),
        [&lastDone, &lastTotal](int done, int total) {
            lastDone = done;
            lastTotal = total;
            return true;
        },
        &result, &error);

    QVERIFY2(ok, qPrintable(error));
    QVERIFY(result.pageCount > 0);
    QVERIFY(result.imageCount == 0);
    QVERIFY(lastTotal == result.pageCount * 2);
    QVERIFY(lastDone <= lastTotal);

    QString markdown;
    QVERIFY(readUtf8(output, &markdown));
    QVERIFY(markdown.contains(QStringLiteral("PDF reader")));
}

void MarkdownExport::richPdfConverts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString output = dir.filePath(QStringLiteral("rich.md"));

    Reader::MarkdownExportResult result;
    QString error;

    QVERIFY2(Reader::exportPdfToMarkdown(testData(QStringLiteral("sample-rich.pdf")), output,
                                         QString(), Reader::MarkdownProgressFn(), &result, &error),
             qPrintable(error));

    QString markdown;
    QVERIFY(readUtf8(output, &markdown));

    // 标题层级（24pt / 16pt / 13pt 对应 # / ## / ###）。
    QVERIFY(markdown.contains(QStringLiteral("# MaPdfR")));
    QVERIFY(markdown.contains(QStringLiteral("## 1. 简介")));
    QVERIFY(markdown.contains(QStringLiteral("### 2.1 英文段落")));

    // 行内样式。
    QVERIFY(markdown.contains(QStringLiteral("**bold**")));
    QVERIFY(markdown.contains(QStringLiteral("*italic*")));

    // 列表。
    QVERIFY(markdown.contains(QStringLiteral("- 第一项：提取文字")));
    QVERIFY(markdown.contains(QStringLiteral("- 第二项：保留图片")));

    // 图片与资源目录。
    QCOMPARE(result.imageCount, 1);
    QVERIFY(markdown.contains(QStringLiteral("![](<rich_assets/image-0001.png>)")));
    QVERIFY(!result.assetsDirectory.isEmpty());
    QVERIFY(
        QFileInfo::exists(QDir(result.assetsDirectory).filePath(QStringLiteral("image-0001.png"))));

    // 重复的页眉 / 页脚应被丢弃。
    QVERIFY(!markdown.contains(QStringLiteral("示例文档")));
    QVERIFY(!markdown.contains(QStringLiteral("- 1 -")));
    QVERIFY(!markdown.contains(QStringLiteral("- 2 -")));

    // 第二页正文仍在。
    QVERIFY(markdown.contains(QStringLiteral("第二页用来验证跨页处理")));
}

void MarkdownExport::encryptedPdfRequiresPassword()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString pdf = testData(QStringLiteral("sample-encrypted.pdf"));
    const QString output = dir.filePath(QStringLiteral("encrypted.md"));

    QString error;

    // 密码错误：必须失败并给出提示。
    QVERIFY(!Reader::exportPdfToMarkdown(pdf, output, QStringLiteral("wrong-password"),
                                         Reader::MarkdownProgressFn(), nullptr, &error));

    QVERIFY(error.contains(QStringLiteral("密码")));
    QVERIFY(!QFileInfo::exists(output));

    // 正确密码：正常导出。
    QVERIFY2(Reader::exportPdfToMarkdown(pdf, output, QStringLiteral("mapdfr"),
                                         Reader::MarkdownProgressFn(), nullptr, &error),
             qPrintable(error));

    QString markdown;
    QVERIFY(readUtf8(output, &markdown));
    QVERIFY(markdown.contains(QStringLiteral("PDF reader")));
}

void MarkdownExport::unicodePathConverts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString pdfPath = dir.filePath(QStringLiteral("示例 文档.pdf"));
    QVERIFY(QFile::copy(testData(QStringLiteral("sample.pdf")), pdfPath));

    const QString output = dir.filePath(QStringLiteral("导出 结果.md"));

    QString error;

    QVERIFY2(Reader::exportPdfToMarkdown(pdfPath, output, QString(), Reader::MarkdownProgressFn(),
                                         nullptr, &error),
             qPrintable(error));

    QVERIFY(QFileInfo::exists(output));
}

void MarkdownExport::cancelStopsExport()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString output = dir.filePath(QStringLiteral("cancelled.md"));

    QString error;
    int calls = 0;

    const bool ok = Reader::exportPdfToMarkdown(
        testData(QStringLiteral("sample.pdf")), output, QString(),
        [&calls](int, int) {
            ++calls;
            return false;
        },
        nullptr, &error);

    QVERIFY(!ok);
    QVERIFY(calls > 0);
    QVERIFY(error.contains(QStringLiteral("取消")));
    QVERIFY(!QFileInfo::exists(output));
}

void MarkdownExport::repeatExportPreservesExistingImages()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir base(dir.path());
    QVERIFY(base.mkdir(QStringLiteral("report_assets")));
    const QString oldImage = dir.filePath(QStringLiteral("report_assets/image-0001.png"));
    const QString output = dir.filePath(QStringLiteral("report.md"));
    const QByteArray imageSentinel("existing asset");
    const QByteArray markdownSentinel("existing report");
    for (const auto& item : {qMakePair(oldImage, imageSentinel),
                             qMakePair(output, markdownSentinel)}) {
        QFile file(item.first);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(item.second), item.second.size());
    }

    const QString input = testData(QStringLiteral("sample-rich.pdf"));
    QString error;
    QVERIFY(!Reader::exportPdfToMarkdown(
        input, output, QString(), [](int done, int total) { return done <= total / 2; },
        nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("取消")));
    QCOMPARE(base.entryList(QDir::Dirs | QDir::NoDotAndDotDot),
             QStringList{QStringLiteral("report_assets")});
    QString markdown;
    QVERIFY(readUtf8(output, &markdown));
    QCOMPARE(markdown, QString::fromUtf8(markdownSentinel));

    Reader::MarkdownExportResult result;
    QVERIFY2(Reader::exportPdfToMarkdown(input, output, QString(), {}, &result, &error),
             qPrintable(error));
    QFile existingAsset(oldImage);
    QVERIFY(existingAsset.open(QIODevice::ReadOnly));
    QCOMPARE(existingAsset.readAll(), imageSentinel);
    QCOMPARE(result.imageCount, 1);
    QVERIFY(QFileInfo::exists(QDir(result.assetsDirectory).filePath(QStringLiteral("image-0001.png"))));
    QVERIFY(readUtf8(output, &markdown));
    const QString resource = QFileInfo(result.assetsDirectory).fileName();
    QVERIFY(resource.startsWith(QStringLiteral("report_assets-")));
    QVERIFY(markdown.contains(QStringLiteral("![](<%1/image-0001.png>)").arg(resource)));
}

// 阅读器里 QPdfDocument（PDFium）与 MuPDF 静态链接在同一个进程：两家的
// 第三方库（尤其 lcms2）会重名，曾导致导出直接崩溃。这里先让 Qt PDF 加载并
// 渲染一页，再走一遍导出流程。
void MarkdownExport::coexistsWithQtPdf()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QPdfDocument doc;
    QCOMPARE(doc.load(testData(QStringLiteral("sample-rich.pdf"))),
             QPdfDocument::Error::None);
    QVERIFY(!doc.render(0, QSize(400, 600)).isNull());

    const QString output = dir.filePath(QStringLiteral("qtpdf.md"));

    Reader::MarkdownExportResult result;
    QString error;

    QVERIFY2(Reader::exportPdfToMarkdown(testData(QStringLiteral("sample-rich.pdf")), output,
                                         QString(), Reader::MarkdownProgressFn(), &result, &error),
             qPrintable(error));

    QVERIFY(QFileInfo::exists(output));
    QCOMPARE(result.pageCount, doc.pageCount());
}

QTEST_MAIN(MarkdownExport)
#include "markdown_export.moc"
