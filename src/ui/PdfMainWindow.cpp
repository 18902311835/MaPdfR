#include "PdfMainWindow.h"

#include "AboutDialog.h"
#include "FileAssociation.h"
#include "ReaderNavigation.h"
#include "SafeFileCopy.h"
#include "SelectablePdfView.h"

#ifdef MAPDFR_WITH_MUPDF
#include "PdfDocumentDialogs.h"
#include "PdfDocumentOperations.h"
#include "PdfToMarkdown.h"
#endif

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPdfDocument>
#include <QPdfLink>
#include <QPdfPageNavigator>
#include <QPdfSearchModel>
#include <QPdfView>
#include <QProgressDialog>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

PdfMainWindow::PdfMainWindow(const QString& filePath,
                             QWidget* parent,
                             const QString& displayName)
    : QMainWindow(parent),
      m_filePath(QFileInfo(filePath).absoluteFilePath()),
      m_displayName(displayName.isEmpty()
                        ? QFileInfo(filePath).fileName()
                        : displayName)
{
    setWindowTitle(
        tr("PDF 查看器 — %1")
            .arg(m_displayName));
    setWindowFlag(Qt::Window, parent == nullptr);
    setAttribute(Qt::WA_DeleteOnClose, parent == nullptr);
    resize(1120, 840);
    setAcceptDrops(parent == nullptr);

    buildUi();
    buildMenus();
    loadDocument();
    for (QAction* action : m_documentActions)
        action->setEnabled(m_loaded);
    m_pageSpin->setEnabled(m_loaded);
    m_findNextAction->setEnabled(false);
    m_findPrevAction->setEnabled(false);
    m_findCloseAction->setEnabled(false);
    if (m_loaded)
        onPageNumberChanged(currentPage());

    if (!m_loaded && !m_error.isEmpty())
        statusBar()->showMessage(m_error);
}

PdfMainWindow::~PdfMainWindow()
{
    m_view->viewport()->removeEventFilter(this);
    // Stop outstanding page renders and search before destroying their document.
    delete m_navigation;
    delete m_view;
    delete m_searchModel;
    if (m_doc)
        m_doc->close();
}

void PdfMainWindow::focusDocument()
{
    m_view->setFocus(Qt::OtherFocusReason);
}

bool PdfMainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (m_view && watched == m_view->viewport() && event->type() == QEvent::Resize)
        updateZoomLabel();
    return QMainWindow::eventFilter(watched, event);
}

void PdfMainWindow::closeEvent(QCloseEvent* event)
{
    if (m_busy) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void PdfMainWindow::buildUi()
{
    auto* toolbar = addToolBar(tr("工具"));
    toolbar->setObjectName(QStringLiteral("ReaderMainToolbar"));
    toolbar->setIconSize(QSize(20, 20));
    toolbar->setMovable(false);

    m_openAction = toolbar->addAction(
        style()->standardIcon(
            QStyle::SP_DialogOpenButton),
        tr("打开..."));

    m_openAction->setShortcut(QKeySequence::Open);

    connect(m_openAction,
            &QAction::triggered,
            this,
            &PdfMainWindow::onOpen);

    m_saveAction = toolbar->addAction(
        style()->standardIcon(
            QStyle::SP_DialogSaveButton),
        tr("另存为..."));

    m_saveAction->setShortcut(QKeySequence::Save);

    connect(m_saveAction,
            &QAction::triggered,
            this,
            &PdfMainWindow::onSaveAs);

#ifdef MAPDFR_WITH_MUPDF
    m_printAction = toolbar->addAction(tr("打印…"));
    m_printAction->setShortcut(QKeySequence::Print);
    connect(m_printAction, &QAction::triggered, this, &PdfMainWindow::onPrint);

    m_exportMarkdownAction = toolbar->addAction(tr("导出 MD…"));
    m_exportMarkdownAction->setToolTip(tr("把当前 PDF 转换为 Markdown 文档"));
    m_exportMarkdownAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+M")));
    m_exportMarkdownAction->setEnabled(false);

    connect(m_exportMarkdownAction,
            &QAction::triggered,
            this,
            &PdfMainWindow::onExportMarkdown);
#endif

    toolbar->addSeparator();

    m_prevPageAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_ArrowLeft),
        tr("上一页"));

    m_prevPageAction->setShortcut(Qt::Key_PageUp);

    connect(m_prevPageAction,
            &QAction::triggered,
            this,
            &PdfMainWindow::onPagePrev);

    m_pageSpin = new QSpinBox(toolbar);
    m_pageSpin->setObjectName(
        QStringLiteral("ReaderPageSpin"));
    m_pageSpin->setRange(1, 1);
    m_pageSpin->setAccessibleName(tr("转到页码"));
    m_pageSpin->setKeyboardTracking(false);
    m_pageSpin->setAlignment(Qt::AlignCenter);
    m_pageSpin->setMinimumWidth(60);

    connect(
        m_pageSpin,
        QOverload<int>::of(
            &QSpinBox::valueChanged),
        this,
        [this](int value) {
            onPageGoto(value - 1);
        });

    toolbar->addWidget(m_pageSpin);

    m_pageTotal = new QLabel(
        QStringLiteral(" / 0 "),
        toolbar);
    m_pageTotal->setMinimumWidth(50);
    toolbar->addWidget(m_pageTotal);

    m_nextPageAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_ArrowRight),
        tr("下一页"));

    m_nextPageAction->setShortcut(Qt::Key_PageDown);

    connect(m_nextPageAction,
            &QAction::triggered,
            this,
            &PdfMainWindow::onPageNext);

    toolbar->addSeparator();

    m_zoomOutAction =
        toolbar->addAction(tr("缩小"));
    m_zoomOutAction->setShortcut(
        QKeySequence::ZoomOut);

    connect(m_zoomOutAction,
            &QAction::triggered,
            this,
            &PdfMainWindow::onZoomOut);

    m_actualSizeAction =
        toolbar->addAction(tr("实际大小"));

    connect(m_actualSizeAction,
            &QAction::triggered,
            this,
            &PdfMainWindow::onZoomActual);

    m_zoomInAction =
        toolbar->addAction(tr("放大"));
    m_zoomInAction->setShortcut(
        QKeySequence::ZoomIn);

    connect(m_zoomInAction,
            &QAction::triggered,
            this,
            &PdfMainWindow::onZoomIn);

    m_zoomLabel = new QLabel(QStringLiteral("100%"), toolbar);
    m_zoomLabel->setObjectName(
        QStringLiteral("ReaderZoomLabel"));
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_zoomLabel->setMinimumWidth(56);
    m_zoomLabel->setToolTip(tr("当前缩放比例"));
    toolbar->addWidget(m_zoomLabel);

    m_fitWidthAction =
        toolbar->addAction(tr("适合宽度"));

    connect(m_fitWidthAction,
            &QAction::triggered,
            this,
            &PdfMainWindow::onFitWidth);

    m_fitPageAction =
        toolbar->addAction(tr("整页"));

    connect(m_fitPageAction,
            &QAction::triggered,
            this,
            &PdfMainWindow::onFitPage);

    toolbar->addSeparator();

    // 查找动作挂在主窗口上而不是查找栏上：查找栏隐藏时 Ctrl+F 仍然有效。
    m_findAction = new QAction(tr("查找..."), this);
    m_findAction->setShortcut(QKeySequence::Find);
    connect(m_findAction,
            &QAction::triggered,
            this,
            &PdfMainWindow::onFind);
    toolbar->addAction(m_findAction);

    m_view = new SelectablePdfView(this);
    m_view->setObjectName(QStringLiteral("ReaderPdfView"));
    m_view->setAccessibleName(tr("PDF 页面"));
    m_view->setPageSpacing(9);
    m_view->setDocumentMargins(QMargins(12, 12, 12, 12));
    m_view->setPageMode(
        QPdfView::PageMode::MultiPage);
    m_view->setZoomMode(
        QPdfView::ZoomMode::FitToWidth);

    connect(m_view,
            &QPdfView::zoomFactorChanged,
            this,
            &PdfMainWindow::updateZoomLabel);
    connect(m_view, &QPdfView::zoomModeChanged, this, &PdfMainWindow::updateZoomLabel);

    setCentralWidget(m_view);

    buildFindBar();
    m_view->viewport()->installEventFilter(this);

    m_navigation = new ReaderNavigation(m_searchModel, this);
    addDockWidget(Qt::LeftDockWidgetArea, m_navigation);
    connect(m_navigation, &ReaderNavigation::pageRequested, this, &PdfMainWindow::onPageGoto);
    connect(m_navigation, &ReaderNavigation::bookmarkRequested, this,
            &PdfMainWindow::jumpToPage);
    connect(m_navigation, &ReaderNavigation::searchResultRequested, this,
            &PdfMainWindow::selectSearchResult);

    m_documentActions = {m_saveAction, m_zoomInAction, m_zoomOutAction, m_actualSizeAction,
                         m_fitWidthAction, m_fitPageAction, m_findAction};
    statusBar()->showMessage(tr("正在加载..."));
}

