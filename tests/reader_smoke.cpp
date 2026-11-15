#include "FileAssociation.h"
#include "PdfMainWindow.h"
#include "PdfWorkspace.h"
#include "SelectablePdfView.h"

#ifdef MAPDFR_WITH_MUPDF
#include "PdfDocumentOperations.h"
#include "PdfToMarkdown.h"
#include <QPainter>
#include <QPrinter>
#endif

#include <QClipboard>
#include <QDialog>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QInputDialog>
#include <QAction>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMimeData>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPdfView>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollBar>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextBrowser>
#include <QTreeView>
#include <QtTest>

// 阅读器必须把「打不开」表现为 isLoaded() == false + 非空 errorString，
// 而不是崩溃或弹出半成品窗口。main.cpp 依赖这个契约决定是否保留窗口。
class ReaderSmoke : public QObject {
    Q_OBJECT

private slots:
    void missingFileReportsError();
    void corruptFileReportsError();
    void validPdfLoads();
    void zoomLabelShowsPercentage();
    void findFindsText();
    void navigationUsesRealBookmarksAndThumbnails();
    void searchSelectsAndClearsResults();
    void selectAndCopyText();
    void selectTextOnRotatedPage();
    void selectionWorksAcrossZoomModes();
    void helpMenuOpensEmbeddedGuide();
    void opensAndSwitchesDocumentTabs();
    void canceledPasswordDoesNotInterruptQueuedOpen();
    void pdfTypeRegistrationSelfTest();
#ifdef MAPDFR_WITH_MUPDF
    void deniedOperationsRespectPermissions();
    void restrictedSelectionDoesNotCopy();
    void printingProducesVisiblePages();
    void noteCopyRetainsEncryptionAndOriginal();
    void signatureFieldPreventsReviewRewrite();
#endif
};

void ReaderSmoke::missingFileReportsError()
{
    PdfMainWindow window(
        QStringLiteral("Z:/__mapdfr_no_such_file__.pdf"));

    QVERIFY(!window.isLoaded());
    QVERIFY(!window.errorString().isEmpty());
}

void ReaderSmoke::corruptFileReportsError()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    file.write("this is not a PDF document");
    file.flush();

    PdfMainWindow window(file.fileName());

    QVERIFY(!window.isLoaded());
    QVERIFY(!window.errorString().isEmpty());
}

void ReaderSmoke::validPdfLoads()
{
    const QString path =
        QStringLiteral(MAPDFR_TEST_DATA_DIR "/sample.pdf");

    QVERIFY2(QFileInfo::exists(path), qPrintable(path));

    PdfMainWindow window(path);

    QVERIFY2(window.isLoaded(),
             qPrintable(window.errorString()));
}

void ReaderSmoke::zoomLabelShowsPercentage()
{
    const QString path =
        QStringLiteral(MAPDFR_TEST_DATA_DIR "/sample.pdf");

    QVERIFY2(QFileInfo::exists(path), qPrintable(path));

    PdfMainWindow window(path);
    QVERIFY2(window.isLoaded(), qPrintable(window.errorString()));

    auto* label =
        window.findChild<QLabel*>(QStringLiteral("ReaderZoomLabel"));
    QVERIFY(label);

    const QRegularExpression percent(QStringLiteral("^\\d+%$"));
    QVERIFY2(percent.match(label->text()).hasMatch(),
             qPrintable(label->text()));
}

void ReaderSmoke::findFindsText()
{
    const QString path =
        QStringLiteral(MAPDFR_TEST_DATA_DIR "/sample-rich.pdf");

    QVERIFY2(QFileInfo::exists(path), qPrintable(path));

    PdfMainWindow window(path);
    QVERIFY2(window.isLoaded(), qPrintable(window.errorString()));

    auto* edit =
        window.findChild<QLineEdit*>(QStringLiteral("ReaderSearchEdit"));
    QVERIFY(edit);

    auto* status =
        window.findChild<QLabel*>(QStringLiteral("ReaderSearchStatus"));
    QVERIFY(status);

    // sample-rich.pdf 包含真实的中文文本层。
    edit->setText(QStringLiteral("第二页"));

    // QPdfSearchModel 按页异步扫描，结果数量不会在 setSearchString 里同步出现。
    QTRY_VERIFY_WITH_TIMEOUT(
        status->text().startsWith(QStringLiteral("1 / ")), 15000);
}

