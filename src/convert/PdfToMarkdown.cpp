#include "PdfToMarkdown.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QVector>

#include <algorithm>
#include <cmath>

#if defined(_MSC_VER)
// MuPDF 头文件里的 setjmp / 未用参数告警与 /W4 冲突。
#pragma warning(push)
#pragma warning(disable : 4100 4611)
#endif
#include "mupdf/fitz.h"
#if defined(_MSC_VER)
#pragma warning(pop)
// fz_try/fz_catch 基于 setjmp/longjmp。本文件在 try 块内只放 C 指针，
// 不构造需要析构的 C++ 对象，因此这里的长跳转是安全的。
#pragma warning(disable : 4611)
#endif

namespace Reader {
namespace {

// ---------------------------------------------------------------------------
// 数据模型
// ---------------------------------------------------------------------------

// 一段同样式的文字（粗体 / 斜体）。
struct Run
{
    QString text;
    bool bold = false;
    bool italic = false;
};

// 结构化文本里的一行。
struct Line
{
    QVector<Run> runs;
    float size = 0;
    float y0 = 0;
    float y1 = 0;
    bool listStart = false;
    bool numbered = false;
    QString listMarker;
    QString plainText; // 去掉列表符号后的文本
    QString fullText;  // 原始文本（识别标题时用）
};

struct Paragraph
{
    QVector<Line> lines;
};

QString charToQString(int c)
{
    if (c <= 0 || c > 0x10FFFF)
        return QString();

    char buf[8];
    const int n = fz_runetochar(buf, c);
    return QString::fromUtf8(buf, n);
}

bool isCjkChar(QChar ch)
{
    const ushort u = ch.unicode();

    return (u >= 0x3000 && u <= 0x303F)     // CJK 标点
           || (u >= 0x3040 && u <= 0x30FF)  // 假名
           || (u >= 0x3400 && u <= 0x4DBF)  // 扩展 A
           || (u >= 0x4E00 && u <= 0x9FFF)  // 基本区
           || (u >= 0xAC00 && u <= 0xD7AF)  // 谚文
           || (u >= 0xF900 && u <= 0xFAFF)  // 兼容汉字
           || (u >= 0xFF00 && u <= 0xFFEF); // 全角
}

bool isCjkText(const QString& text)
{
    return !text.isEmpty() && isCjkChar(text.at(0));
}

QString normalizeEdgeLine(const QString& text)
{
    QString result;
    result.reserve(text.size());

    bool pendingSpace = false;

    for (const QChar ch : text) {
        if (ch.isSpace()) {
            pendingSpace = !result.isEmpty();
            continue;
        }

        if (pendingSpace) {
            result.append(QLatin1Char(' '));
            pendingSpace = false;
        }

        // 页码这类数字每页都不同，归一成 # 才能识别为重复行。
        result.append(ch.isDigit() ? QLatin1Char('#') : ch);
    }

    result = result.trimmed();

    if (result.size() > 80)
        result.truncate(80);

    return result;
}

// ---------------------------------------------------------------------------
// Markdown 转义
// ---------------------------------------------------------------------------

QString escapeInline(const QString& text)
{
    QString out;
    out.reserve(text.size() + 8);

    for (const QChar ch : text) {
        switch (ch.unicode()) {
        case '\\':
        case '`':
        case '*':
        case '_':
        case '[':
        case ']':
        case '<':
        case '>':
            out.append(QLatin1Char('\\'));
            break;
        default:
            break;
        }
        out.append(ch);
    }

    return out;
}

// 段落 / 标题开头的字符如果会被当成 Markdown 语法，就转义掉。
QString escapeLineStart(const QString& text)
{
    if (text.isEmpty())
        return text;

    const QChar first = text.at(0);

    bool escape = first == QLatin1Char('#') || first == QLatin1Char('>') ||
                  first == QLatin1Char('-') || first == QLatin1Char('+') ||
                  first == QLatin1Char('=');

    if (!escape && first.isDigit()) {
        // "1. xxx" 会被渲染成有序列表；这里不是列表，需要转义。
        int i = 0;
        while (i < text.size() && text.at(i).isDigit())
            ++i;
        if (i < text.size() && (text.at(i) == QLatin1Char('.') || text.at(i) == QLatin1Char(')'))) {
            escape = true;
        }
    }

    if (!escape)
        return text;

    return QLatin1Char('\\') + text;
}

// ---------------------------------------------------------------------------
// 行与段落
// ---------------------------------------------------------------------------

struct FontStyle
{
    bool bold = false;
    bool italic = false;
};

using FontStyleCache = QHash<const fz_font*, FontStyle>;

FontStyle fontStyleOf(fz_context* ctx, fz_font* font, FontStyleCache& cache)
{
    if (!font)
        return FontStyle();

    const auto cached = cache.constFind(font);
    if (cached != cache.constEnd())
        return cached.value();

    FontStyle style;

    if (const fz_font_flags_t* flags = fz_font_flags(font)) {
        style.bold = flags->is_bold || flags->fake_bold;
        style.italic = flags->is_italic || flags->fake_italic;
    }

    // 有些 PDF（尤其是 HTML 渲染生成的）只改了字体的样式名，
    // is_bold / is_italic 标记并不可靠，这里再看一眼字体名。
    const QString name = QString::fromLatin1(fz_font_name(ctx, font)).toLower();

    if (name.contains(QStringLiteral("bold")) || name.contains(QStringLiteral("black")) ||
        name.contains(QStringLiteral("heavy")))
        style.bold = true;

    if (name.contains(QStringLiteral("italic")) || name.contains(QStringLiteral("oblique")))
        style.italic = true;

    cache.insert(font, style);
    return style;
}

void appendRun(QVector<Run>& runs, const Run& run)
{
    if (run.text.isEmpty())
        return;

    if (!runs.isEmpty() && runs.last().bold == run.bold && runs.last().italic == run.italic) {
        runs.last().text += run.text;
        return;
    }

    runs.append(run);
}

int leadingSpaceCount(const QString& text)
{
    int i = 0;
    while (i < text.size() && text.at(i).isSpace())
        ++i;
    return i;
}

int trailingSpaceCount(const QString& text)
{
    int i = static_cast<int>(text.size());
    while (i > 0 && text.at(i - 1).isSpace())
        --i;
    return text.size() - i;
}

void trimRuns(QVector<Run>& runs)
{
    while (!runs.isEmpty()) {
        Run& first = runs.first();
        const int spaces = leadingSpaceCount(first.text);
        if (spaces == 0)
            break;
        first.text.remove(0, spaces);
        if (first.text.isEmpty())
            runs.removeFirst();
    }

    while (!runs.isEmpty()) {
        Run& last = runs.last();
        const int spaces = trailingSpaceCount(last.text);
        if (spaces == 0)
            break;
        last.text.chop(spaces);
        if (last.text.isEmpty())
            runs.removeLast();
    }
}

QString plainTextOf(const QVector<Run>& runs)
{
    QString text;
    for (const Run& run : runs)
        text += run.text;
    return text;
}

QString renderRuns(const QVector<Run>& runs)
{
    QString out;

    for (const Run& run : runs) {
        const int leading = leadingSpaceCount(run.text);
        const int trailing = trailingSpaceCount(run.text);
        const int coreLength = static_cast<int>(run.text.size()) - leading - trailing;

        out += run.text.left(leading);

        if (coreLength <= 0) {
            out += run.text.mid(leading);
            continue;
        }

        const QString escaped = escapeInline(run.text.mid(leading, coreLength));

        if (run.bold && run.italic)
            out += QStringLiteral("***") + escaped + QStringLiteral("***");
        else if (run.bold)
            out += QStringLiteral("**") + escaped + QStringLiteral("**");
        else if (run.italic)
            out += QLatin1Char('*') + escaped + QLatin1Char('*');
        else
            out += escaped;

        out += run.text.mid(leading + coreLength);
    }

    return out;
}

// 去掉行首的列表符号（项目符号或编号），返回是否为列表项。
bool detectListMarker(QString& text, QString* marker, bool* numbered)
{
    static const QString bulletChars = QStringLiteral("•·▪▫◦‣∙●○■□◆◇❖➢➤➣");

    int i = 0;
    while (i < text.size() && text.at(i).isSpace())
        ++i;

    if (i >= text.size())
        return false;

    const QChar first = text.at(i);

    if (bulletChars.contains(first) || ((first == QLatin1Char('-') || first == QChar(0x2013) ||
                                         first == QChar(0x2014) || first == QChar(0x2212)) &&
                                        i + 1 < text.size() && text.at(i + 1).isSpace())) {
        int end = i + 1;
        while (end < text.size() && text.at(end).isSpace())
            ++end;
        text.remove(0, end);
        *marker = QStringLiteral("-");
        *numbered = false;
        return true;
    }

    // 1. / 1) / 1、
    int j = i;
    while (j < text.size() && text.at(j).isDigit())
        ++j;

    if (j > i && j < text.size() &&
        (text.at(j) == QLatin1Char('.') || text.at(j) == QLatin1Char(')') ||
         text.at(j) == QChar(0x3001))) {
        int end = j + 1;
        while (end < text.size() && text.at(end).isSpace())
            ++end;
        const QString number = text.mid(i, j - i);
        text.remove(0, end);
        *marker = number + QLatin1Char('.');
        *numbered = true;
        return true;
    }

    // 一、/ （一）
    const QStringList cnNumerals = {
        QStringLiteral("一"), QStringLiteral("二"), QStringLiteral("三"), QStringLiteral("四"),
        QStringLiteral("五"), QStringLiteral("六"), QStringLiteral("七"), QStringLiteral("八"),
        QStringLiteral("九"), QStringLiteral("十")};

    for (const QString& numeral : cnNumerals) {
        if (!text.mid(i, numeral.size()).startsWith(numeral))
            continue;

        int end = i + static_cast<int>(numeral.size());
        if (end < text.size() &&
            (text.at(end) == QChar(0x3001) || text.at(end) == QLatin1Char('.'))) {
            ++end;
            while (end < text.size() && text.at(end).isSpace())
                ++end;
            const QString numeralMarker = numeral + QStringLiteral("、");
            text.remove(0, end);
            *marker = numeralMarker;
            *numbered = true;
            return true;
        }
    }

    return false;
}

// 把一行结构化文本转成 Line。返回 false 表示空行。
bool buildLine(fz_context* ctx, const fz_stext_line* line, FontStyleCache& cache, Line* out)
{
    QVector<Run> runs;
    Run current;
    float maxSize = 0;

    for (const fz_stext_char* ch = line->first_char; ch; ch = ch->next) {
        const QString text = charToQString(ch->c);
        if (text.isEmpty())
            continue;

        const FontStyle style = fontStyleOf(ctx, ch->font, cache);
        const bool bold = (ch->flags & FZ_STEXT_BOLD) != 0 || style.bold;
        const bool italic = style.italic;

        maxSize = std::max(maxSize, ch->size);

        if (current.text.isEmpty() || (current.bold == bold && current.italic == italic)) {
            // 合并同样式字符。
            if (current.text.isEmpty()) {
                current.bold = bold;
                current.italic = italic;
            }
            current.text += text;
            continue;
        }

        appendRun(runs, current);
        current = Run{text, bold, italic};
    }

    appendRun(runs, current);
    trimRuns(runs);

    if (runs.isEmpty())
        return false;

    out->runs = runs;
    out->size = maxSize;
    out->y0 = line->bbox.y0;
    out->y1 = line->bbox.y1;
    out->plainText = plainTextOf(runs);
    out->fullText = out->plainText;

    QString marker;
    bool numbered = false;

    QString stripped = out->plainText;
    if (detectListMarker(stripped, &marker, &numbered)) {
        // 从 runs 里移除与标记等长的前缀。
        int remaining = static_cast<int>(out->plainText.size() - stripped.size());
        while (remaining > 0 && !out->runs.isEmpty()) {
            Run& first = out->runs.first();
            const int take = std::min(remaining, static_cast<int>(first.text.size()));
            first.text.remove(0, take);
            remaining -= take;
            if (first.text.isEmpty())
                out->runs.removeFirst();
        }
        trimRuns(out->runs);
        out->plainText = plainTextOf(out->runs);
        out->listStart = true;
        out->numbered = numbered;
        out->listMarker = marker;
    }

    return !out->plainText.isEmpty();
}

void mergeLineIntoParagraph(QVector<Run>& target, const Line& line)
{
    const QString lastText = target.isEmpty() ? QString() : target.last().text;
    const QString nextText = line.runs.isEmpty() ? QString() : line.runs.first().text;

    bool separator = true;

    if (lastText.endsWith(QChar(0x00AD))) {
        // 软连字符（PDF 断词），直接拼接。
        target.last().text.chop(1);
        separator = false;
    } else if (isCjkText(lastText.right(1)) && isCjkText(nextText)) {
        separator = false;
    } else if (lastText.endsWith(QLatin1Char(' ')) || nextText.startsWith(QLatin1Char(' '))) {
        separator = false;
    }

    if (separator) {
        Run space;
        space.text = QStringLiteral(" ");
        if (!target.isEmpty()) {
            space.bold = target.last().bold;
            space.italic = target.last().italic;
        }
        appendRun(target, space);
    }

    for (const Run& run : line.runs)
        appendRun(target, run);
}

// ---------------------------------------------------------------------------
// 转换器
// ---------------------------------------------------------------------------

class MarkdownExporter
{
public:
    MarkdownExporter(QString pdfPath, QString markdownPath, QString password,
                     MarkdownProgressFn progress)
        : m_pdfPath(std::move(pdfPath)), m_markdownPath(std::move(markdownPath)),
          m_password(std::move(password)), m_progress(std::move(progress))
    {
    }