void PdfMainWindow::buildFindBar()
{
    addToolBarBreak(Qt::TopToolBarArea);
    m_findToolBar = addToolBar(tr("查找"));
    m_findToolBar->setObjectName(
        QStringLiteral("ReaderFindToolBar"));
    m_findToolBar->setIconSize(QSize(20, 20));
    m_findToolBar->setMovable(false);
    m_findToolBar->hide();

    m_searchModel = new QPdfSearchModel(this);

    m_searchEdit = new QLineEdit(m_findToolBar);
    m_searchEdit->setObjectName(
        QStringLiteral("ReaderSearchEdit"));
    m_searchEdit->setPlaceholderText(tr("查找文本…"));
    m_searchEdit->setAccessibleName(tr("全文查找"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMinimumWidth(220);
    m_searchEdit->setMaximumWidth(280);
    m_findToolBar->addWidget(m_searchEdit);

    m_searchStatus = new QLabel(m_findToolBar);
    m_searchStatus->setObjectName(
        QStringLiteral("ReaderSearchStatus"));
    m_searchStatus->setAlignment(Qt::AlignCenter);
    m_searchStatus->setMinimumWidth(80);
    m_findToolBar->addWidget(m_searchStatus);

    m_findPrevAction = new QAction(
        style()->standardIcon(QStyle::SP_ArrowBack),
        tr("上一个"),
        this);
    m_findPrevAction->setShortcut(
        QKeySequence(Qt::SHIFT | Qt::Key_F3));
    connect(m_findPrevAction,
            &QAction::triggered,
            this,
            &PdfMainWindow::onFindPrev);
    m_findToolBar->addAction(m_findPrevAction);

    m_findNextAction = new QAction(
        style()->standardIcon(QStyle::SP_ArrowForward),
        tr("下一个"),
        this);
    m_findNextAction->setShortcut(Qt::Key_F3);
    connect(m_findNextAction,
            &QAction::triggered,
            this,
            &PdfMainWindow::onFindNext);
    m_findToolBar->addAction(m_findNextAction);

    m_findCloseAction = new QAction(
        style()->standardIcon(QStyle::SP_DialogCloseButton),
        tr("关闭查找"),
        this);
    m_findCloseAction->setShortcut(Qt::Key_Escape);
    connect(m_findCloseAction,
            &QAction::triggered,
            this,
            &PdfMainWindow::onFindClose);
    m_findToolBar->addAction(m_findCloseAction);

    connect(m_searchEdit,
            &QLineEdit::textChanged,
            this,
            &PdfMainWindow::onSearchTextChanged);
    connect(m_searchEdit,
            &QLineEdit::returnPressed,
            this,
            &PdfMainWindow::onFindNext);

    m_searchDelay = new QTimer(this);
    m_searchDelay->setSingleShot(true);
    m_searchDelay->setInterval(220);
    connect(m_searchDelay, &QTimer::timeout, this, &PdfMainWindow::beginSearch);

    m_searchScan = new QTimer(this);
    m_searchScan->setInterval(12);
    connect(m_searchScan, &QTimer::timeout, this, &PdfMainWindow::scanSearchPage);

    connect(m_searchModel, &QPdfSearchModel::countChanged, this,
            &PdfMainWindow::updateSearchStatus, Qt::QueuedConnection);
}

void PdfMainWindow::buildMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    fileMenu->addAction(m_openAction);
    fileMenu->addAction(m_saveAction);
#ifdef MAPDFR_WITH_MUPDF
    fileMenu->addAction(m_printAction);
    fileMenu->addAction(m_exportMarkdownAction);
    m_documentActions << m_printAction << m_exportMarkdownAction;
#endif
    m_documentActions << fileMenu->addAction(tr("文档属性…"), QKeySequence(Qt::CTRL | Qt::Key_D),
                                            this, &PdfMainWindow::showDocumentProperties);
    fileMenu->addSeparator();

    QAction* closeAction = fileMenu->addAction(tr("关闭当前 PDF"));
    QList<QKeySequence> closeKeys = QKeySequence::keyBindings(QKeySequence::Close);
    const QKeySequence closeTabKey(Qt::CTRL | Qt::Key_W);
    if (!closeKeys.contains(closeTabKey))
        closeKeys.prepend(closeTabKey);
    closeAction->setShortcuts(closeKeys);
    addAction(closeAction);
    connect(closeAction, &QAction::triggered, this, [this]() {
        if (isWindow())
            close();
        else
            emit closeRequested();
    });

    QAction* quitAction = fileMenu->addAction(tr("退出"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, [this]() { window()->close(); });

    QMenu* pageMenu = menuBar()->addMenu(tr("页面(&P)"));
    pageMenu->addAction(m_prevPageAction);
    pageMenu->addAction(m_nextPageAction);
    m_documentActions << pageMenu->addAction(tr("转到页码…"), QKeySequence(Qt::CTRL | Qt::Key_G),
                                            this, [this]() {
                                                m_pageSpin->setFocus(Qt::ShortcutFocusReason);
                                                m_pageSpin->selectAll();
                                            });
    m_documentActions << pageMenu->addAction(tr("首页"), QKeySequence(Qt::CTRL | Qt::Key_Home),
                                            this, [this]() { onPageGoto(0); });
    m_documentActions << pageMenu->addAction(tr("末页"), QKeySequence(Qt::CTRL | Qt::Key_End),
                                            this, [this]() { onPageGoto(m_doc->pageCount() - 1); });

    auto* navigator = m_view->pageNavigator();
    pageMenu->addSeparator();
    auto* back = pageMenu->addAction(tr("上一个视图"), QKeySequence(Qt::ALT | Qt::Key_Left),
                                     navigator, &QPdfPageNavigator::back);
    auto* forward = pageMenu->addAction(tr("下一个视图"), QKeySequence(Qt::ALT | Qt::Key_Right),
                                        navigator, &QPdfPageNavigator::forward);
    back->setEnabled(false);
    forward->setEnabled(false);
    connect(navigator, &QPdfPageNavigator::backAvailableChanged, back, &QAction::setEnabled);
    connect(navigator, &QPdfPageNavigator::forwardAvailableChanged, forward, &QAction::setEnabled);
    pageMenu->addSeparator();
    pageMenu->addAction(m_fitWidthAction);
    pageMenu->addAction(m_fitPageAction);
    pageMenu->addAction(m_actualSizeAction);
    pageMenu->addAction(m_zoomInAction);
    pageMenu->addAction(m_zoomOutAction);

    QMenu* viewMenu = menuBar()->addMenu(tr("视图(&V)"));
    auto* modes = new QActionGroup(this);
    for (const auto mode : {QPdfView::PageMode::SinglePage, QPdfView::PageMode::MultiPage}) {
        const bool continuous = mode == QPdfView::PageMode::MultiPage;
        auto* action = viewMenu->addAction(continuous ? tr("连续页面") : tr("单页"));
        action->setCheckable(true);
        action->setChecked(continuous);
        modes->addAction(action);
        m_documentActions << action;
        connect(action, &QAction::triggered, this, [this, mode]() {
            m_view->setPageMode(mode);
            updateZoomLabel();
        });
    }
    viewMenu->addSeparator();
    auto* navigationToggle = m_navigation->toggleViewAction();
    navigationToggle->setShortcut(QKeySequence(Qt::Key_F4));
    viewMenu->addAction(navigationToggle);
    m_documentActions << navigationToggle;
    auto* fullScreen = viewMenu->addAction(tr("全屏"), QKeySequence(Qt::Key_F11), this, [this]() {
        QWidget* workspace = window();
        workspace->isFullScreen() ? workspace->showNormal() : workspace->showFullScreen();
    });
    m_documentActions << fullScreen;

    QMenu* findMenu = menuBar()->addMenu(tr("编辑(&E)"));
    findMenu->addAction(m_findAction);
    findMenu->addAction(m_findNextAction);
    findMenu->addAction(m_findPrevAction);
    m_documentActions << findMenu->addAction(tr("显示搜索结果"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), this, [this]() {
            onFind();
            m_navigation->showSearchResults();
        });
    auto* copy = findMenu->addAction(tr("复制所选文本"));
    copy->setObjectName(QStringLiteral("ReaderCopySelection"));
    copy->setShortcut(QKeySequence::Copy);
    copy->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    copy->setEnabled(false);
    m_view->addAction(copy);
#ifdef MAPDFR_WITH_MUPDF
    connect(copy, &QAction::triggered, this, &PdfMainWindow::onCopySelection);
    connect(m_view, &SelectablePdfView::selectionChanged, copy, &QAction::setEnabled);
#else
    copy->setToolTip(tr("此构建需要 MuPDF 才能校验 PDF 的复制权限"));
#endif
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QWidget::customContextMenuRequested, this, [this, copy](const QPoint& point) {
        QMenu menu(this);
        menu.addAction(copy);
        menu.exec(m_view->viewport()->mapToGlobal(point));
    });
#ifdef MAPDFR_WITH_MUPDF
    m_documentActions << findMenu->addAction(tr("复制当前页文本"), this,
                                            &PdfMainWindow::onCopyPage);
    QMenu* reviewMenu = menuBar()->addMenu(tr("审阅(&R)"));
    m_documentActions << reviewMenu->addAction(tr("查看当前页便笺…"), this,
                                              &PdfMainWindow::onViewNotes);
    m_documentActions << reviewMenu->addAction(tr("添加便笺并保存副本…"), this,
                                              &PdfMainWindow::onAddNote);
#endif

    QMenu* helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    auto* guide = helpMenu->addAction(tr("用户说明书"), QKeySequence::HelpContents,
                                      this, &PdfMainWindow::onUserGuide);
    guide->setObjectName(QStringLiteral("ReaderUserGuide"));
    helpMenu->addSeparator();

    QAction* registerTypeAction =
        helpMenu->addAction(tr("注册 PDF 文件类型(&R)..."));
    connect(registerTypeAction,
            &QAction::triggered,
            this,
            &PdfMainWindow::onRegisterPdfType);

    QAction* unregisterTypeAction =
        helpMenu->addAction(tr("取消 PDF 文件类型注册(&U)"));
    connect(unregisterTypeAction,
            &QAction::triggered,
            this,
            &PdfMainWindow::onUnregisterPdfType);

    QAction* defaultAppAction =
        helpMenu->addAction(tr("设为默认 PDF 打开程序..."));
    connect(defaultAppAction,
            &QAction::triggered,
            this,
            &PdfMainWindow::onSetDefaultPdfApp);

    helpMenu->addSeparator();

    QAction* aboutAction = helpMenu->addAction(tr("关于 MaPdfR(&A)"));
    connect(aboutAction,
            &QAction::triggered,
            this,
            &PdfMainWindow::onAbout);
}

void PdfMainWindow::loadDocument()
{
    const QFileInfo info(m_filePath);

    if (!info.exists()
        || !info.isFile()
        || !info.isReadable()) {
        m_error = tr("文件不存在或不可读取：%1")
                      .arg(m_filePath);
        return;
    }

    m_sourceCanonicalPath = info.canonicalFilePath();
    m_sourceLastModified = info.lastModified();
    m_sourceSize = info.size();
    m_doc = new QPdfDocument(this);

    QPdfDocument::Error error =
        m_doc->load(m_filePath);

    int retry = 0;

    while (error == QPdfDocument::Error::IncorrectPassword && retry < 3) {
        bool accepted = false;

        const QString password =
            QInputDialog::getText(
                window(),
                tr("需要密码"),
                tr("请输入 PDF 密码："),
                QLineEdit::Password,
                QString(),
                &accepted);

        if (!accepted)
            return;

        m_doc->setPassword(password);
        m_password = password;
        error = m_doc->load(m_filePath);
        ++retry;
    }

    if (error != QPdfDocument::Error::None) {
        m_password.clear();
        switch (error) {
        case QPdfDocument::Error::IncorrectPassword:
            m_error = tr("密码不正确。请确认密码后重新打开。");
            break;
        case QPdfDocument::Error::UnsupportedSecurityScheme:
            m_error = tr("此 PDF 使用了当前阅读器不支持的安全机制。");
            break;
        case QPdfDocument::Error::InvalidFileFormat:
            m_error = tr("文件不是有效的 PDF，或文件已损坏。");
            break;
        default:
            m_error = tr("无法读取 PDF（错误 %1）。").arg(int(error));
            break;
        }
        return;
    }

    const int total = m_doc->pageCount();

    if (total <= 0) {
        m_error = tr("PDF 没有可显示的页面");
        return;
    }

    m_view->setDocument(m_doc);
    m_searchModel->setDocument(m_doc);
    m_navigation->setDocument(m_doc);

    m_pageSpin->setRange(1, total);
    m_pageTotal->setText(
        QStringLiteral(" / %1 ").arg(total));

    if (QPdfPageNavigator* navigator =
            m_view->pageNavigator()) {
        connect(
            navigator,
            &QPdfPageNavigator::currentPageChanged,
            this,
            &PdfMainWindow::onPageNumberChanged);
    }

    m_loaded = true;
    updateZoomLabel();

    statusBar()->showMessage(
        tr("共 %1 页").arg(total),
        3000);
}

bool PdfMainWindow::sourceUnchanged(const QString& title)
{
    const QFileInfo source(m_filePath);
    if (source.isFile() && source.size() == m_sourceSize
        && source.lastModified() == m_sourceLastModified
        && source.canonicalFilePath() == m_sourceCanonicalPath) {
        return true;
    }
    QMessageBox::warning(this, title, tr("源文件已被修改、移动或删除。请重新打开文档后再试。"));
    return false;
}

void PdfMainWindow::onOpen()
{
    const QString startDirectory =
        QFileInfo(m_filePath).absolutePath();

    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("打开 PDF"), startDirectory,
        tr("PDF 文件 (*.pdf);;所有文件 (*.*)"));
    for (const QString& path : paths)
        emit openRequested(path);
}

void PdfMainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls())
        return;

    const QList<QUrl> urls = event->mimeData()->urls();

    if (urls.size() == 1
        && urls.first().isLocalFile()
        && QFileInfo(urls.first().toLocalFile())
               .suffix()
               .compare(QStringLiteral("pdf"),
                        Qt::CaseInsensitive)
               == 0) {
        event->acceptProposedAction();
    }
}

void PdfMainWindow::dropEvent(QDropEvent* event)
{
    const QList<QUrl> urls = event->mimeData()->urls();

    if (urls.size() != 1 || !urls.first().isLocalFile())
        return;

    const QString path = urls.first().toLocalFile();

    if (QFileInfo(path)
            .suffix()
            .compare(QStringLiteral("pdf"),
                     Qt::CaseInsensitive)
        != 0) {
        return;
    }

    event->acceptProposedAction();
    emit openRequested(path);
}

qreal PdfMainWindow::effectiveZoom() const
{
    if (!m_loaded || m_view->zoomMode() == QPdfView::ZoomMode::Custom)
        return m_view->zoomFactor();

    const QScreen* screen = QGuiApplication::primaryScreen();
    const qreal dpiScale = screen ? screen->logicalDotsPerInch() / 72 : 1;
    const QSizeF points = m_doc->pagePointSize(currentPage()) * dpiScale;
    if (points.isEmpty())
        return m_view->zoomFactor();
    const QSize area = m_view->viewport()->size();
    const QMargins margins = m_view->documentMargins();
    const qreal widthRatio = qMax(1, area.width() - margins.left() - margins.right())
        / points.width();
    if (m_view->zoomMode() == QPdfView::ZoomMode::FitToWidth)
        return widthRatio;
    return qMin(widthRatio, qMax(1, area.height() - m_view->pageSpacing()) / points.height());
}