void ReaderSmoke::navigationUsesRealBookmarksAndThumbnails()
{
    PdfMainWindow window(QStringLiteral(MAPDFR_TEST_DATA_DIR "/sample-navigation.pdf"));
    QVERIFY2(window.isLoaded(), qPrintable(window.errorString()));
    window.show();

    auto* view = window.findChild<QPdfView*>(QStringLiteral("ReaderPdfView"));
    auto* bookmarks = window.findChild<QTreeView*>(QStringLiteral("ReaderBookmarks"));
    auto* thumbnails = window.findChild<QListView*>(QStringLiteral("ReaderThumbnails"));
    auto* pages = window.findChild<QSpinBox*>(QStringLiteral("ReaderPageSpin"));
    auto* dock = window.findChild<QDockWidget*>(QStringLiteral("ReaderNavigation"));
    QVERIFY(view && bookmarks && thumbnails && pages && dock);
    auto* tabs = qobject_cast<QTabWidget*>(dock->widget());
    QVERIFY(tabs);

    QCOMPARE(bookmarks->model()->rowCount(), 2);
    const QModelIndex chapter = bookmarks->model()->index(1, 0);
    QCOMPARE(chapter.data().toString(), QStringLiteral("Inspection"));
    QVERIFY(QMetaObject::invokeMethod(bookmarks, "activated", Q_ARG(QModelIndex, chapter)));
    QTRY_COMPARE(pages->value(), 2);

    tabs->setCurrentWidget(thumbnails);
    QCOMPARE(thumbnails->model()->rowCount(), 3);
    const QModelIndex second = thumbnails->model()->index(1, 0);
    QCOMPARE(second.data().toString(), QStringLiteral("QC-2"));
    QTRY_VERIFY(!second.data(Qt::DecorationRole).value<QIcon>().isNull());
    const QModelIndex first = thumbnails->model()->index(0, 0);
    QVERIFY(QMetaObject::invokeMethod(thumbnails, "activated", Q_ARG(QModelIndex, first)));
    QTRY_COMPARE(pages->value(), 1);

    view->pageNavigator()->back();
    QTRY_COMPARE(pages->value(), 2);
}

void ReaderSmoke::searchSelectsAndClearsResults()
{
    PdfMainWindow window(QStringLiteral(MAPDFR_TEST_DATA_DIR "/sample-navigation.pdf"));
    QVERIFY2(window.isLoaded(), qPrintable(window.errorString()));
    window.show();

    auto* edit = window.findChild<QLineEdit*>(QStringLiteral("ReaderSearchEdit"));
    auto* status = window.findChild<QLabel*>(QStringLiteral("ReaderSearchStatus"));
    auto* results = window.findChild<QListView*>(QStringLiteral("ReaderSearchResults"));
    auto* pdf = window.findChild<QPdfView*>(QStringLiteral("ReaderPdfView"));
    QVERIFY(edit && status && results && pdf);
    QVERIFY(QMetaObject::invokeMethod(&window, "onFind"));
    edit->setText(QStringLiteral("checkpoint"));
    QTRY_COMPARE(results->model()->rowCount(), 3);
    const QModelIndex last = results->model()->index(2, 0);
    QVERIFY(last.data().toString().contains(QStringLiteral("第 3 页")));
    QVERIFY(QMetaObject::invokeMethod(results, "activated", Q_ARG(QModelIndex, last)));
    QCOMPARE(pdf->currentSearchResultIndex(), 2);
    QTRY_COMPARE(pdf->pageNavigator()->currentPage(), 2);

    edit->setText(QStringLiteral("TermThatDoesNotExist"));
    QTRY_COMPARE(status->text(), QStringLiteral("无结果"));
    QCOMPARE(results->model()->rowCount(), 0);
    QCOMPARE(pdf->currentSearchResultIndex(), -1);
    QVERIFY(QMetaObject::invokeMethod(&window, "onFindClose"));
    QCOMPARE(status->text(), QString());
}