    ~MarkdownExporter()
    {
        closeContext();
    }

    bool run(MarkdownExportResult* result, QString* error);

private:
    bool openContext(fz_context** ctx, fz_document** doc, QString* error);
    void closeContext();

    bool loadStructuredPage(fz_context* ctx, fz_document* doc, int pageNo, int flags,
                            fz_page** page, fz_stext_page** stext, QString* error);

    bool analyzePage(fz_context* ctx, fz_document* doc, int pageNo, QString* error);
    bool renderPage(fz_context* ctx, fz_document* doc, int pageNo, QString* pageMarkdown,
                    QString* error);

    float bodySize() const;
    int headingLevel(float size) const;
    bool isRepeatedEdgeLine(const QString& text) const;

    bool emitProgress(int done, int total, QString* error);
    bool exportImage(fz_context* ctx, fz_image* image, QString* markdown, QString* error);
    bool createAssetsDirectory(QString* error);
    void cleanupAssets();

    QString m_pdfPath;
    QString m_markdownPath;
    QString m_password;
    MarkdownProgressFn m_progress;

    fz_context* m_ctx = nullptr;
    fz_document* m_doc = nullptr;

    QMap<int, int> m_sizeHistogram;
    QHash<QString, int> m_edgeLineCounts;
    QSet<QString> m_repeatedEdgeLines;
    int m_pageCount = 0;