void PdfMainWindow::onZoomIn()
{
    const qreal zoom = std::clamp(effectiveZoom() * 1.25, 0.1, 8.0);
    m_view->setZoomFactor(zoom);
    m_view->setZoomMode(QPdfView::ZoomMode::Custom);
}

void PdfMainWindow::onZoomOut()
{
    const qreal zoom = std::clamp(effectiveZoom() / 1.25, 0.1, 8.0);
    m_view->setZoomFactor(zoom);
    m_view->setZoomMode(QPdfView::ZoomMode::Custom);
}

void PdfMainWindow::onZoomActual()
{
    m_view->setZoomFactor(1.0);
    m_view->setZoomMode(QPdfView::ZoomMode::Custom);
}

void PdfMainWindow::onFitWidth()
{
    m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
}

void PdfMainWindow::onFitPage()
{
    m_view->setZoomMode(QPdfView::ZoomMode::FitInView);
}

int PdfMainWindow::currentPage() const
{
    return m_view->pageNavigator()->currentPage();
}

void PdfMainWindow::onPagePrev()
{
    onPageGoto(currentPage() - 1);
}

void PdfMainWindow::onPageNext()
{
    onPageGoto(currentPage() + 1);
}

void PdfMainWindow::onPageGoto(int page)
{
    if (page != currentPage())
        jumpToPage(page);
}