namespace {

void dragOnPage(SelectablePdfView* view, int page, QPointF from, QPointF to)
{
    QWidget* canvas = view->viewport();
    const QMargins margins = view->documentMargins();
    const QSizeF points = view->document()->pagePointSize(page);
    const int width = canvas->width() - margins.left() - margins.right();
    const qreal screenScale = QGuiApplication::primaryScreen()->logicalDotsPerInch() / 72;
    QSize pixels;
    int top = margins.top();
    int left = margins.left();
    if (view->zoomMode() == QPdfView::ZoomMode::FitToWidth) {
        pixels = (points * (width / points.width())).toSize();
        if (view->pageMode() == QPdfView::PageMode::MultiPage) {
            for (int p = 0; p < page; ++p) {
                const QSizeF size = view->document()->pagePointSize(p);
                top += qRound(size.height() * width / size.width()) + view->pageSpacing();
            }
        }
    } else {
        QCOMPARE(view->pageMode(), QPdfView::PageMode::SinglePage);
        if (view->zoomMode() == QPdfView::ZoomMode::FitInView) {
            pixels = (points * screenScale).toSize().scaled(
                QSize(width, canvas->height() - view->pageSpacing()), Qt::KeepAspectRatio);
            left = (canvas->width() - pixels.width()) / 2;
        } else {
            pixels = (points * (screenScale * view->zoomFactor())).toSize();
            left = qMax(margins.left(), (canvas->width() - pixels.width()) / 2);
        }
    }
    const qreal xScale = pixels.width() / points.width();
    const qreal yScale = pixels.height() / points.height();
    const QPoint offset(left - view->horizontalScrollBar()->value(),
                        top - view->verticalScrollBar()->value());
    const QPoint start = QPointF(from.x() * xScale, from.y() * yScale).toPoint() + offset;
    const QPoint end = QPointF(to.x() * xScale, to.y() * yScale).toPoint() + offset;
    QVERIFY2(canvas->rect().contains(start), "Selection start must be visible");
    QVERIFY2(canvas->rect().contains(end), "Selection end must be visible");
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(canvas, end);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
}

QPair<QPointF, QPointF> inspectionWord(SelectablePdfView* view, int page)
{
    const auto glyph = [view, page](int index) {
        return view->document()->getSelectionAtIndex(page, index, 1)
            .boundingRectangle().normalized();
    };
    const QRectF first = glyph(0);
    const QRectF last = glyph(9);
    if (page == 1)
        return {first.center(), glyph(10).center()};
    return {QPointF(first.left() + 0.5, first.center().y()),
            QPointF(last.right() - 0.7, last.center().y())};
}

}

void ReaderSmoke::selectAndCopyText()
{
    PdfMainWindow window(QStringLiteral(MAPDFR_TEST_DATA_DIR "/sample-navigation.pdf"));
    QVERIFY(window.isLoaded());
    window.show();
    window.activateWindow();
    auto* view = window.findChild<SelectablePdfView*>();
    auto* copy = window.findChild<QAction*>(QStringLiteral("ReaderCopySelection"));
    auto* search = window.findChild<QLineEdit*>(QStringLiteral("ReaderSearchEdit"));
    QVERIFY(view && copy && search);
    QCoreApplication::processEvents();

    const auto span = inspectionWord(view, 0);
    dragOnPage(view, 0, span.first, span.second);
    QCOMPARE(view->selectedText(), QStringLiteral("Inspection"));
    const QString sentinel = QStringLiteral("clipboard sentinel");
    QApplication::clipboard()->setText(sentinel);
    QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
#ifdef MAPDFR_WITH_MUPDF
    QVERIFY(copy->isEnabled());
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("Inspection"));
#else
    QVERIFY(!copy->isEnabled());
    QCOMPARE(QApplication::clipboard()->text(), sentinel);
#endif

    dragOnPage(view, 0, span.second, span.first);
    QCOMPARE(view->selectedText(), QStringLiteral("Inspection"));
    const QRectF glyph = view->document()->getSelectionAtIndex(0, 4, 1)
        .boundingRectangle().normalized();
    dragOnPage(view, 0, QPointF(glyph.left() + 0.3, glyph.center().y()),
                        QPointF(glyph.right() - 0.3, glyph.center().y()));
    QCOMPARE(view->selectedText(), QStringLiteral("e"));

    QVERIFY(QMetaObject::invokeMethod(&window, "onFind"));
    search->setText(QStringLiteral("other input"));
    search->selectAll();
    QTest::keyClick(search, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("other input"));
    QVERIFY(QMetaObject::invokeMethod(&window, "onFindClose"));

    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(25, 25));
    QVERIFY(view->selectedText().isEmpty());
    QVERIFY(!copy->isEnabled());
}

