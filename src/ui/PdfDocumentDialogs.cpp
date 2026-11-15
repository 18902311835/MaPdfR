#include "PdfDocumentDialogs.h"

#include "PdfDocumentOperations.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTextEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPageRanges>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPrintDialog>
#include <QPrinter>
#include <QPrinterInfo>
#include <QProgressDialog>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>

namespace Reader {
namespace {

QString tr(const char* text)
{
    return QApplication::translate("PdfDocumentDialogs", text);
}

class NotePreview : public QWidget
{
public:
    NotePreview(const QImage& image, QWidget* parent)
        : QWidget(parent), m_image(image)
    {
        setFixedSize(image.size());
        setCursor(Qt::CrossCursor);
        setAccessibleName(tr("批注位置预览"));
    }

    QPointF position() const { return m_position; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.drawImage(QPoint(), m_image);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPointF center(m_position.x() * width(), m_position.y() * height());
        painter.setPen(QPen(QColor(73, 51, 18), 1.5));
        painter.setBrush(QColor(255, 205, 89));
        painter.drawRoundedRect(QRectF(center - QPointF(8, 8), QSizeF(16, 16)), 3, 3);
        painter.drawLine(center - QPointF(4, 2), center + QPointF(4, -2));
        painter.drawLine(center - QPointF(4, -2), center + QPointF(2, 2));
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        m_position = QPointF(std::clamp(event->position().x() / width(), 0.0, 1.0),
                             std::clamp(event->position().y() / height(), 0.0, 1.0));
        update();
    }

private:
    QImage m_image;
    QPointF m_position{0.9, 0.12};
};

QList<QPageRanges::Range> selectedRanges(const QPrinter& printer, int currentPage, int total)
{
    if (printer.printRange() == QPrinter::CurrentPage)
        return {{currentPage + 1, currentPage + 1}};
    if (printer.printRange() != QPrinter::PageRange)
        return {{1, total}};
    auto ranges = printer.pageRanges().toRangeList();
    if (ranges.isEmpty() && printer.fromPage() > 0 && printer.toPage() >= printer.fromPage())
        ranges.append({printer.fromPage(), printer.toPage()});
    return ranges;
}

}

void printDocument(QWidget* parent, PdfDocumentOperations& document, int currentPage,
                   const QString& displayName)
{
    if (QPrinterInfo::availablePrinterNames().isEmpty()) {
        QMessageBox::warning(parent, tr("无法打印"), tr("系统没有可用的打印机。"));
        return;
    }
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer, parent);
    dialog.setWindowTitle(tr("打印 PDF"));
    dialog.setMinMax(1, document.pageCount());
    dialog.setOption(QAbstractPrintDialog::PrintPageRange);
    dialog.setOption(QAbstractPrintDialog::PrintCurrentPage);
    dialog.setOption(QAbstractPrintDialog::PrintSelection, false);
    dialog.setOption(QAbstractPrintDialog::PrintToFile, false);

    if (dialog.exec() != QDialog::Accepted)
        return;

    auto ranges = selectedRanges(printer, currentPage, document.pageCount());
    const bool reverse = printer.pageOrder() == QPrinter::LastPageFirst;
    if (reverse)
        std::reverse(ranges.begin(), ranges.end());

    qint64 pages = 0;
    for (const auto& range : ranges) {
        if (range.from < 1 || range.to > document.pageCount() || range.from > range.to) {
            QMessageBox::warning(parent, tr("无法打印"), tr("页码范围超出文档。"));
            return;
        }
        pages += qint64(range.to) - range.from + 1;
    }
    const int copies = printer.supportsMultipleCopies() ? 1 : qMax(1, printer.copyCount());
    const bool collated = printer.collateCopies();
    if (pages <= 0 || pages > std::numeric_limits<int>::max() / copies) {
        QMessageBox::warning(parent, tr("无法打印"), tr("请选择有效的打印范围与份数。"));
        return;
    }
    if (copies > 1)
        printer.setCopyCount(1);
    printer.setDocName(displayName);
    printer.setFullPage(false);