void PdfMainWindow::jumpToPage(int page, QPointF location, qreal zoom)
{
    if (!m_loaded || page < 0 || page >= m_doc->pageCount())
        return;
    auto* navigator = m_view->pageNavigator();
    navigator->jump(page, location, qFuzzyIsNull(zoom) ? navigator->currentZoom() : zoom);
}

void PdfMainWindow::onPageNumberChanged(int page)
{
    const QSignalBlocker blocker(m_pageSpin);
    m_pageSpin->setValue(page + 1);
    m_prevPageAction->setEnabled(m_loaded && page > 0);
    m_nextPageAction->setEnabled(m_loaded && page + 1 < m_doc->pageCount());
    m_navigation->setCurrentPage(page);
    updateZoomLabel();
}

void PdfMainWindow::onFind()
{
    if (!m_loaded)
        return;

    if (m_findToolBar->isHidden())
        m_findToolBar->show();

    m_findCloseAction->setEnabled(true);
    m_searchEdit->setFocus(Qt::ShortcutFocusReason);
    m_searchEdit->selectAll();
}

void PdfMainWindow::onFindClose()
{
    if (!m_findToolBar || m_findToolBar->isHidden())
        return;

    m_findToolBar->hide();
    m_findCloseAction->setEnabled(false);

    // 清空查找文本会同时清掉 QPdfView 里的高亮。
    if (m_searchEdit)
        m_searchEdit->clear();

    if (m_view)
        m_view->setFocus();
}

void PdfMainWindow::onSearchTextChanged(const QString& text)
{
    if (!m_loaded)
        return;
    m_searchDelay->stop();
    m_searchScan->stop();
    m_selectedSearchResult = QModelIndex();
    m_view->setSearchModel(nullptr);
    m_searchModel->setSearchString(QString());
    if (!text.trimmed().isEmpty())
        m_searchDelay->start();
    updateSearchStatus();
}

void PdfMainWindow::beginSearch()
{
    m_searchDelay->stop();
    m_nextSearchPage = 0;
    m_searchModel->setSearchString(m_searchEdit->text().trimmed());
    m_searchScan->start();
    m_view->setSearchModel(m_searchModel);
    scanSearchPage();
}

void PdfMainWindow::scanSearchPage()
{
    // Qt does not expose a signal for the end of a search. Visit each page
    // through the public cache API to track completion, including zero-hit PDFs.
    const auto pageResults = m_searchModel->resultsOnPage(m_nextSearchPage++);
    if (!m_selectedSearchResult.isValid() && !pageResults.isEmpty())
        selectSearchResult(0);
    if (m_nextSearchPage >= m_doc->pageCount())
        m_searchScan->stop();
    updateSearchStatus();
}