void ReaderSmoke::selectTextOnRotatedPage()
{
    PdfMainWindow window(QStringLiteral(MAPDFR_TEST_DATA_DIR "/sample-navigation.pdf"));
    QVERIFY(window.isLoaded());
    window.show();
    auto* view = window.findChild<SelectablePdfView*>();
    QVERIFY(view);
    view->pageNavigator()->jump(1, {});
    QTRY_COMPARE(view->pageNavigator()->currentPage(), 1);
    const auto span = inspectionWord(view, 1);
    dragOnPage(view, 1, span.first, span.second);
    QCOMPARE(view->selectedText().trimmed(), QStringLiteral("Inspection"));
}

void ReaderSmoke::selectionWorksAcrossZoomModes()
{
    PdfMainWindow window(QStringLiteral(MAPDFR_TEST_DATA_DIR "/sample-navigation.pdf"));
    QVERIFY(window.isLoaded());
    window.show();
    auto* view = window.findChild<SelectablePdfView*>();
    QVERIFY(view);
    view->setPageMode(QPdfView::PageMode::SinglePage);
    const auto span = inspectionWord(view, 0);

    view->setZoomMode(QPdfView::ZoomMode::FitInView);
    QCoreApplication::processEvents();
    dragOnPage(view, 0, span.first, span.second);
    QCOMPARE(view->selectedText(), QStringLiteral("Inspection"));

    view->setZoomFactor(1.55);
    view->setZoomMode(QPdfView::ZoomMode::Custom);
    QCoreApplication::processEvents();
    view->horizontalScrollBar()->setValue(25);
    view->verticalScrollBar()->setValue(40);
    dragOnPage(view, 0, span.first, span.second);
    QCOMPARE(view->selectedText(), QStringLiteral("Inspection"));
}

void ReaderSmoke::helpMenuOpensEmbeddedGuide()
{
    PdfMainWindow window(QStringLiteral(MAPDFR_TEST_DATA_DIR "/sample.pdf"));
    QVERIFY(window.isLoaded());
    window.show();
    window.activateWindow();
    auto* help = window.findChild<QAction*>(QStringLiteral("ReaderUserGuide"));
    QVERIFY(help);
    QCOMPARE(help->shortcut(), QKeySequence(QKeySequence::HelpContents));
    help->trigger();
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("ReaderUserGuideDialog"));
    QVERIFY(dialog && dialog->isVisible());
    auto* reader = dialog->findChild<QTextBrowser*>();
    QVERIFY(reader);
    QVERIFY(reader->toPlainText().contains(QStringLiteral("选择与复制文本")));
    QVERIFY(reader->toPlainText().contains(QStringLiteral("常见问题")));
    QCOMPARE(reader->source().scheme(), QStringLiteral("qrc"));
    help->trigger();
    QCOMPARE(window.findChildren<QDialog*>(QStringLiteral("ReaderUserGuideDialog")).size(), 1);
}