    QString m_assetsDir;
    QString m_assetsRelative;
    int m_imageIndex = 0;
    QVector<QString> m_createdAssets;
    bool m_cancelled = false;
};

bool MarkdownExporter::openContext(fz_context** ctx, fz_document** doc, QString* error)
{
    fz_context* context = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!context) {
        *error = QStringLiteral("无法创建 MuPDF 上下文");
        return false;
    }

    fz_document* document = nullptr;
    *ctx = context;

    const QByteArray filename = m_pdfPath.toUtf8();
    const QByteArray password = m_password.toUtf8();
    fz_var(document);
    fz_try(context)
    {
        fz_register_document_handlers(context);
        document = fz_open_document(context, filename.constData());
        if ((fz_needs_password(context, document) || !password.isEmpty()) &&
            !fz_authenticate_password(context, document, password.constData())) {
            fz_throw(context, FZ_ERROR_ARGUMENT, "PDF 已加密，需要正确的密码才能导出");
        }
        if (!fz_has_permission(context, document, FZ_PERMISSION_COPY))
            fz_throw(context, FZ_ERROR_ARGUMENT, "文档限制了文本复制，无法导出");
    }
    fz_catch(context)
    {
        *error = QString::fromUtf8(fz_caught_message(context));
        fz_drop_document(context, document);
        fz_drop_context(context);
        *ctx = nullptr;
        return false;
    }

