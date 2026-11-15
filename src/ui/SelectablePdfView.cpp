#include "SelectablePdfView.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QScreen>
#include <QScrollBar>
#include <QSet>
#include <algorithm>
#include <cmath>

SelectablePdfView::SelectablePdfView(QWidget* parent)
    : QPdfView(parent),
      m_dpiScale(QGuiApplication::primaryScreen()->logicalDotsPerInch() / 72.0)
{
    setFocusPolicy(Qt::StrongFocus);
    const auto invalidate = [this]() { invalidatePageLayout(); };
    connect(this, &QPdfView::zoomModeChanged, this, invalidate);
    connect(this, &QPdfView::zoomFactorChanged, this, invalidate);
    connect(this, &QPdfView::pageSpacingChanged, this, invalidate);
    connect(this, &QPdfView::documentMarginsChanged, this, invalidate);
    connect(this, &QPdfView::pageModeChanged, this, invalidate);
    connect(pageNavigator(), &QPdfPageNavigator::currentPageChanged, this, [this]() {
        if (pageMode() == PageMode::SinglePage)
            invalidatePageLayout();
    });
    connect(this, &QPdfView::documentChanged, this, [this](QPdfDocument* document) {
        disconnect(m_documentStatus);
        clearSelection();
        invalidatePageLayout();
        if (document) {
            m_documentStatus = connect(document, &QPdfDocument::statusChanged, this,
                [this](QPdfDocument::Status status) {
                    if (status != QPdfDocument::Status::Ready)
                        clearSelection();
                    invalidatePageLayout();
                });
        }
    });

    m_autoScroll.setInterval(24);
    connect(&m_autoScroll, &QTimer::timeout, this, &SelectablePdfView::updateAutoScroll);
}

QString SelectablePdfView::selectedText() const
{
    return m_selection ? m_selection->text() : QString();
}

void SelectablePdfView::clearSelection()
{
    m_autoScroll.stop();
    m_dragging = false;
    m_selectionPending = false;
    m_anchorPage = -1;
    const bool hadSelection = !selectedText().isEmpty();
    m_selection.reset();
    m_selectionPage = -1;
    if (hadSelection) {
        viewport()->update();
        emit selectionChanged(false);
    }
}

void SelectablePdfView::invalidatePageLayout()
{
    m_layoutDirty = true;
    if (m_selectionPage >= 0)
        viewport()->update();
}

void SelectablePdfView::preparePageLayout()
{
    if (!m_layoutDirty && m_layoutViewport == viewport()->size())
        return;
    m_layoutDirty = false;
    m_layoutViewport = viewport()->size();
    m_pageRects.clear();
    m_pagePoints.clear();
    if (!document() || document()->status() != QPdfDocument::Status::Ready)
        return;

    const int pageCount = document()->pageCount();
    const bool single = pageMode() == PageMode::SinglePage;
    m_firstPage = single ? pageNavigator()->currentPage() : 0;
    if (m_firstPage < 0 || m_firstPage >= pageCount)
        return;
    const int endPage = single ? m_firstPage + 1 : pageCount;
    const QMargins margins = documentMargins();
    const int availableWidth = qMax(1, m_layoutViewport.width() - margins.left() - margins.right());
    const QSize available(availableWidth, qMax(1, m_layoutViewport.height() - pageSpacing()));
    m_pageRects.reserve(endPage - m_firstPage);
    m_pagePoints.reserve(endPage - m_firstPage);

    int widest = 0;
    int top = margins.top();
    for (int page = m_firstPage; page < endPage; ++page) {
        const QSizeF points = document()->pagePointSize(page);
        m_pagePoints.append(points);
        QSize pixels(0, 0);
        if (!points.isEmpty()) {
            const QSize actual = (points * m_dpiScale).toSize();
            switch (zoomMode()) {
            case ZoomMode::Custom:
                pixels = (points * m_dpiScale * zoomFactor()).toSize();
                break;
            case ZoomMode::FitToWidth:
                if (actual.width() > 0)
                    pixels = actual * (qreal(availableWidth) / actual.width());
                break;
            case ZoomMode::FitInView:
                pixels = actual.scaled(available, Qt::KeepAspectRatio);
                break;
            }
        }
        m_pageRects.append(QRect(QPoint(0, top), pixels));
        widest = qMax(widest, pixels.width());
        top += pixels.height() + pageSpacing();
    }

    const int width = qMax(m_layoutViewport.width(), widest + margins.left() + margins.right());
    for (QRect& rect : m_pageRects)
        rect.moveLeft((width - rect.width()) / 2);
}

