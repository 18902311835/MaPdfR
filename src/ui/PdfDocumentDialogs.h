#pragma once

class QString;
class QWidget;

namespace Reader {

class PdfDocumentOperations;

void printDocument(QWidget* parent, PdfDocumentOperations& document, int currentPage,
                   const QString& displayName);
void showPageNotes(QWidget* parent, const PdfDocumentOperations& document, int page);
QString createReviewCopy(QWidget* parent, PdfDocumentOperations& document, int page,
                         const QString& sourcePath);

}