    *doc = document;
    return true;
}

void MarkdownExporter::closeContext()
{
    if (m_doc) {
        fz_drop_document(m_ctx, m_doc);
        m_doc = nullptr;
    }

    if (m_ctx) {
        fz_drop_context(m_ctx);
        m_ctx = nullptr;
    }
}

bool MarkdownExporter::loadStructuredPage(fz_context* ctx, fz_document* doc, int pageNo, int flags,
                                          fz_page** page, fz_stext_page** stext, QString* error)
{
    fz_stext_options options;
    options.flags = flags;
    options.scale = 0;
    options.clip = fz_empty_rect;

    fz_page* pg = nullptr;
    fz_stext_page* st = nullptr;

    fz_var(pg);
    fz_var(st);

    fz_try(ctx)
    {
        pg = fz_load_page(ctx, doc, pageNo);
        st = fz_new_stext_page_from_page(ctx, pg, &options);
    }
    fz_catch(ctx)
    {
        *error = QStringLiteral("第 %1 页解析失败：%2")
                     .arg(pageNo + 1)
                     .arg(QString::fromUtf8(fz_caught_message(ctx)));
        fz_drop_stext_page(ctx, st);
        fz_drop_page(ctx, pg);
        return false;
    }

    *page = pg;
    *stext = st;
    return true;
}

bool MarkdownExporter::analyzePage(fz_context* ctx, fz_document* doc, int pageNo, QString* error)
{
    fz_page* page = nullptr;
    fz_stext_page* stext = nullptr;

    if (!loadStructuredPage(ctx, doc, pageNo, FZ_STEXT_DEHYPHENATE | FZ_STEXT_CLIP, &page, &stext,
                            error)) {
        return false;
    }

    const float height = stext->mediabox.y1 - stext->mediabox.y0;

    FontStyleCache styleCache;

    for (const fz_stext_block* block = stext->first_block; block; block = block->next) {
        if (block->type != FZ_STEXT_BLOCK_TEXT)
            continue;

        for (const fz_stext_line* line = block->u.t.first_line; line; line = line->next) {
            Line built;
            if (!buildLine(ctx, line, styleCache, &built))
                continue;

            const int bin = static_cast<int>(std::lround(built.size * 4.0f));
            m_sizeHistogram[bin] += built.plainText.size();

            if (height <= 0)
                continue;

            const float center = (built.y0 + built.y1) * 0.5f;
            const bool inEdge = center <= height * 0.08f || center >= height * 0.92f;

            if (inEdge) {
                const QString normalized = normalizeEdgeLine(built.plainText);
                if (!normalized.isEmpty() && normalized.size() <= 60)
                    ++m_edgeLineCounts[normalized];
            }
        }
    }

    fz_drop_stext_page(ctx, stext);
    fz_drop_page(ctx, page);

    return true;
}

bool MarkdownExporter::isRepeatedEdgeLine(const QString& text) const
{
    if (m_repeatedEdgeLines.isEmpty())
        return false;

    return m_repeatedEdgeLines.contains(normalizeEdgeLine(text));
}

float MarkdownExporter::bodySize() const
{
    int bestBin = 0;
    int bestCount = 0;

    for (auto it = m_sizeHistogram.cbegin(); it != m_sizeHistogram.cend(); ++it) {
        if (it.value() > bestCount) {
            bestCount = it.value();
            bestBin = it.key();
        }
    }

    return bestBin > 0 ? bestBin / 4.0f : 0.0f;
}

int MarkdownExporter::headingLevel(float size) const
{
    const float body = bodySize();

    if (body <= 0)
        return 0;

    const float ratio = size / body;

    if (ratio >= 1.9f)
        return 1;
    if (ratio >= 1.35f)
        return 2;
    if (ratio >= 1.15f)
        return 3;

    return 0;
}

bool MarkdownExporter::emitProgress(int done, int total, QString* error)
{
    if (m_cancelled)
        return false;

    if (m_progress && !m_progress(done, total)) {
        m_cancelled = true;
        *error = QStringLiteral("已取消导出");
        return false;
    }

    return true;
}

bool MarkdownExporter::createAssetsDirectory(QString* error)
{
    const QFileInfo output(m_markdownPath);
    const QDir parent = output.absoluteDir();
    const QString base = output.completeBaseName() + QStringLiteral("_assets");
    const QString preferred = parent.filePath(base);
    if (parent.mkdir(base)) {
        m_assetsDir = preferred;
        m_assetsRelative = base;
        return true;
    }

    const QFileInfo occupied(preferred);
    if (occupied.exists() || occupied.isSymLink()) {
        // Another export or an earlier Markdown file may own the preferred
        // directory. Never write into it, even when the user replaces the .md.
        QTemporaryDir fresh(parent.filePath(base + QStringLiteral("-XXXXXX")));
        if (fresh.isValid()) {
            fresh.setAutoRemove(false);
            m_assetsDir = fresh.path();
            m_assetsRelative = QFileInfo(m_assetsDir).fileName();
            return true;
        }
    }
    *error = QStringLiteral("无法在输出位置创建图片目录：%1").arg(parent.absolutePath());
    return false;
}

bool MarkdownExporter::exportImage(fz_context* ctx, fz_image* image, QString* markdown,
                                   QString* error)
{
    fz_pixmap* pixmap = nullptr;
    int width = 0;
    int height = 0;

    fz_var(pixmap);

    fz_try(ctx)
    {
        pixmap = fz_get_pixmap_from_image(ctx, image, nullptr, nullptr, &width, &height);
    }
    fz_catch(ctx)
    {
        // 单个图片解不出来时不影响整体导出。
        fz_drop_pixmap(ctx, pixmap);
        return true;
    }

    // 跳过装饰性的小图标。
    if (!pixmap || width < 24 || height < 24) {
        fz_drop_pixmap(ctx, pixmap);
        return true;
    }

    if (m_assetsDir.isEmpty() && !createAssetsDirectory(error)) {
        fz_drop_pixmap(ctx, pixmap);
        return false;
    }
    const QString actualName =
        QStringLiteral("image-%1.png").arg(m_imageIndex + 1, 4, 10, QLatin1Char('0'));
    const QString absolutePath = QDir(m_assetsDir).filePath(actualName);

    const QByteArray utf8Path = absolutePath.toUtf8();

    fz_try(ctx)
    {
        fz_save_pixmap_as_png(ctx, pixmap, utf8Path.constData());
    }
    fz_catch(ctx)
    {
        *error = QStringLiteral("图片写入失败：%1").arg(absolutePath);
        QFile::remove(absolutePath);
        fz_drop_pixmap(ctx, pixmap);
        return false;
    }

    fz_drop_pixmap(ctx, pixmap);

    ++m_imageIndex;
    m_createdAssets.append(absolutePath);

    QString relative = m_assetsRelative + QStringLiteral("/") + actualName;
    relative.replace(QLatin1Char('\\'), QLatin1Char('/'));

    *markdown = QStringLiteral("![](") + QLatin1Char('<') + relative + QLatin1Char('>') +
                QStringLiteral(")\n\n");
    return true;
}

void MarkdownExporter::cleanupAssets()
{
    if (m_assetsDir.isEmpty())
        return;

    for (const QString& path : m_createdAssets)
        QFile::remove(path);

    QDir().rmdir(m_assetsDir);
}

bool MarkdownExporter::renderPage(fz_context* ctx, fz_document* doc, int pageNo,
                                  QString* pageMarkdown, QString* error)
{
    fz_page* page = nullptr;
    fz_stext_page* stext = nullptr;

    if (!loadStructuredPage(ctx, doc, pageNo,
                            FZ_STEXT_DEHYPHENATE | FZ_STEXT_CLIP | FZ_STEXT_PRESERVE_IMAGES, &page,
                            &stext, error)) {
        return false;
    }

    const float height = stext->mediabox.y1 - stext->mediabox.y0;

    QString markdown;
    FontStyleCache styleCache;
    bool lastWasList = false;

    auto startNonListBlock = [&markdown, &lastWasList]() {
        if (lastWasList) {
            markdown += QLatin1Char('\n');
            lastWasList = false;
        }
    };

    auto lineIsFiltered = [this, height](const Line& line) {
        if (m_repeatedEdgeLines.isEmpty() || height <= 0)
            return false;
        const float center = (line.y0 + line.y1) * 0.5f;
        const bool inEdge = center <= height * 0.08f || center >= height * 0.92f;
        return inEdge && isRepeatedEdgeLine(line.plainText);
    };

    for (const fz_stext_block* block = stext->first_block; block; block = block->next) {
        if (block->type == FZ_STEXT_BLOCK_IMAGE) {
            QString imageMarkdown;
            if (!exportImage(ctx, block->u.i.image, &imageMarkdown, error)) {
                fz_drop_stext_page(ctx, stext);
                fz_drop_page(ctx, page);
                return false;
            }
            startNonListBlock();
            markdown += imageMarkdown;
            continue;
        }

        if (block->type != FZ_STEXT_BLOCK_TEXT)
            continue;

        QVector<Paragraph> paragraphs;
        Paragraph current;

        auto flushParagraph = [&paragraphs, &current]() {
            if (!current.lines.isEmpty()) {
                paragraphs.append(current);
                current = Paragraph();
            }
        };

        for (const fz_stext_line* line = block->u.t.first_line; line; line = line->next) {
            Line built;
            if (!buildLine(ctx, line, styleCache, &built))
                continue;

            if (lineIsFiltered(built))
                continue;

            const bool startNew = current.lines.isEmpty() || built.listStart ||
                                  built.y0 - current.lines.last().y1 >
                                      std::max(current.lines.last().size * 0.8f, 3.0f) ||
                                  std::abs(built.size - current.lines.last().size) >
                                      current.lines.last().size * 0.25f;

            if (startNew)
                flushParagraph();

            current.lines.append(built);
        }

        flushParagraph();

        for (const Paragraph& paragraph : paragraphs) {
            QVector<Run> merged;
            for (const Line& line : paragraph.lines)
                mergeLineIntoParagraph(merged, line);

            if (merged.isEmpty())
                continue;

            const QString plain = plainTextOf(merged).trimmed();
            if (plain.isEmpty())
                continue;

            const Line& firstLine = paragraph.lines.first();
            const int level = headingLevel(firstLine.size);
            const QString headingText = firstLine.fullText.trimmed();

            if (level > 0 && headingText.size() <= 200 && paragraph.lines.size() <= 4) {
                startNonListBlock();
                markdown += QString(level, QLatin1Char('#')) + QLatin1Char(' ') +
                            escapeInline(headingText) + QStringLiteral("\n\n");
                continue;
            }

            if (firstLine.listStart) {
                QString body = renderRuns(merged).trimmed();
                if (firstLine.numbered && !firstLine.listMarker.isEmpty())
                    markdown += firstLine.listMarker + QLatin1Char(' ') + body + QLatin1Char('\n');
                else
                    markdown += QStringLiteral("- ") + body + QLatin1Char('\n');
                lastWasList = true;
                continue;
            }

            startNonListBlock();
            markdown += escapeLineStart(renderRuns(merged).trimmed()) + QStringLiteral("\n\n");
        }
    }

    fz_drop_stext_page(ctx, stext);
    fz_drop_page(ctx, page);

    *pageMarkdown = markdown;
    return true;
}

bool MarkdownExporter::run(MarkdownExportResult* result, QString* error)
{
    error->clear();

    if (!openContext(&m_ctx, &m_doc, error))
        return false;

    int pages = 0;

    fz_try(m_ctx)
    {
        pages = fz_count_pages(m_ctx, m_doc);
    }
    fz_catch(m_ctx)
    {
        *error = QString::fromUtf8(fz_caught_message(m_ctx));
        return false;
    }

    if (pages <= 0) {
        *error = QStringLiteral("PDF 没有可转换的页面");
        return false;
    }

    m_pageCount = pages;
    const int totalSteps = pages * 2;

    // 第一遍：统计正文字号分布、页眉页脚重复行。
    for (int i = 0; i < pages; ++i) {
        if (!analyzePage(m_ctx, m_doc, i, error))
            return false;
        if (!emitProgress(i + 1, totalSteps, error))
            return false;
    }

    // 在大多数页面的页眉 / 页脚区域重复出现的行（书名、页码等）视为装饰，
    // 导出时丢弃。
    if (pages >= 2) {
        const int threshold = std::max(2, (pages * 3 + 4) / 5);
        for (auto it = m_edgeLineCounts.cbegin(); it != m_edgeLineCounts.cend(); ++it) {
            if (it.value() >= threshold)
                m_repeatedEdgeLines.insert(it.key());
        }
    }

    // 第二遍：生成 Markdown。
    QByteArray utf8;

    for (int i = 0; i < pages; ++i) {
        QString pageMarkdown;
        if (!renderPage(m_ctx, m_doc, i, &pageMarkdown, error)) {
            cleanupAssets();
            return false;
        }

        if (!pageMarkdown.trimmed().isEmpty()) {
            if (!utf8.isEmpty())
                utf8.append("\n");
            utf8.append(pageMarkdown.toUtf8());
        }

        if (!emitProgress(pages + i + 1, totalSteps, error)) {
            cleanupAssets();
            return false;
        }
    }

    while (utf8.endsWith('\n'))
        utf8.chop(1);
    utf8.append('\n');

    QSaveFile file(m_markdownPath);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("无法写入：%1\n%2").arg(m_markdownPath, file.errorString());
        cleanupAssets();
        return false;
    }