QTransform SelectablePdfView::pageTransform(int page) const
{
    const int index = page - m_firstPage;
    if (index < 0 || index >= m_pageRects.size() || m_pageRects.at(index).isEmpty())
        return QTransform::fromScale(0, 0);
    const QRect rect = m_pageRects.at(index);
    const QSizeF points = m_pagePoints.at(index);
    QTransform transform;
    transform.translate(rect.x() - horizontalScrollBar()->value(),
                        rect.y() - verticalScrollBar()->value());
    transform.scale(rect.width() / points.width(), rect.height() / points.height());
    return transform;
}

int SelectablePdfView::pageAt(QPointF viewportPoint)
{
    preparePageLayout();
    const QPointF documentPoint = viewportPoint
        + QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value());
    const auto page = std::upper_bound(m_pageRects.cbegin(), m_pageRects.cend(), documentPoint.y(),
                                      [](qreal y, const QRect& rect) { return y < rect.top(); });
    if (page == m_pageRects.cbegin())
        return -1;
    const auto candidate = std::prev(page);
    return QRectF(*candidate).contains(documentPoint)
        ? m_firstPage + static_cast<int>(candidate - m_pageRects.cbegin())
        : -1;
}

void SelectablePdfView::paintEvent(QPaintEvent* event)
{
    QPdfView::paintEvent(event);
    if (m_selectionPage < 0)
        return;
    preparePageLayout();
    const QTransform transform = pageTransform(m_selectionPage);
    if (!transform.isInvertible())
        return;

    QPainter painter(viewport());
    painter.setPen(Qt::NoPen);
    QColor color = palette().color(QPalette::Highlight);
    color.setAlpha(112);
    painter.setBrush(color);
    for (const QPolygonF& bounds : m_selection->bounds())
        painter.drawPolygon(transform.map(bounds));
}

void SelectablePdfView::resizeEvent(QResizeEvent* event)
{
    invalidatePageLayout();
    QPdfView::resizeEvent(event);
}

void SelectablePdfView::scrollContentsBy(int dx, int dy)
{
    QPdfView::scrollContentsBy(dx, dy);
    if (m_dragging)
        m_selectionPending = true;
    if (m_selectionPage >= 0)
        viewport()->update();
}

void SelectablePdfView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);
        clearSelection();
        m_pressPosition = event->position();
        m_anchorPage = pageAt(m_pressPosition);
        if (m_anchorPage >= 0)
            m_anchor = pageTransform(m_anchorPage).inverted().map(m_pressPosition);
    }
    QPdfView::mousePressEvent(event);
}

void SelectablePdfView::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton) || m_anchorPage < 0) {
        QPdfView::mouseMoveEvent(event);
        return;
    }
    m_pointer = event->position();
    if (!m_dragging && (m_pointer - m_pressPosition).manhattanLength() < 3) {
        return;
    }
    m_dragging = true;
    setCursor(Qt::IBeamCursor);
    if (!m_autoScroll.isActive()) {
        m_autoScroll.start();
        updateSelection(m_pointer);
    } else {
        m_selectionPending = true;
    }
    event->accept();
}

