#include "PdfDocumentOperations.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QPainter>
#include <QPrinter>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100 4611)
#endif
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#ifdef _MSC_VER
#pragma warning(pop)
#pragma warning(disable : 4611) // MuPDF wrappers keep C++ objects outside fz_try.
#endif

namespace Reader {
namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("PdfDocumentOperations", text);
}

// Full rewrites invalidate signatures. Include unsigned signature fields so the
// review workflow also stays clear of documents prepared for signing.
bool hasSignatureField(fz_context* ctx, pdf_obj* fields, int depth = 0)
{
    if (depth > 64)
        return true;
    for (int i = 0; i < pdf_array_len(ctx, fields); ++i) {
        pdf_obj* field = pdf_array_get(ctx, fields, i);
        if (pdf_name_eq(ctx, pdf_dict_get(ctx, field, PDF_NAME(FT)), PDF_NAME(Sig)) ||
            hasSignatureField(ctx, pdf_dict_get(ctx, field, PDF_NAME(Kids)), depth + 1)) {
            return true;
        }
    }
    return false;
}

void writePdf(fz_context* ctx, void* state, const void* bytes, size_t size)
{
    auto* output = static_cast<QSaveFile*>(state);
    if (output->write(static_cast<const char*>(bytes), static_cast<qint64>(size)) !=
        static_cast<qint64>(size)) {
        fz_throw(ctx, FZ_ERROR_SYSTEM, "Writing the PDF failed");
    }
}

int64_t tellPdf(fz_context*, void* state)
{
    return static_cast<QSaveFile*>(state)->pos();
}

bool sameFilePath(const QFileInfo& source, const QFileInfo& destination)
{
#ifdef Q_OS_WIN
    constexpr auto caseSensitivity = Qt::CaseInsensitive;
#else
    constexpr auto caseSensitivity = Qt::CaseSensitive;
#endif
    const QString canonical = source.canonicalFilePath();
    return source.absoluteFilePath().compare(destination.absoluteFilePath(), caseSensitivity) == 0
        || (!canonical.isEmpty()
            && canonical.compare(destination.canonicalFilePath(), caseSensitivity) == 0);
}

}

PdfDocumentOperations::~PdfDocumentOperations()
{
    if (m_context) {
        pdf_drop_document(m_context, m_document);
        fz_drop_context(m_context);
    }
}

bool PdfDocumentOperations::open(const QString& path, const QString& password, QString& error)
{
    error.clear();
    if (m_context) {
        error = tr("请为新文件创建独立的文档会话。");
        return false;
    }
    m_context = fz_new_context(nullptr, nullptr, 64 * 1024 * 1024);
    if (!m_context) {
        error = tr("无法初始化 PDF 处理引擎。");
        return false;
    }
    const QByteArray filename = path.toUtf8();
    const QByteArray passwordBytes = password.toUtf8();
    fz_try(m_context)
    {
        m_document = pdf_open_document(m_context, filename.constData());
        if ((fz_needs_password(m_context, &m_document->super) || !passwordBytes.isEmpty()) &&
            !fz_authenticate_password(m_context, &m_document->super, passwordBytes.constData())) {
            fz_throw(m_context, FZ_ERROR_ARGUMENT, "PDF 密码不正确。");
        }
        m_pageCount = pdf_count_pages(m_context, m_document);
        m_canPrint = pdf_has_permission(m_context, m_document, FZ_PERMISSION_PRINT);
        m_highQualityPrint = pdf_has_permission(m_context, m_document, FZ_PERMISSION_PRINT_HQ);
        m_canCopy = pdf_has_permission(m_context, m_document, FZ_PERMISSION_COPY);
        pdf_obj* catalog = pdf_dict_get(m_context, pdf_trailer(m_context, m_document), PDF_NAME(Root));
        m_canAnnotate = pdf_has_permission(m_context, m_document, FZ_PERMISSION_ANNOTATE)
            && !pdf_dict_get(m_context, catalog, PDF_NAME(Perms))
            && pdf_to_int(m_context, pdf_dict_getp(m_context, catalog, "AcroForm/SigFlags")) == 0
            && !hasSignatureField(m_context, pdf_dict_getp(m_context, catalog, "AcroForm/Fields"));
    }
    fz_catch(m_context)
    {
        error = QString::fromUtf8(fz_caught_message(m_context));
        m_pageCount = 0;
        m_canPrint = m_canCopy = m_canAnnotate = false;
        return false;
    }
    m_path = QFileInfo(path).absoluteFilePath();
    if (m_pageCount == 0) {
        error = tr("PDF 没有可处理的页面。");
        return false;
    }
    return true;
}

