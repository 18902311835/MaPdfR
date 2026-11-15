#include "ReaderNavigation.h"

#include <QAbstractListModel>
#include <QApplication>
#include <QCache>
#include <QCoreApplication>
#include <QIcon>
#include <QIdentityProxyModel>
#include <QLabel>
#include <QListView>
#include <QPainter>
#include <QPdfBookmarkModel>
#include <QPdfDocument>
#include <QPdfPageRenderer>
#include <QPdfSearchModel>
#include <QPixmap>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTimer>
#include <QTreeView>

namespace {

const QSize ThumbnailSize(112, 144);

class ThumbnailDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override
    {
        return QSize(ThumbnailSize.width() + 24,
                     ThumbnailSize.height() + option.fontMetrics.height() + 22);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        const QStyle* style = option.widget ? option.widget->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &option, painter, option.widget);

        const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        const QSize size = icon.isNull() ? QSize(92, 130) : icon.actualSize(ThumbnailSize);
        const QRect page(QPoint(option.rect.center().x() - size.width() / 2, option.rect.top() + 8)
                         + QPoint(0, (ThumbnailSize.height() - size.height()) / 2), size);
        painter->fillRect(page, Qt::white);
        icon.paint(painter, page);
        const bool selected = option.state.testFlag(QStyle::State_Selected);

        painter->save();
        painter->setPen(option.palette.color(selected ? QPalette::HighlightedText : QPalette::Mid));
        painter->drawRect(page.adjusted(0, 0, -1, -1));
        painter->setPen(option.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
        painter->setFont(option.font);
        const QRect caption = option.rect.adjusted(5, ThumbnailSize.height() + 10, -5, -4);
        painter->drawText(caption, Qt::AlignCenter,
            option.fontMetrics.elidedText(index.data().toString(), Qt::ElideRight, caption.width()));
        painter->restore();
    }
};

class SearchResultLabels : public QIdentityProxyModel
{
public:
    explicit SearchResultLabels(QPdfSearchModel* search, QObject* parent)
        : QIdentityProxyModel(parent), m_search(search)
    {
        setSourceModel(search);
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || (role != Qt::DisplayRole && role != Qt::ToolTipRole
                                 && role != Qt::AccessibleTextRole)) {
            return QIdentityProxyModel::data(index, role);
        }
        using Role = QPdfSearchModel::Role;
        const QModelIndex source = mapToSource(index);
        const int page = m_search->data(source, int(Role::Page)).toInt();
        const QString context = m_search->data(source, int(Role::ContextBefore)).toString()
            + m_search->searchString()
            + m_search->data(source, int(Role::ContextAfter)).toString();
        const QString label = QCoreApplication::translate("ReaderNavigation", "第 %1 页｜%2")
            .arg(page + 1).arg(context.simplified());
        return role == Qt::ToolTipRole ? label.toHtmlEscaped() : label;
    }

private:
    QPdfSearchModel* m_search;
};

}

class ReaderNavigation::ThumbnailModel : public QAbstractListModel
{
public:
    explicit ThumbnailModel(QObject* parent)
        : QAbstractListModel(parent), m_renderer(new QPdfPageRenderer(this))
    {
        m_renderer->setRenderMode(QPdfPageRenderer::RenderMode::MultiThreaded);
        m_schedule.setSingleShot(true);
        connect(&m_schedule, &QTimer::timeout, this, [this]() { requestNext(); });
        connect(m_renderer, &QPdfPageRenderer::pageRendered, this,
                [this](int page, QSize, const QImage& image, QPdfDocumentRenderOptions,
                       quint64 request) {
                    if (!m_document || m_activePage != page || request != m_request)
                        return;
                    m_activePage = -1;
                    m_cache.insert(page, new QIcon(QPixmap::fromImage(image)));
                    const QModelIndex changed = index(page, 0);
                    emit dataChanged(changed, changed, {Qt::DecorationRole});
                    requestNext();
                });
    }

    void setDocument(QPdfDocument* document)
    {
        beginResetModel();
        m_schedule.stop();
        m_activePage = -1;
        m_pending.clear();
        m_cache.clear();
        m_document = document;
        m_renderer->setDocument(document);
        endResetModel();
    }

    int rowCount(const QModelIndex& parent) const override
    {
        return !parent.isValid() && m_document ? m_document->pageCount() : 0;
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() >= rowCount({}))
            return {};
        const int page = index.row();
        if (role == Qt::DecorationRole) {
            if (const QIcon* icon = m_cache.object(page))
                return *icon;
            if (page != m_activePage && !m_pending.contains(page)) {
                // Keep work bounded when a user drags the scrollbar across a long file.
                if (m_pending.size() == 12)
                    m_pending.removeFirst();
                m_pending.append(page);
                if (m_activePage < 0 && !m_schedule.isActive())
                    m_schedule.start(0);
            }
            return {};
        }
        if (role == Qt::DisplayRole || role == Qt::AccessibleTextRole) {
            const QString label = m_document->pageLabel(page);
            return label.isEmpty() ? QString::number(page + 1) : label;
        }
        return {};
    }

