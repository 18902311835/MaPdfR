#include "PdfWorkspace.h"

#include "PdfMainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QString documentKey(const QString& path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    const QString absolute = canonical.isEmpty() ? info.absoluteFilePath() : canonical;
#ifdef Q_OS_WIN
    return absolute.toCaseFolded();
#else
    return absolute;
#endif
}

QStringList droppedPdfs(const QMimeData* mime)
{
    QStringList paths;
    for (const QUrl& url : mime->urls()) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (QFileInfo(path).suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0)
            paths.append(path);
    }
    return paths;
}

}

PdfWorkspace::PdfWorkspace(QWidget* parent)
    : QMainWindow(parent),
      m_pages(new QStackedWidget(this)),
      m_empty(new QWidget(m_pages)),
      m_tabs(new QTabWidget(m_pages)),
      m_openTimer(new QTimer(this))
{
    resize(1120, 840);
    setAcceptDrops(true);
    setCentralWidget(m_pages);

    m_tabs->setObjectName(QStringLiteral("ReaderDocumentTabs"));
    m_tabs->setAccessibleName(tr("已打开的 PDF"));
    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setElideMode(Qt::ElideMiddle);
    m_pages->addWidget(m_empty);
    m_pages->addWidget(m_tabs);

    auto* open = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton),
                             tr("打开 PDF…"), m_empty);
    open->setShortcut(QKeySequence::Open);
    open->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_empty->addAction(open);
    connect(open, &QAction::triggered, this, &PdfWorkspace::browse);

    auto* layout = new QVBoxLayout(m_empty);
    auto* openButton = new QPushButton(open->icon(), open->text(), m_empty);
    openButton->setAccessibleDescription(tr("也可以把 PDF 文件拖到窗口中"));
    connect(openButton, &QPushButton::clicked, open, &QAction::trigger);
    layout->addStretch();
    layout->addWidget(openButton, 0, Qt::AlignHCenter);
    auto* hint = new QLabel(tr("也可以把 PDF 文件拖到这里"), m_empty);
    layout->addWidget(hint, 0, Qt::AlignHCenter);
    layout->addStretch();
    m_empty->setFocusProxy(openButton);
    openButton->setFocus();

    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &PdfWorkspace::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, &PdfWorkspace::updateWindowTitle);
    const auto switchTab = [this](int step) {
        const int count = m_tabs->count();
        if (count > 1)
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + count + step) % count);
    };
    auto* next = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), this);
    connect(next, &QShortcut::activated, this, [switchTab]() { switchTab(1); });
    auto* previous = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), this);
    connect(previous, &QShortcut::activated, this, [switchTab]() { switchTab(-1); });
    m_openTimer->setSingleShot(true);
    connect(m_openTimer, &QTimer::timeout, this, &PdfWorkspace::openNext);
    updateWindowTitle();
}

void PdfWorkspace::requestOpen(const QStringList& paths)
{
    m_pending.append(paths);
    m_activate = true;
    if (!m_openTimer->isActive() && !m_opening)
        m_openTimer->start(0);
}

void PdfWorkspace::openNext()
{
    if (m_opening)
        return;
    if (QApplication::activeModalWidget()) {
        m_openTimer->start(100);
        return;
    }
    QScopedValueRollback<bool> opening(m_opening, true);
    if (m_activate) {
        m_activate = false;
        activateWorkspace();
    }
    if (m_pending.isEmpty())
        return;

    const QString path = m_pending.takeFirst();
    const QString key = documentKey(path);
    PdfMainWindow* existing = nullptr;
    for (int index = 0; index < m_tabs->count(); ++index) {
        auto* reader = static_cast<PdfMainWindow*>(m_tabs->widget(index));
        if (reader->property("documentKey").toString() == key) {
            existing = reader;
            break;
        }
    }

    if (existing) {
        m_tabs->setCurrentWidget(existing);
    } else {
        // Use the visible workspace for modal password prompts. QTabWidget
        // reparents the loaded reader only after it has opened successfully.
        auto* reader = new PdfMainWindow(path, this);
        if (!reader->isLoaded()) {
            const QString error = reader->errorString();
            delete reader;
            if (!error.isEmpty()) {
                QMessageBox::warning(this, tr("无法打开 PDF"),
                                     tr("%1\n%2").arg(path, error));
            }
        } else {
            reader->setProperty("documentKey", key);
            connect(reader, &PdfMainWindow::openRequested, this,
                    [this](const QString& file) { requestOpen({file}); });
            connect(reader, &PdfMainWindow::closeRequested, this,
                    [this, reader]() { closeTab(m_tabs->indexOf(reader)); });
            const int index = m_tabs->addTab(reader, QFileInfo(path).fileName());
            m_tabs->setTabToolTip(index, QFileInfo(path).absoluteFilePath());
            m_pages->setCurrentWidget(m_tabs);
            m_tabs->setCurrentIndex(index);
            reader->focusDocument();
        }
    }
    if (!m_pending.isEmpty() || m_activate)
        m_openTimer->start(0);
}

void PdfWorkspace::browse()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("打开 PDF"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        tr("PDF 文件 (*.pdf);;所有文件 (*.*)"));
    if (!files.isEmpty())
        requestOpen(files);
}

void PdfWorkspace::closeTab(int index)
{
    auto* reader = qobject_cast<PdfMainWindow*>(m_tabs->widget(index));
    if (!reader || !reader->close())
        return;

    m_tabs->removeTab(index);
    reader->deleteLater();
    if (m_tabs->count() == 0) {
        m_pages->setCurrentWidget(m_empty);
        m_empty->setFocus();
    }
}

void PdfWorkspace::updateWindowTitle()
{
    if (m_tabs->currentWidget())
        setWindowTitle(tr("MaPdfR — %1").arg(m_tabs->tabText(m_tabs->currentIndex())));
    else
        setWindowTitle(QStringLiteral("MaPdfR"));
}

void PdfWorkspace::activateWorkspace()
{
    if (isMinimized())
        setWindowState(windowState() & ~Qt::WindowMinimized);
    show();
    raise();
    activateWindow();
}

void PdfWorkspace::closeEvent(QCloseEvent* event)
{
    if (m_opening) {
        event->ignore();
        return;
    }
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (static_cast<PdfMainWindow*>(m_tabs->widget(i))->isBusy()) {
            event->ignore();
            return;
        }
    }
    QMainWindow::closeEvent(event);
}

void PdfWorkspace::dragEnterEvent(QDragEnterEvent* event)
{
    if (!droppedPdfs(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void PdfWorkspace::dropEvent(QDropEvent* event)
{
    const QStringList paths = droppedPdfs(event->mimeData());
    if (!paths.isEmpty()) {
        event->acceptProposedAction();
        requestOpen(paths);
    }
}