void PdfMainWindow::selectSearchResult(int row)
{
    if (row < 0 || row >= m_searchModel->count())
        return;
    const QPdfLink result = m_searchModel->resultAtIndex(row);
    if (!result.isValid())
        return;
    m_selectedSearchResult = m_searchModel->index(row, 0);
    m_navigation->setCurrentResult(m_selectedSearchResult);
    m_view->setCurrentSearchResultIndex(row);
    jumpToPage(result.page(), result.location());
    updateSearchStatus();
}

void PdfMainWindow::onFindNext()
{
    if (m_searchDelay->isActive()) {
        beginSearch();
        return;
    }
    const int count = m_searchModel->count();
    if (count == 0)
        return;
    selectSearchResult((m_selectedSearchResult.row() + 1) % count);
}

void PdfMainWindow::onFindPrev()
{
    const int count = m_searchModel->count();
    if (count == 0)
        return;
    const int row = m_selectedSearchResult.row();
    selectSearchResult(row <= 0 ? count - 1 : row - 1);
}

void PdfMainWindow::updateSearchStatus()
{
    const int count = m_searchModel->count();
    m_findNextAction->setEnabled(count > 0);
    m_findPrevAction->setEnabled(count > 0);
    if (m_searchEdit->text().trimmed().isEmpty()) {
        m_searchStatus->clear();
        return;
    }
    if (m_searchDelay->isActive()) {
        m_searchStatus->setText(tr("等待搜索…"));
        return;
    }

    const int row = m_selectedSearchResult.row();
    if (row >= 0)
        m_view->setCurrentSearchResultIndex(row);
    QString label = count == 0 ? tr("无结果") : tr("共 %1 项").arg(count);
    if (row >= 0)
        label = QStringLiteral("%1 / %2").arg(row + 1).arg(count);
    if (m_searchScan->isActive()) {
        label = tr("搜索中 · %1 / %2 页 · %3 项")
            .arg(m_nextSearchPage).arg(m_doc->pageCount()).arg(count);
    }
    m_searchStatus->setText(label);
}

void PdfMainWindow::updateZoomLabel()
{
    m_zoomLabel->setText(QStringLiteral("%1%").arg(qMax(1, qRound(effectiveZoom() * 100))));
}

void PdfMainWindow::showDocumentProperties()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("文档属性"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    const auto addRow = [&](const QString& name, const QString& value) {
        auto* field = new QLineEdit(value.isEmpty() ? tr("未指定") : value, &dialog);
        field->setReadOnly(true);
        field->setCursorPosition(0);
        form->addRow(name, field);
    };

    using Field = QPdfDocument::MetaDataField;
    addRow(tr("文件"), QDir::toNativeSeparators(QFileInfo(m_filePath).absoluteFilePath()));
    addRow(tr("文件大小"), QLocale().formattedDataSize(QFileInfo(m_filePath).size()));
    addRow(tr("页数"), QString::number(m_doc->pageCount()));
    addRow(tr("标题"), m_doc->metaData(Field::Title).toString());
    addRow(tr("作者"), m_doc->metaData(Field::Author).toString());
    addRow(tr("主题"), m_doc->metaData(Field::Subject).toString());
    addRow(tr("关键字"), m_doc->metaData(Field::Keywords).toString());
    addRow(tr("创建工具"), m_doc->metaData(Field::Creator).toString());
    addRow(tr("PDF 生成器"), m_doc->metaData(Field::Producer).toString());
    for (const auto field : {Field::CreationDate, Field::ModificationDate}) {
        const QDateTime date = m_doc->metaData(field).toDateTime();
        addRow(field == Field::CreationDate ? tr("创建时间") : tr("修改时间"),
               date.isValid() ? QLocale().toString(date.toLocalTime(), QLocale::ShortFormat)
                              : QString());
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(560, dialog.sizeHint().height());
    dialog.exec();
}

void PdfMainWindow::onSaveAs()
{
    const QString defaultDirectory =
        QStandardPaths::writableLocation(
            QStandardPaths::DocumentsLocation);

    // 用展示名推导建议文件名：Office 文档转换来的 PDF 是临时产物，
    // 拿它的名字（报告.docx.pdf 或随机名）做默认值会很别扭。
    const QString suggestedName =
        QFileInfo(m_displayName).completeBaseName()
        + QStringLiteral(".pdf");

    const QString outputPath =
        QFileDialog::getSaveFileName(
            this,
            tr("保存 PDF"),
            QDir(defaultDirectory).filePath(suggestedName),
            tr("PDF 文件 (*.pdf)"));

    if (outputPath.isEmpty() || !sourceUnchanged(tr("无法另存 PDF")))
        return;

    QString error;

    if (!Reader::copyFileAtomically(
            m_filePath, outputPath, &error)) {
        QMessageBox::critical(
            this,
            tr("保存失败"),
            tr("无法写入：%1\n%2")
                .arg(outputPath, error));
        return;
    }

    statusBar()->showMessage(
        tr("已保存到 %1").arg(outputPath),
        3000);
}

QString PdfMainWindow::ensurePdfIconFile()
{
#ifdef Q_OS_WIN
    const QString bundled = QDir(
        QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("pdf.ico"));

    if (QFileInfo::exists(bundled))
        return bundled;

    // 便携版可能只有 EXE：把嵌入的 pdf.ico 释放到用户本地数据目录，
    // 供注册表的 DefaultIcon 引用（qrc 路径在注册表里不可用）。
    const QString dataDir =
        QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation);

    if (dataDir.isEmpty())
        return QString();

    if (!QDir().mkpath(dataDir))
        return QString();

    const QString extracted =
        QDir(dataDir).filePath(QStringLiteral("pdf.ico"));

    if (!QFileInfo::exists(extracted)) {
        if (!QFile::copy(QStringLiteral(":/pdf.ico"), extracted))
            return QString();
    }

    return extracted;
#else
    // 桌面项（Desktop Entry）的 Icon 只能是主题名或真实文件路径，
    // qrc 路径不可用。用 256×256 的 PNG 释放到应用数据目录。
    const QString bundled = QDir(
        QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("mapdfr.png"));

    if (QFileInfo::exists(bundled))
        return bundled;

    const QString dataDir =
        QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation);

    if (dataDir.isEmpty() || !QDir().mkpath(dataDir))
        return QString();

    const QString extracted =
        QDir(dataDir).filePath(QStringLiteral("mapdfr.png"));

    if (!QFileInfo::exists(extracted)) {
        if (!QFile::copy(QStringLiteral(":/app.png"), extracted))
            return QString();
    }

    return extracted;