void ReaderSmoke::opensAndSwitchesDocumentTabs()
{
    PdfWorkspace workspace;
    workspace.show();
    workspace.activateWindow();
    auto* tabs = workspace.findChild<QTabWidget*>(QStringLiteral("ReaderDocumentTabs"));
    QVERIFY(tabs);
    const QString navigation = QStringLiteral(MAPDFR_TEST_DATA_DIR "/sample-navigation.pdf");
    workspace.requestOpen({navigation});
    QTRY_COMPARE(tabs->count(), 1);
    auto* first = qobject_cast<PdfMainWindow*>(tabs->currentWidget());
    QVERIFY(first && !first->isWindow());
    auto* view = first->findChild<SelectablePdfView*>();
    QVERIFY(view);
    view->setZoomMode(QPdfView::ZoomMode::Custom);
    view->setZoomFactor(1.21);
    view->pageNavigator()->jump(1, {});
    QTRY_COMPARE(view->pageNavigator()->currentPage(), 1);
    QVERIFY(QMetaObject::invokeMethod(first, "onFind"));
    auto* search = first->findChild<QLineEdit*>(QStringLiteral("ReaderSearchEdit"));
    QVERIFY(search);
    search->setText(QStringLiteral("no existing hit"));
    const QString second = QStringLiteral(MAPDFR_TEST_DATA_DIR "/sample.pdf");
    first->openRequested(second);
    workspace.requestOpen({QStringLiteral(MAPDFR_TEST_DATA_DIR "/sample-rich.pdf"),
                           QStringLiteral(MAPDFR_TEST_DATA_DIR "/sample-signature-field.pdf")});
    QTRY_COMPARE(tabs->count(), 4);
    QCOMPARE(tabs->currentIndex(), 3);
    QVERIFY(workspace.windowTitle().contains(QStringLiteral("sample-signature-field.pdf")));

    // Canonical paths, including a redundant directory component, reuse the
    // original tab and keep its document state.
    workspace.requestOpen({QStringLiteral(MAPDFR_TEST_DATA_DIR "/./sample-navigation.pdf")});
    QTRY_COMPARE(tabs->currentWidget(), static_cast<QWidget*>(first));
    QCOMPARE(tabs->count(), 4);
    QCOMPARE(view->zoomFactor(), qreal(1.21));
    QCOMPARE(view->pageNavigator()->currentPage(), 1);
    QCOMPARE(search->text(), QStringLiteral("no existing hit"));

    QTest::keyClick(view, Qt::Key_Tab, Qt::ControlModifier);
    QTRY_COMPARE(tabs->currentIndex(), 1);
    QTest::keyClick(tabs->currentWidget(), Qt::Key_Tab, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_COMPARE(tabs->currentWidget(), static_cast<QWidget*>(first));

    QTest::keyClick(view, Qt::Key_W, Qt::ControlModifier);
    QTRY_COMPARE(tabs->count(), 3);
    QCOMPARE(tabs->tabText(0), QStringLiteral("sample.pdf"));
    for (int count = 3; count > 0; --count) {
        tabs->tabCloseRequested(0);
        QCOMPARE(tabs->count(), count - 1);
    }
    QVERIFY(workspace.isVisible());
    QMimeData files;
    files.setUrls({QUrl::fromLocalFile(second), QUrl::fromLocalFile(navigation)});
    QDragEnterEvent enter(QPoint(100, 100), Qt::CopyAction, &files, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&workspace, &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(100, 100), Qt::CopyAction, &files, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&workspace, &drop);
    QVERIFY(drop.isAccepted());
    QTRY_COMPARE(tabs->count(), 2);
    QCOMPARE(tabs->tabText(0), QStringLiteral("sample.pdf"));
    QCOMPARE(tabs->tabText(1), QStringLiteral("sample-navigation.pdf"));
}

void ReaderSmoke::canceledPasswordDoesNotInterruptQueuedOpen()
{
    PdfWorkspace workspace;
    workspace.show();
    auto* tabs = workspace.findChild<QTabWidget*>(QStringLiteral("ReaderDocumentTabs"));
    QVERIFY(tabs);
    QTimer cancel;
    cancel.setInterval(10);
    connect(&cancel, &QTimer::timeout, &workspace, [&workspace, &cancel]() {
        auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        if (!input)
            return;
        cancel.stop();
        workspace.requestOpen({QStringLiteral(MAPDFR_TEST_DATA_DIR "/sample-rich.pdf")});
        input->reject();
    });
    cancel.start();
    workspace.requestOpen({QStringLiteral(MAPDFR_TEST_DATA_DIR "/sample-encrypted.pdf")});
    QTRY_COMPARE(tabs->count(), 1);
    QCOMPARE(tabs->tabText(0), QStringLiteral("sample-rich.pdf"));
}

void ReaderSmoke::pdfTypeRegistrationSelfTest()
{
#if defined(Q_OS_WIN)
    // 注册逻辑只写 HKCU 的隔离子树（Software\MaPdfR\SelfTest\<uuid>），
    // 不会碰真实的 .pdf 关联。
    QString error;

    QVERIFY2(MaPdfRFileType::selfTest(error), qPrintable(error));
#elif defined(Q_OS_LINUX)
    // Linux 的关联由用户桌面项（.desktop）和 mimeapps.list 决定。
    // 全部写入临时 XDG 目录，不触碰真实的用户配置。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QByteArray oldData = qgetenv("XDG_DATA_HOME");
    const QByteArray oldConfig = qgetenv("XDG_CONFIG_HOME");
    const QString dataHome = dir.filePath(QStringLiteral("data"));
    const QString configHome = dir.filePath(QStringLiteral("config"));
    qputenv("XDG_DATA_HOME", QFile::encodeName(dataHome));
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(configHome));

    const QString desktopPath = QDir(dataHome).filePath(
        QStringLiteral("applications/mapdfr.desktop"));
    const QString exe = QStringLiteral("/usr/bin/mapdfr");
    QString error;

    QVERIFY2(MaPdfRFileType::registerPdfType(exe, QString(), error),
             qPrintable(error));
    QVERIFY(QFileInfo::exists(desktopPath));

    QFile desktop(desktopPath);
    QVERIFY(desktop.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(desktop.readAll());
    desktop.close();
    QVERIFY(content.contains(QStringLiteral("Exec=\"/usr/bin/mapdfr\" %F")));
    QVERIFY(content.contains(QStringLiteral("TryExec=/usr/bin/mapdfr")));
    QVERIFY(content.contains(QStringLiteral("MimeType=application/pdf;")));

    if (!QStandardPaths::findExecutable(QStringLiteral("xdg-mime")).isEmpty()) {
        // xdg-mime default 把本程序写入临时 mimeapps.list。
        QVERIFY2(MaPdfRFileType::openWithDialog(error), qPrintable(error));
        QCOMPARE(MaPdfRFileType::query().command,
                 QStringLiteral("mapdfr.desktop"));
    }

    QVERIFY2(MaPdfRFileType::unregisterPdfType(error), qPrintable(error));
    QVERIFY(!QFileInfo::exists(desktopPath));
    QVERIFY(MaPdfRFileType::query().command
            != QStringLiteral("mapdfr.desktop"));

    const QString mimeApps = QDir(configHome).filePath(
        QStringLiteral("mimeapps.list"));

    if (QFileInfo::exists(mimeApps)) {
        QFile file(mimeApps);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!QString::fromUtf8(file.readAll())
                     .contains(QStringLiteral("mapdfr.desktop")));
    }

    if (oldData.isNull())
        qunsetenv("XDG_DATA_HOME");
    else
        qputenv("XDG_DATA_HOME", oldData);
    if (oldConfig.isNull())
        qunsetenv("XDG_CONFIG_HOME");
    else
        qputenv("XDG_CONFIG_HOME", oldConfig);
#else
    QSKIP("Windows registry test");
#endif
}

