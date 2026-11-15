#pragma once

#include <QDateTime>
#include <QMainWindow>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QString>

namespace Reader { class PdfDocumentOperations; }

class QAction;
class QDialog;
class QLabel;
class QLineEdit;
class QPdfDocument;
class QPdfSearchModel;
class SelectablePdfView;
class QSpinBox;
class QTimer;
class QToolBar;
class ReaderNavigation;

class PdfMainWindow : public QMainWindow {
    Q_OBJECT

public:
    // displayName 为可选的显示名。由 Office 文档转换而来时，
    // 标题栏与「另存为」应当用原始文档名，而不是临时产物的 report.pdf。
    explicit PdfMainWindow(const QString& filePath,
                           QWidget* parent = nullptr,
                           const QString& displayName = QString());
    ~PdfMainWindow() override;

    bool isLoaded() const
    {
        return m_loaded;
    }

    bool isBusy() const { return m_busy; }
    void focusDocument();

    QString errorString() const
    {
        return m_error;
    }

signals:
    // 用户在本窗口选择打开另一个 PDF 时发出。
    // 由工作区在切换或新建标签时保留当前文档状态。
    void openRequested(const QString& filePath);
    void closeRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onOpen();
    void onZoomIn();
    void onZoomOut();
    void onZoomActual();
    void onFitWidth();
    void onFitPage();
    void onPagePrev();
    void onPageNext();
    void onPageGoto(int page);
    void onPageNumberChanged(int page);
    void onFind();
    void onFindNext();
    void onFindPrev();
    void onFindClose();
    void onSearchTextChanged(const QString& text);
    void onSaveAs();
    void onRegisterPdfType();
    void onUnregisterPdfType();
    void onSetDefaultPdfApp();
    void onUserGuide();
    void onAbout();
#ifdef MAPDFR_WITH_MUPDF
    void onExportMarkdown();
    void onPrint();
    void onCopyPage();
    void onCopySelection();
    void onAddNote();
    void onViewNotes();
    bool openForOperation(Reader::PdfDocumentOperations& operation, const QString& title);
#endif

private:
    void buildUi();
    void buildFindBar();
    void buildMenus();
    void loadDocument();
    bool sourceUnchanged(const QString& title);
    void showDocumentProperties();
    void jumpToPage(int page, QPointF location = {}, qreal zoom = 0);
    int currentPage() const;
    qreal effectiveZoom() const;
    void beginSearch();
    void scanSearchPage();
    void selectSearchResult(int row);
    // 返回注册 PDF 文件类型用的图标路径；发行目录里没有 pdf.ico 时，
    // 从嵌入资源释放到用户本地数据目录。失败返回空（回退到 EXE 图标）。
    QString ensurePdfIconFile();
    void updateSearchStatus();
    void updateZoomLabel();

    QString m_filePath;
    QString m_displayName;
    QString m_error;
    QString m_sourceCanonicalPath;
    QDateTime m_sourceLastModified;
    qint64 m_sourceSize = -1;
    // 打开加密 PDF 时用户输入的密码，导出 Markdown 时复用。
    QString m_password;
    bool m_loaded = false;
    bool m_busy = false;

    QPdfDocument* m_doc = nullptr;
    SelectablePdfView* m_view = nullptr;
    QSpinBox* m_pageSpin = nullptr;
    QLabel* m_pageTotal = nullptr;
    ReaderNavigation* m_navigation = nullptr;
    QPointer<QDialog> m_userGuide;

    // 工具栏上的当前缩放比例，随 zoomFactor 变化实时更新。
    QLabel* m_zoomLabel = nullptr;

    // 查找栏：默认隐藏，Ctrl+F 显示；命中结果由 QPdfView 高亮。
    QToolBar* m_findToolBar = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QLabel* m_searchStatus = nullptr;
    QPdfSearchModel* m_searchModel = nullptr;
    QTimer* m_searchDelay = nullptr;
    QTimer* m_searchScan = nullptr;
    int m_nextSearchPage = 0;
    QPersistentModelIndex m_selectedSearchResult;
    QList<QAction*> m_documentActions;

    // 工具栏与菜单共用同一批 QAction，避免同一快捷键被注册两次
    // （Qt 会把重复注册报成 ambiguous shortcut）。
    QAction* m_openAction = nullptr;
    QAction* m_saveAction = nullptr;
#ifdef MAPDFR_WITH_MUPDF
    QAction* m_exportMarkdownAction = nullptr;
    QAction* m_printAction = nullptr;
#endif
    QAction* m_prevPageAction = nullptr;
    QAction* m_nextPageAction = nullptr;
    QAction* m_zoomInAction = nullptr;
    QAction* m_zoomOutAction = nullptr;
    QAction* m_actualSizeAction = nullptr;
    QAction* m_fitWidthAction = nullptr;
    QAction* m_fitPageAction = nullptr;
    QAction* m_findAction = nullptr;
    QAction* m_findNextAction = nullptr;
    QAction* m_findPrevAction = nullptr;
    QAction* m_findCloseAction = nullptr;
};
