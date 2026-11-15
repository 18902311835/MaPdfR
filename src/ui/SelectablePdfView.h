#pragma once

#include <QPdfSelection>
#include <QPdfView>
#include <QTimer>
#include <QVector>
#include <optional>

class SelectablePdfView : public QPdfView
{
    Q_OBJECT

public:
    explicit SelectablePdfView(QWidget* parent = nullptr);
    QString selectedText() const;
    void clearSelection();

signals:
    void selectionChanged(bool hasSelection);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void invalidatePageLayout();
    void preparePageLayout();
    QTransform pageTransform(int page) const;
    int pageAt(QPointF viewportPoint);
    void updateSelection(QPointF viewportPoint, bool final = false);
    std::optional<QPdfSelection> selectSingleGlyph(QPointF end) const;
    void updateAutoScroll();

    // QPdfView paints into a scrolled document layout and does not expose page
    // rectangles. Cache the geometry used by Qt 6.8–6.11 for point conversion.
    QVector<QRect> m_pageRects;
    QVector<QSizeF> m_pagePoints;
    int m_firstPage = 0;
    qreal m_dpiScale;
    QSize m_layoutViewport;
    bool m_layoutDirty = true;
    QMetaObject::Connection m_documentStatus;

    std::optional<QPdfSelection> m_selection;
    int m_selectionPage = -1;
    int m_anchorPage = -1;
    QPointF m_pressPosition;
    QPointF m_anchor;
    QPointF m_pointer;
    bool m_dragging = false;
    bool m_selectionPending = false;
    QTimer m_autoScroll;
};