#ifdef MAPDFR_WITH_MUPDF
void ReaderSmoke::deniedOperationsRespectPermissions()
{
    const QString path = QStringLiteral(MAPDFR_TEST_DATA_DIR "/sample-restricted.pdf");
    QString error;
    Reader::PdfDocumentOperations reader;
    QVERIFY2(reader.open(path, QStringLiteral("review"), error), qPrintable(error));
    QVERIFY(!reader.canPrint());
    QVERIFY(!reader.canCopy());
    QVERIFY(!reader.canAnnotate());
    QVERIFY(reader.pageText(0, error).isEmpty());
    QVERIFY(!error.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mdPath = dir.filePath(QStringLiteral("denied.md"));
    QVERIFY(!Reader::exportPdfToMarkdown(path, mdPath, QStringLiteral("review"),
                                        {}, nullptr, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFileInfo::exists(mdPath));

    Reader::PdfDocumentOperations owner;
    QVERIFY2(owner.open(path, QStringLiteral("qa-owner"), error), qPrintable(error));
    QVERIFY(owner.canPrint());
    QVERIFY(owner.canAnnotate());
    QVERIFY(owner.pageText(0, error).contains(QStringLiteral("Inspection checkpoint 1")));
}

void ReaderSmoke::restrictedSelectionDoesNotCopy()
{
    QTimer::singleShot(0, qApp, []() {
        const auto files = QApplication::topLevelWidgets();
        for (QWidget* widget : files) {
            if (auto* input = qobject_cast<QInputDialog*>(widget); input && input->isVisible()) {
                input->setTextValue(QStringLiteral("review"));
                input->accept();
            }
        }
    });
    PdfMainWindow window(QStringLiteral(MAPDFR_TEST_DATA_DIR "/sample-restricted.pdf"));
    QVERIFY2(window.isLoaded(), qPrintable(window.errorString()));
    window.show();
    auto* view = window.findChild<SelectablePdfView*>();
    auto* copy = window.findChild<QAction*>(QStringLiteral("ReaderCopySelection"));
    QVERIFY(view && copy);
    const auto span = inspectionWord(view, 0);
    dragOnPage(view, 0, span.first, span.second);
    QCOMPARE(view->selectedText(), QStringLiteral("Inspection"));
    QVERIFY(copy->isEnabled());
    const auto sentinel = QStringLiteral("restricted copy sentinel");
    QApplication::clipboard()->setText(sentinel);
    copy->trigger();
    QCOMPARE(QApplication::clipboard()->text(), sentinel);
}

void ReaderSmoke::printingProducesVisiblePages()
{
    QString error;
    Reader::PdfDocumentOperations reader;
    QVERIFY2(reader.open(QStringLiteral(MAPDFR_TEST_DATA_DIR "/sample-review-encrypted.pdf"),
                         QStringLiteral("review"), error), qPrintable(error));
    QVERIFY(reader.canPrint());
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("print.pdf"));
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    printer.setResolution(300);
    QVERIFY(printer.setPageSize(QPageSize(QPageSize::A4)));
    QPainter painter;
    QVERIFY(painter.begin(&printer));
    QVERIFY2(reader.printPage(printer, painter, 1, error), qPrintable(error));
    QVERIFY(printer.newPage());
    QVERIFY2(reader.printPage(printer, painter, 2, error), qPrintable(error));
    QVERIFY(painter.end());

    // Reopen with the independent Qt PDF/PDFium renderer and check that there is ink.
    QPdfDocument printed;
    QCOMPARE(printed.load(path), QPdfDocument::Error::None);
    QCOMPARE(printed.pageCount(), 2);
    for (int page = 0; page < printed.pageCount(); ++page) {
        const QImage image = printed.render(page, QSize(200, 280));
        QVERIFY(!image.isNull());
        int bluePixels = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor color = image.pixelColor(x, y);
                if (color.blue() > color.red() + 30 && color.alpha() == 255)
                    ++bluePixels;
            }
        }
        QVERIFY(bluePixels > 30);
    }
}