    if (file.write(utf8) != utf8.size() || !file.commit()) {
        *error = QStringLiteral("保存失败：%1").arg(file.errorString());
        cleanupAssets();
        return false;
    }

    const int images = m_imageIndex;

    if (result) {
        result->pageCount = pages;
        result->imageCount = images;
        result->assetsDirectory = m_imageIndex > 0 ? m_assetsDir : QString();
    }

    return true;
}

} // namespace

bool exportPdfToMarkdown(const QString& pdfPath, const QString& markdownPath,
                         const QString& password, const MarkdownProgressFn& progress,
                         MarkdownExportResult* result, QString* error)
{
    QString dummy;
    if (!error)
        error = &dummy;

    error->clear();

    const QFileInfo pdfInfo(pdfPath);
    if (!pdfInfo.exists() || !pdfInfo.isFile() || !pdfInfo.isReadable()) {
        *error = QStringLiteral("文件不存在或不可读取：%1").arg(pdfPath);
        return false;
    }

    if (markdownPath.trimmed().isEmpty()) {
        *error = QStringLiteral("未指定输出文件");
        return false;
    }

    const QFileInfo outputInfo(markdownPath);
#ifdef Q_OS_WIN
    constexpr auto caseSensitivity = Qt::CaseInsensitive;
#else
    constexpr auto caseSensitivity = Qt::CaseSensitive;
#endif
    if (pdfInfo.absoluteFilePath().compare(outputInfo.absoluteFilePath(), caseSensitivity) == 0 ||
        (!pdfInfo.canonicalFilePath().isEmpty() &&
         pdfInfo.canonicalFilePath().compare(outputInfo.canonicalFilePath(), caseSensitivity) == 0)) {
        *error = QStringLiteral("输出文件不能覆盖原始 PDF 文件");
        return false;
    }

    MarkdownExporter exporter(pdfPath, markdownPath, password, progress);
    return exporter.run(result, error);
}

} // namespace Reader