void SelectablePdfView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const bool wasDragging = m_dragging;
    if (wasDragging)
        updateSelection(event->position(), true);
    m_autoScroll.stop();
    m_dragging = false;
    m_selectionPending = false;
    m_anchorPage = -1;
    if (wasDragging)
        setCursor(Qt::ArrowCursor);
    else
        QPdfView::mouseReleaseEvent(event);
}

void SelectablePdfView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && m_selectionPage >= 0) {
        clearSelection();
        event->accept();
        return;
    }
    QPdfView::keyPressEvent(event);
}

void SelectablePdfView::updateSelection(QPointF viewportPoint, bool final)
{
    if (m_anchorPage < 0 || pageAt(viewportPoint) != m_anchorPage)
        return;
    const QPointF end = pageTransform(m_anchorPage).inverted().map(viewportPoint);
    QPdfSelection selection = document()->getSelection(m_anchorPage, m_anchor, end);
    // Qt computes the final character boundary from the second argument,
    // including when selecting in reverse. Compare both directions.
    const auto reversed = document()->getSelection(m_anchorPage, end, m_anchor);
    if (reversed.text().size() > selection.text().size())
        selection = reversed;
    if (final && selection.text().isEmpty()) {
        if (const auto glyph = selectSingleGlyph(end))
            selection = *glyph;
    }
    if (selection.text().isEmpty() || selection.bounds().isEmpty()) {
        // Keep the last candidate when the pointer leaves the text line.
        return;
    }

    const bool hadSelection = !selectedText().isEmpty();
    m_selection = selection;
    m_selectionPage = m_anchorPage;
    viewport()->update();
    if (!hadSelection)
        emit selectionChanged(true);
}

std::optional<QPdfSelection> SelectablePdfView::selectSingleGlyph(QPointF end) const
{
    const QPointF delta = end - m_anchor;
    const qreal length = std::hypot(delta.x(), delta.y());
    if (length <= 0 || length > 56)
        return {};
    const QPointF middle = (m_anchor + end) / 2;
    const auto pageText = document()->getAllText(m_anchorPage);
    if (!pageText.boundingRectangle().normalized().contains(middle))
        return {};
    if (pageText.endIndex() - pageText.startIndex() == 1)
        return pageText;

    // Qt returns an empty range for two positions in the same glyph. Probe
    // nearby positions, then verify the actual glyph bounds before accepting.
    const QPointF direction = delta / length;
    QSet<int> checked;
    for (const qreal displacement : {10.0, -10.0, 22.0, -22.0, 42.0, -42.0}) {
        const auto span = document()->getSelection(m_anchorPage, m_anchor,
                                                    middle + direction * displacement);
        if (span.text().isEmpty())
            continue;
        for (const int index : {span.startIndex(), span.endIndex() - 1, span.endIndex()}) {
            if (index < pageText.startIndex() || index >= pageText.endIndex() ||
                checked.contains(index))
                continue;
            checked.insert(index);
            const auto glyph = document()->getSelectionAtIndex(m_anchorPage, index, 1);
            if (glyph.boundingRectangle().normalized().contains(middle))
                return glyph;
        }
    }
    return {};
}

void SelectablePdfView::updateAutoScroll()
{
    preparePageLayout();
    const int index = m_anchorPage - m_firstPage;
    if (index < 0 || index >= m_pageRects.size())
        return;
    // Do not scroll into the next page while extending a single-page selection.
    auto* bar = verticalScrollBar();
    const QRect page = m_pageRects.at(index);
    constexpr int edge = 16;
    constexpr int step = 14;
    if (m_pointer.y() < edge && bar->value() > page.top())
        bar->setValue(qMax(page.top(), bar->value() - step));
    const int last = page.bottom() + 1 - viewport()->height();
    if (m_pointer.y() > viewport()->height() - edge && bar->value() < last)
        bar->setValue(qMin(last, bar->value() + step));
    if (m_selectionPending) {
        m_selectionPending = false;
        updateSelection(m_pointer);
    }
}