void ReaderSmoke::noteCopyRetainsEncryptionAndOriginal()
{
    const QString input = QStringLiteral(MAPDFR_TEST_DATA_DIR "/sample-review-encrypted.pdf");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath(QStringLiteral("原件.pdf"));
    const QString destination = dir.filePath(QStringLiteral("副本.pdf"));
    QVERIFY(QFile::copy(input, source));
    QFile original(source);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const QByteArray baseline = original.readAll();
    original.close();

    QString error;
    Reader::PdfDocumentOperations reader;
    QVERIFY2(reader.open(source, QStringLiteral("review"), error), qPrintable(error));
    QVERIFY(reader.canAnnotate());
    QVERIFY(!reader.canCopy());
    const auto location = QPointF(0.62, 0.26);
    const QString author = QStringLiteral("质检员");
    const QString text = QStringLiteral("请复核旋转页面内的尺寸。");
    QVERIFY(!reader.saveNoteCopy(1, location, author, text, source, error));
    QVERIFY(!reader.saveNoteCopy(1, location, author, text,
                                 dir.filePath(QStringLiteral("missing/note.pdf")), error));
    QVERIFY2(reader.saveNoteCopy(1, location, author, text, destination, error), qPrintable(error));
    QVERIFY2(reader.pageNotes(1, error).isEmpty(), qPrintable(error));
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), baseline);

    QPdfDocument locked;
    QCOMPARE(locked.load(destination), QPdfDocument::Error::IncorrectPassword);
    Reader::PdfDocumentOperations annotated;
    QVERIFY2(annotated.open(destination, QStringLiteral("review"), error), qPrintable(error));
    QVERIFY(!annotated.canCopy());
    QVERIFY(annotated.canPrint());
    QCOMPARE(annotated.pageCount(), 3);
    QVERIFY(annotated.pageNotes(0, error).isEmpty());
    const auto notes = annotated.pageNotes(1, error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(notes.size(), 1);
    QCOMPARE(notes.first().author, author);
    QCOMPARE(notes.first().text, text);
}

void ReaderSmoke::signatureFieldPreventsReviewRewrite()
{
    QString error;
    Reader::PdfDocumentOperations reader;
    QVERIFY2(reader.open(QStringLiteral(MAPDFR_TEST_DATA_DIR "/sample-signature-field.pdf"),
                         QString(), error), qPrintable(error));
    QVERIFY(!reader.canAnnotate());
    QVERIFY(reader.canPrint());
}
#endif

QTEST_MAIN(ReaderSmoke)
#include "reader_smoke.moc"