QString PdfDocumentOperations::pageText(int page, QString& error) const
{
    error.clear();
    if (!m_canCopy || page < 0 || page >= m_pageCount) {
        error = tr("当前页不可复制或文档限制了文本复制。");
        return {};
    }
    fz_buffer* buffer = nullptr;
    unsigned char* bytes = nullptr;
    size_t length = 0;
    fz_var(buffer);
    fz_try(m_context)
    {
        buffer = fz_new_buffer_from_page_number(m_context, &m_document->super, page, nullptr);
        length = fz_buffer_storage(m_context, buffer, &bytes);
    }
    fz_catch(m_context)
    {
        fz_drop_buffer(m_context, buffer);
        error = QString::fromUtf8(fz_caught_message(m_context));
        return {};
    }
    const QString result = QString::fromUtf8(reinterpret_cast<const char*>(bytes),
                                               static_cast<qsizetype>(length));
    fz_drop_buffer(m_context, buffer);
    return result;
}

QImage PdfDocumentOperations::rasterize(int pageNumber, const QSize& bounds, int maxDpi,
                                        bool forPrint, QString& error) const
{
    error.clear();
    if (pageNumber < 0 || pageNumber >= m_pageCount || bounds.isEmpty()) {
        error = tr("页面或渲染尺寸无效。");
        return {};
    }

    pdf_page* page = nullptr;
    fz_pixmap* pixmap = nullptr;
    fz_var(page);
    fz_var(pixmap);
    fz_try(m_context)
    {
        page = pdf_load_page(m_context, m_document, pageNumber);
        const fz_rect rect = pdf_bound_page(m_context, page, FZ_CROP_BOX);
        const double width = static_cast<double>(rect.x1) - rect.x0;
        const double height = static_cast<double>(rect.y1) - rect.y0;
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
            fz_throw(m_context, FZ_ERROR_FORMAT, "Invalid PDF page dimensions");

        constexpr double maxPixels = 12'000'000;
        const double scale = std::min({bounds.width() / width, bounds.height() / height,
                                       maxDpi / 72.0, std::sqrt(maxPixels / width / height)});
        const fz_matrix matrix = fz_scale(static_cast<float>(scale), static_cast<float>(scale));
        pixmap = pdf_new_pixmap_from_page_with_usage(
            m_context, page, matrix, fz_device_rgb(m_context), 0,
            forPrint ? "Print" : "View", FZ_CROP_BOX);
    }
    fz_always(m_context)
    {
        pdf_drop_page(m_context, page);
    }
    fz_catch(m_context)
    {
        error = QString::fromUtf8(fz_caught_message(m_context));
        return {};
    }

    QImage copy;
    {
        const QImage image(fz_pixmap_samples(m_context, pixmap),
                           fz_pixmap_width(m_context, pixmap), fz_pixmap_height(m_context, pixmap),
                           fz_pixmap_stride(m_context, pixmap), QImage::Format_RGB888);
        copy = image.copy();
    }
    fz_drop_pixmap(m_context, pixmap);
    if (copy.isNull())
        error = tr("无法为页面分配图像。");
    return copy;
}

QImage PdfDocumentOperations::preview(int page, const QSize& bounds, QString& error) const
{
    return rasterize(page, bounds, 144, false, error);
}

bool PdfDocumentOperations::printPage(QPrinter& printer, QPainter& painter, int page,
                                      QString& error) const
{
    if (!m_canPrint) {
        error = tr("文档禁止打印。");
        return false;
    }
    const QRectF printable(QPointF(0, 0), QSizeF(printer.pageLayout()
        .paintRectPixels(printer.resolution()).size()));
    const int maxDpi = m_highQualityPrint ? 300 : 150;
    const QImage image = rasterize(page, printable.size().toSize(), maxDpi, true, error);
    if (image.isNull())
        return false;
    const QSizeF fitted = QSizeF(image.size()).scaled(printable.size(), Qt::KeepAspectRatio);
    const QRectF target(printable.center() - QPointF(fitted.width(), fitted.height()) / 2, fitted);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::LosslessImageRendering);
    painter.drawImage(target, image);
    return true;
}