#endif
}

void PdfMainWindow::onRegisterPdfType()
{
    const QString exePath = QCoreApplication::applicationFilePath();
    QString error;

    if (!MaPdfRFileType::registerPdfType(
            exePath, ensurePdfIconFile(), error)) {
        QMessageBox::critical(
            this,
            tr("注册 PDF 文件类型失败"),
            tr("无法注册 PDF 文件类型：\n%1").arg(error));
        return;
    }

    statusBar()->showMessage(
        tr("已注册 PDF 文件类型：%1").arg(exePath), 5000);

#ifdef Q_OS_WIN
    const QMessageBox::StandardButton answer =
        QMessageBox::question(
            this,
            tr("注册 PDF 文件类型"),
            tr("已把 MaPdfR 加入 PDF 的「打开方式」列表。\n\n"
               "Windows 不允许程序静默修改默认打开程序，"
               "是否现在打开系统的「打开方式」对话框，"
               "勾选「始终使用此应用」把 MaPdfR 设为默认？"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);

    if (answer == QMessageBox::Yes)
        onSetDefaultPdfApp();
#else
    const QMessageBox::StandardButton answer =
        QMessageBox::question(
            this,
            tr("注册 PDF 文件类型"),
            tr("已把 MaPdfR 加入 PDF 的「打开方式」列表。\n\n"
               "是否现在把 MaPdfR 设为 PDF 的默认打开程序？"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);

    if (answer == QMessageBox::Yes)
        onSetDefaultPdfApp();
#endif
}

void PdfMainWindow::onUnregisterPdfType()
{
#ifdef Q_OS_WIN
    const QString question =
        tr("将从当前用户注册表删除 MaPdfR 的 PDF 文件类型注册。\n"
           "已有的 PDF 文件不会被删除或修改。是否继续？");
#elif defined(Q_OS_MACOS)
    const QString question =
        tr("将取消 MaPdfR 作为 PDF 默认打开程序的登记。\n"
           "已有的 PDF 文件不会被删除或修改。是否继续？");
#else
    const QString question =
        tr("将删除 MaPdfR 的用户桌面项，并从 PDF 的关联列表中移除。\n"
           "已有的 PDF 文件不会被删除或修改。是否继续？");
#endif

    const QMessageBox::StandardButton answer =
        QMessageBox::question(
            this,
            tr("取消 PDF 文件类型注册"),
            question,
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);

    if (answer != QMessageBox::Yes)
        return;

    QString error;

    if (!MaPdfRFileType::unregisterPdfType(error)) {
        QMessageBox::critical(
            this,
            tr("取消注册失败"),
            error);
        return;
    }

    statusBar()->showMessage(tr("已取消 PDF 文件类型注册"), 5000);
}

void PdfMainWindow::onSetDefaultPdfApp()
{
    // 系统的「打开方式」对话框只列出已注册的程序，先确保注册存在。
    QString error;

    if (!MaPdfRFileType::registerPdfType(
            QCoreApplication::applicationFilePath(),
            ensurePdfIconFile(),
            error)) {
        QMessageBox::critical(
            this,
            tr("注册 PDF 文件类型失败"),
            tr("无法写入当前用户注册表：\n%1").arg(error));
        return;
    }

    if (!MaPdfRFileType::openWithDialog(error)) {
        if (error == tr("已取消")) {
            statusBar()->showMessage(tr("已取消设置默认程序"), 3000);
            return;
        }

#ifdef Q_OS_WIN
        const QString hint =
            tr("也可以在 Windows「设置 → 应用 → 默认应用」中"
               "手动为 .pdf 选择 MaPdfR。");
#elif defined(Q_OS_MACOS)
        const QString hint =
            tr("也可以在 Finder 中右键 PDF 文件 → 显示简介 → 打开方式，"
               "选择 MaPdfR 并点「全部更改」。");
#else
        const QString hint =
            tr("也可以在系统设置的「默认应用」中手动为 PDF 选择 MaPdfR。");
#endif
        QMessageBox::critical(
            this,
            tr("无法设置默认程序"),
            tr("%1\n\n%2").arg(error, hint));
        return;
    }

    statusBar()->showMessage(tr("已完成默认程序选择"), 5000);
}

void PdfMainWindow::onUserGuide()
{
    if (m_userGuide) {
        m_userGuide->showNormal();
        m_userGuide->raise();
        m_userGuide->activateWindow();
        return;
    }

    auto* dialog = new QDialog(this);
    m_userGuide = dialog;
    dialog->setObjectName(QStringLiteral("ReaderUserGuideDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("MaPdfR 用户说明书"));
    dialog->resize(780, 670);

    auto* layout = new QVBoxLayout(dialog);
    auto* browser = new QTextBrowser(dialog);
    browser->setAccessibleName(tr("用户说明书正文"));
    browser->setOpenExternalLinks(false);
    browser->setSource(QUrl(QStringLiteral("qrc:/help/user-guide.zh_CN.html")));
    layout->addWidget(browser);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->button(QDialogButtonBox::Close)->setText(tr("关闭"));
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QWidget::close);
    layout->addWidget(buttons);
    dialog->show();
}

void PdfMainWindow::onAbout()
{
    AboutDialog dialog(this);
    dialog.exec();
}

#ifdef MAPDFR_WITH_MUPDF
bool PdfMainWindow::openForOperation(Reader::PdfDocumentOperations& operation, const QString& title)
{
    QString error;
    if (!sourceUnchanged(title))
        return false;
    if (operation.open(m_filePath, m_password, error) && operation.pageCount() == m_doc->pageCount())
        return sourceUnchanged(title);

    if (error.isEmpty())
        error = tr("文件已被修改。请重新打开文档后再试。");
    QMessageBox::warning(this, title, error);
    return false;
}

void PdfMainWindow::onPrint()
{
    if (!m_loaded || m_busy)
        return;
    QScopedValueRollback<bool> busy(m_busy, true);
    Reader::PdfDocumentOperations operation;
    if (!openForOperation(operation, tr("无法打印")))
        return;
    if (!operation.canPrint()) {
        QMessageBox::warning(this, tr("无法打印"), tr("文档禁止打印。"));
        return;
    }
    Reader::printDocument(this, operation, currentPage(), m_displayName);
}

void PdfMainWindow::onCopySelection()
{
    const QString text = m_view->selectedText();
    if (!m_loaded || m_busy || text.isEmpty())
        return;

    QScopedValueRollback<bool> busy(m_busy, true);
    Reader::PdfDocumentOperations operation;
    if (!openForOperation(operation, tr("无法复制文本")))
        return;
    if (!operation.canCopy()) {
        statusBar()->showMessage(tr("此 PDF 限制了文本复制"), 5000);
        return;
    }
    QApplication::clipboard()->setText(text);
    statusBar()->showMessage(tr("已复制所选文本"), 3500);
}

void PdfMainWindow::onCopyPage()
{
    if (!m_loaded || m_busy)
        return;
    QScopedValueRollback<bool> busy(m_busy, true);
    Reader::PdfDocumentOperations operation;
    if (!openForOperation(operation, tr("无法复制文本")))
        return;

    QString error;
    const QString text = operation.pageText(currentPage(), error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("无法复制文本"), error);
    } else if (text.trimmed().isEmpty()) {
        statusBar()->showMessage(tr("当前页没有可复制的文字"), 3500);
    } else {
        QApplication::clipboard()->setText(text);
        statusBar()->showMessage(tr("已复制第 %1 页文本").arg(currentPage() + 1), 3500);
    }
}

void PdfMainWindow::onViewNotes()
{
    if (!m_loaded || m_busy)
        return;
    QScopedValueRollback<bool> busy(m_busy, true);
    Reader::PdfDocumentOperations operation;
    if (openForOperation(operation, tr("无法读取便笺")))
        Reader::showPageNotes(this, operation, currentPage());
}

void PdfMainWindow::onAddNote()
{
    if (!m_loaded || m_busy)
        return;

    QString path;
    {
        QScopedValueRollback<bool> busy(m_busy, true);
        Reader::PdfDocumentOperations operation;
        if (!openForOperation(operation, tr("无法添加批注")))
            return;
        if (!operation.canAnnotate()) {
            QMessageBox::warning(this, tr("无法添加批注"),
                tr("文档限制了批注，或含有签名字段、认证或使用权限。"));
            return;
        }
        path = Reader::createReviewCopy(this, operation, currentPage(), m_filePath);
    }
    if (!path.isEmpty()) {
        statusBar()->showMessage(tr("已保存批注副本：%1").arg(path), 5000);
        emit openRequested(path);
    }
}

void PdfMainWindow::onExportMarkdown()
{
    if (!m_loaded || m_filePath.isEmpty() || m_busy)
        return;

    QScopedValueRollback<bool> busy(m_busy, true);

    const QFileInfo pdfInfo(m_filePath);
    const QString suggestedName =
        pdfInfo.completeBaseName() + QStringLiteral(".md");

    QFileDialog output(this, tr("导出为 Markdown"),
                       QDir(pdfInfo.absolutePath()).filePath(suggestedName),
                       tr("Markdown 文件 (*.md)"));
    output.setAcceptMode(QFileDialog::AcceptSave);
    output.setDefaultSuffix(QStringLiteral("md"));
    if (output.exec() != QDialog::Accepted)
        return;
    const QString outputPath = output.selectedFiles().constFirst();
    if (!sourceUnchanged(tr("无法导出")))
        return;

    QProgressDialog progress(tr("正在解析 PDF..."), tr("取消"), 0, 0, this);
    progress.setWindowTitle(tr("导出 Markdown"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setValue(0);
    progress.show();

    Reader::MarkdownExportResult result;
    QString error;

    const bool ok = Reader::exportPdfToMarkdown(
        m_filePath,
        outputPath,
        m_password,
        [&progress](int done, int total) {
            if (progress.maximum() != total)
                progress.setRange(0, total);
            progress.setValue(done);
            QApplication::processEvents();
            return !progress.wasCanceled();
        },
        &result,
        &error);

    progress.close();

    if (!ok) {
        if (progress.wasCanceled()) {
            statusBar()->showMessage(tr("已取消导出 Markdown"), 3000);
            return;
        }

        QMessageBox::critical(
            this,
            tr("导出 Markdown 失败"),
            tr("无法把 PDF 转换为 Markdown：\n%1").arg(error));
        return;
    }

    QString message = tr("已导出 %1 页").arg(result.pageCount);
    if (result.imageCount > 0)
        message += tr("、%1 张图片").arg(result.imageCount);
    message += QStringLiteral("：\n") + outputPath;

    statusBar()->showMessage(
        tr("Markdown 已保存到 %1").arg(outputPath),
        5000);

    QMessageBox::information(this, tr("导出 Markdown"), message);
}
#endif