private:
    void requestNext()
    {
        while (m_document && m_activePage < 0 && !m_pending.isEmpty()) {
            const int page = m_pending.takeFirst();
            const QSizeF points = m_document->pagePointSize(page);
            if (!points.isValid() || points.isEmpty())
                continue;
            const QSizeF fitted = points.scaled(QSizeF(ThumbnailSize) * 2, Qt::KeepAspectRatio);
            const QSize pixels = fitted.toSize().expandedTo(QSize(1, 1));
            m_activePage = page;
            m_request = m_renderer->requestPage(page, pixels);
        }
    }

    QPdfDocument* m_document = nullptr;
    QPdfPageRenderer* m_renderer;
    mutable QCache<int, QIcon> m_cache{48};
    mutable QList<int> m_pending;
    mutable QTimer m_schedule;
    int m_activePage = -1;
    quint64 m_request = 0;
};

ReaderNavigation::ReaderNavigation(QPdfSearchModel* search, QWidget* parent)
    : QDockWidget(tr("导航"), parent),
      m_tabs(new QTabWidget(this)),
      m_outline(new QStackedWidget(m_tabs)),
      m_bookmarks(new QPdfBookmarkModel(this)),
      m_bookmarkView(new QTreeView(m_outline)),
      m_thumbnails(new ThumbnailModel(this)),
      m_thumbnailView(new QListView(m_tabs)),
      m_searchView(new QListView(m_tabs))
{
    setObjectName(QStringLiteral("ReaderNavigation"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setWidget(m_tabs);
    m_tabs->setMinimumWidth(245);

    auto* noOutline = new QLabel(tr("此文档没有书签"), m_outline);
    noOutline->setAlignment(Qt::AlignCenter);
    m_outline->addWidget(m_bookmarkView);
    m_outline->addWidget(noOutline);
    m_outline->setCurrentIndex(1);
    m_bookmarkView->setObjectName(QStringLiteral("ReaderBookmarks"));
    m_bookmarkView->setAccessibleName(tr("文档书签"));
    m_bookmarkView->setHeaderHidden(true);
    m_bookmarkView->setUniformRowHeights(true);
    m_bookmarkView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_bookmarkView->setModel(m_bookmarks);

    m_thumbnailView->setObjectName(QStringLiteral("ReaderThumbnails"));
    m_thumbnailView->setAccessibleName(tr("页面缩略图"));
    m_thumbnailView->setItemDelegate(new ThumbnailDelegate(m_thumbnailView));
    m_thumbnailView->setResizeMode(QListView::Adjust);
    m_thumbnailView->setLayoutMode(QListView::Batched);
    m_thumbnailView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_thumbnailView->setUniformItemSizes(true);
    m_thumbnailView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_thumbnailView->setModel(m_thumbnails);

    m_searchView->setObjectName(QStringLiteral("ReaderSearchResults"));
    m_searchView->setAccessibleName(tr("文档搜索结果"));
    m_searchView->setUniformItemSizes(true);
    m_searchView->setModel(new SearchResultLabels(search, m_searchView));

    m_tabs->addTab(m_outline, tr("书签"));
    m_tabs->addTab(m_thumbnailView, tr("页面"));
    m_tabs->addTab(m_searchView, tr("搜索结果"));

    const auto activateBookmark = [this](const QModelIndex& index) {
        using Role = QPdfBookmarkModel::Role;
        const QVariant page = index.data(int(Role::Page));
        if (page.isValid()) {
            emit bookmarkRequested(page.toInt(), index.data(int(Role::Location)).toPointF(),
                                   index.data(int(Role::Zoom)).toReal());
        }
    };
    connect(m_bookmarkView, &QTreeView::clicked, this, activateBookmark);
    connect(m_bookmarkView, &QTreeView::activated, this, activateBookmark);
    const auto activatePage = [this](const QModelIndex& i) { emit pageRequested(i.row()); };
    connect(m_thumbnailView, &QListView::clicked, this, activatePage);
    connect(m_thumbnailView, &QListView::activated, this, activatePage);
    const auto activateResult = [this](const QModelIndex& i) { emit searchResultRequested(i.row()); };
    connect(m_searchView, &QListView::clicked, this, activateResult);
    connect(m_searchView, &QListView::activated, this, activateResult);
}

ReaderNavigation::~ReaderNavigation()
{
    setDocument(nullptr);
}

void ReaderNavigation::setDocument(QPdfDocument* document)
{
    m_bookmarks->setDocument(document);
    m_thumbnails->setDocument(document);
    const bool hasBookmarks = m_bookmarks->rowCount() > 0;
    m_outline->setCurrentIndex(hasBookmarks ? 0 : 1);
    m_tabs->setCurrentIndex(hasBookmarks ? 0 : 1);
    if (document)
        setCurrentPage(0);
}

void ReaderNavigation::setCurrentPage(int page)
{
    const QModelIndex index = m_thumbnails->index(page, 0);
    if (!index.isValid())
        return;
    m_thumbnailView->setCurrentIndex(index);
    if (m_tabs->currentWidget() == m_thumbnailView && isVisible())
        m_thumbnailView->scrollTo(index);
}

void ReaderNavigation::setCurrentResult(const QModelIndex& index)
{
    auto* labels = static_cast<SearchResultLabels*>(m_searchView->model());
    const QModelIndex mapped = labels->mapFromSource(index);
    m_searchView->setCurrentIndex(mapped);
    if (mapped.isValid() && m_tabs->currentWidget() == m_searchView && isVisible())
        m_searchView->scrollTo(mapped);
}

void ReaderNavigation::showSearchResults()
{
    m_tabs->setCurrentWidget(m_searchView);
    show();
    raise();
}
