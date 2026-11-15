#pragma once

#include <QImage>
#include <QList>
#include <QPointF>
#include <QString>

struct fz_context;
struct pdf_document;
class QPainter;
class QPrinter;

namespace Reader {

struct PdfNote
{
    QString author;
    QString text;
};

class PdfDocumentOperations
{
public:
    PdfDocumentOperations() = default;
    ~PdfDocumentOperations();
    PdfDocumentOperations(const PdfDocumentOperations&) = delete;
    PdfDocumentOperations& operator=(const PdfDocumentOperations&) = delete;

    bool open(const QString& path, const QString& password, QString& error);
    int pageCount() const { return m_pageCount; }
    bool canPrint() const { return m_canPrint; }
    bool canCopy() const { return m_canCopy; }
    bool canAnnotate() const { return m_canAnnotate; }
    QString pageText(int page, QString& error) const;
    QList<PdfNote> pageNotes(int page, QString& error) const;
    QImage preview(int page, const QSize& bounds, QString& error) const;
    bool printPage(QPrinter& printer, QPainter& painter, int page, QString& error) const;
    bool saveNoteCopy(int page, QPointF position, const QString& author,
                      const QString& text, const QString& destination, QString& error);

private:
    QImage rasterize(int page, const QSize& bounds, int maxDpi, bool forPrint,
                     QString& error) const;

    fz_context* m_context = nullptr;
    pdf_document* m_document = nullptr;
    QString m_path;
    int m_pageCount = 0;
    bool m_canPrint = false;
    bool m_highQualityPrint = false;
    bool m_canCopy = false;
    bool m_canAnnotate = false;
};

}