    QPainter painter;
    if (!painter.begin(&printer)) {
        QMessageBox::warning(parent, tr("无法打印"), tr("打印设备无法开始作业。"));
        return;
    }
    QProgressDialog progress(tr("正在生成打印页…"), tr("取消"), 0,
                             static_cast<int>(pages * copies), parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setMinimumDuration(300);
    progress.setValue(0);

    int printed = 0;
    QString error;
    const auto sendPage = [&](int page) {
        if (progress.wasCanceled())
            return false;
        if (printed != 0 && !printer.newPage()) {
            error = tr("打印设备无法开始新的一页。");
            return false;
        }
        if (!document.printPage(printer, painter, page - 1, error)) {
            error = tr("第 %1 页：%2").arg(page).arg(error);
            return false;
        }
        progress.setValue(++printed);
        return !progress.wasCanceled();
    };
    const auto sendAllPages = [&]() {
        for (const auto& range : ranges) {
            const int first = reverse ? range.to : range.from;
            const int last = reverse ? range.from : range.to;
            for (int page = first;; page += (reverse ? -1 : 1)) {
                for (int n = 0; n < (collated ? 1 : copies); ++n) {
                    if (!sendPage(page))
                        return false;
                }
                if (page == last)
                    break;
            }
        }
        return true;
    };

    bool finished = true;
    for (int n = 0; n < (collated ? copies : 1) && finished; ++n)
        finished = sendAllPages();
    if (!finished)
        printer.abort();
    if (!painter.end() && finished)
        error = tr("打印设备未能完成作业。");
    progress.close();

    if (!error.isEmpty())
        QMessageBox::warning(parent, tr("打印失败"), error);
}

void showPageNotes(QWidget* parent, const PdfDocumentOperations& document, int page)
{
    QString error;
    const auto notes = document.pageNotes(page, error);
    const QString title = tr("第 %1 页 · 便笺").arg(page + 1);
    if (!error.isEmpty()) {
        QMessageBox::warning(parent, title, error);
        return;
    }
    if (notes.isEmpty()) {
        QMessageBox::information(parent, title, tr("当前页没有可查看的便笺。"));
        return;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    list->setAccessibleName(tr("便笺列表"));
    for (qsizetype i = 0; i < notes.size(); ++i) {
        const auto& note = notes.at(i);
        list->addItem(tr("便笺 %1 · %2").arg(i + 1)
                          .arg(note.author.isEmpty() ? tr("未署名") : note.author.simplified()));
    }
    layout->addWidget(list);
    auto* detail = new QTextEdit(&dialog);
    detail->setAccessibleName(tr("便笺正文"));
    detail->setReadOnly(true);
    detail->setTextInteractionFlags(document.canCopy()
        ? Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard : Qt::NoTextInteraction);
    layout->addWidget(detail, 2);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    QObject::connect(list, &QListWidget::currentRowChanged, detail, [detail, &notes](int row) {
        detail->setPlainText(row >= 0 && row < notes.size() ? notes.at(row).text : QString());
    });
    list->setCurrentRow(0);
    dialog.resize(480, 420);
    dialog.exec();
}

QString createReviewCopy(QWidget* parent, PdfDocumentOperations& document, int page,
                         const QString& sourcePath)
{
    QString error;
    const QImage preview = document.preview(page, QSize(350, 370), error);
    if (preview.isNull()) {
        QMessageBox::warning(parent, tr("无法预览页面"), error);
        return {};
    }
    QDialog dialog(parent);
    dialog.setWindowTitle(tr("第 %1 页 · 添加便笺批注").arg(page + 1));

    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("在页面预览中点击可更改便笺位置。"), &dialog));
    auto* notePreview = new NotePreview(preview, &dialog);
    layout->addWidget(notePreview, 0, Qt::AlignHCenter);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    auto* author = new QLineEdit(&dialog);
    author->setMaxLength(120);
    author->setPlaceholderText(tr("选填"));
    form->addRow(tr("作者"), author);
    auto* note = new QPlainTextEdit(&dialog);
    note->setTabChangesFocus(true);
    note->setMinimumWidth(320);
    note->setFixedHeight(note->fontMetrics().lineSpacing() * 5 + 12);
    form->addRow(tr("批注"), note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto* save = buttons->addButton(tr("另存批注副本…"), QDialogButtonBox::AcceptRole);
    save->setEnabled(false);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(note, &QPlainTextEdit::textChanged, &dialog, [save, note]() {
        save->setEnabled(!note->toPlainText().trimmed().isEmpty());
    });
    note->setFocus();

    QString savedPath;
    const QFileInfo source(sourcePath);
    const QString suggestion = QDir(source.absolutePath()).filePath(
        tr("%1-批注.pdf").arg(source.completeBaseName()));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        QFileDialog output(&dialog, tr("另存批注副本"), suggestion, tr("PDF 文件 (*.pdf)"));
        output.setAcceptMode(QFileDialog::AcceptSave);
        output.setDefaultSuffix(QStringLiteral("pdf"));
        if (output.exec() != QDialog::Accepted)
            return;

        const QString path = output.selectedFiles().constFirst();
        if (!document.saveNoteCopy(page, notePreview->position(), author->text().trimmed(),
                                   note->toPlainText().trimmed(), path, error)) {
            QMessageBox::warning(&dialog, tr("保存批注失败"), error);
            return;
        }
        savedPath = path;
        dialog.accept();
    });
    dialog.exec();
    return savedPath;
}

}