QList<PdfNote> PdfDocumentOperations::pageNotes(int pageNumber, QString& error) const
{
    error.clear();
    if (pageNumber < 0 || pageNumber >= m_pageCount) {
        error = tr("页码超出文档。");
        return {};
    }

    pdf_page* page = nullptr;
    pdf_annot* annotation = nullptr;
    fz_var(page);
    fz_try(m_context)
    {
        page = pdf_load_page(m_context, m_document, pageNumber);
        annotation = pdf_first_annot(m_context, page);
    }
    fz_catch(m_context)
    {
        pdf_drop_page(m_context, page);
        error = QString::fromUtf8(fz_caught_message(m_context));
        return {};
    }

    QList<PdfNote> notes;
    while (annotation) {
        const char* text = nullptr;
        const char* author = nullptr;
        fz_try(m_context)
        {
            const int flags = pdf_annot_flags(m_context, annotation);
            constexpr int hidden = PDF_ANNOT_IS_INVISIBLE | PDF_ANNOT_IS_HIDDEN | PDF_ANNOT_IS_NO_VIEW;
            if (!(flags & hidden) && pdf_annot_type(m_context, annotation) == PDF_ANNOT_TEXT) {
                text = pdf_annot_contents(m_context, annotation);
                author = pdf_annot_author(m_context, annotation);
            }
            annotation = pdf_next_annot(m_context, annotation);
        }
        fz_catch(m_context)
        {
            pdf_drop_page(m_context, page);
            error = QString::fromUtf8(fz_caught_message(m_context));
            return {};
        }
        if (text && *text)
            notes.append({QString::fromUtf8(author), QString::fromUtf8(text)});
    }

    pdf_drop_page(m_context, page);
    return notes;
}

bool PdfDocumentOperations::saveNoteCopy(int pageNumber, QPointF position, const QString& author,
                                          const QString& text, const QString& destination,
                                          QString& error)
{
    error.clear();
    if (!m_canAnnotate || pageNumber < 0 || pageNumber >= m_pageCount) {
        error = tr("当前页不可批注，或文档包含签名字段、认证权限或批注限制。");
        return false;
    }
    if (sameFilePath(QFileInfo(m_path), QFileInfo(destination))) {
        error = tr("请选择与源 PDF 不同的文件名。");
        return false;
    }
    if (!std::isfinite(position.x()) || !std::isfinite(position.y()) ||
        position.x() < 0 || position.x() > 1 || position.y() < 0 || position.y() > 1 ||
        text.trimmed().isEmpty() || text.contains(QChar::Null) || author.contains(QChar::Null)) {
        error = tr("请指定有效的批注位置与内容。");
        return false;
    }

    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)) {
        error = output.errorString();
        return false;
    }
    const QByteArray utf8Text = text.toUtf8();
    const QByteArray utf8Author = author.toUtf8();
    const qint64 timestamp = QDateTime::currentSecsSinceEpoch();

    // Remove the temporary edit after writing so a retry cannot duplicate it.
    pdf_page* page = nullptr;
    pdf_annot* note = nullptr;
    fz_output* pdfOutput = nullptr;
    fz_var(page);
    fz_var(note);
    fz_var(pdfOutput);
    fz_try(m_context)
    {
        page = pdf_load_page(m_context, m_document, pageNumber);
        const fz_rect bounds = pdf_bound_page(m_context, page, FZ_CROP_BOX);
        const float width = bounds.x1 - bounds.x0;
        const float height = bounds.y1 - bounds.y0;
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
            fz_throw(m_context, FZ_ERROR_FORMAT, "Invalid PDF page dimensions");
        const float size = std::min({24.0f, width, height});
        const float x = bounds.x0 + std::clamp(static_cast<float>(position.x()) * width - size / 2,
                                               0.0f, width - size);
        const float y = bounds.y0 + std::clamp(static_cast<float>(position.y()) * height - size / 2,
                                               0.0f, height - size);
        note = pdf_create_annot(m_context, page, PDF_ANNOT_TEXT);
        pdf_set_annot_rect(m_context, note, fz_make_rect(x, y, x + size, y + size));
        pdf_set_annot_contents(m_context, note, utf8Text.constData());
        pdf_set_annot_author(m_context, note, utf8Author.constData());
        pdf_set_annot_creation_date(m_context, note, timestamp);
        pdf_set_annot_modification_date(m_context, note, timestamp);
        pdf_update_annot(m_context, note);

        pdf_write_options options = pdf_default_write_options;
        options.do_encrypt = PDF_ENCRYPT_KEEP;
        pdfOutput = fz_new_output(m_context, 64 * 1024, &output, writePdf, nullptr, nullptr);
        pdfOutput->tell = tellPdf;
        pdf_write_document(m_context, m_document, pdfOutput, &options);
        fz_close_output(m_context, pdfOutput);
    }
    fz_always(m_context)
    {
        fz_drop_output(m_context, pdfOutput);
    }
    fz_catch(m_context)
    {
        error = QString::fromUtf8(fz_caught_message(m_context));
    }

    if (note) {
        fz_try(m_context)
        {
            pdf_delete_annot(m_context, page, note);
        }
        fz_catch(m_context)
        {
            m_canAnnotate = false;
        }
    }
    pdf_drop_annot(m_context, note);
    pdf_drop_page(m_context, page);
    if (!error.isEmpty())
        return false;

    if (!output.commit()) {
        error = output.errorString();
        return false;
    }
    return true;
}

}
