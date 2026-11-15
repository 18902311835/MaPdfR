#pragma once

#include <QDockWidget>

class QPdfBookmarkModel;
class QPdfDocument;
class QPdfSearchModel;
class QListView;
class QModelIndex;
class QStackedWidget;
class QTabWidget;
class QTreeView;

class ReaderNavigation : public QDockWidget
{
    Q_OBJECT

public:
    explicit ReaderNavigation(QPdfSearchModel* search, QWidget* parent);
    ~ReaderNavigation() override;

    void setDocument(QPdfDocument* document);
    void setCurrentPage(int page);
    void setCurrentResult(const QModelIndex& index);
    void showSearchResults();

signals:
    void pageRequested(int page);
    void bookmarkRequested(int page, QPointF location, qreal zoom);
    void searchResultRequested(int row);

private:
    class ThumbnailModel;
    QTabWidget* m_tabs;
    QStackedWidget* m_outline;
    QPdfBookmarkModel* m_bookmarks;
    QTreeView* m_bookmarkView;
    ThumbnailModel* m_thumbnails;
    QListView* m_thumbnailView;
    QListView* m_searchView;
};
