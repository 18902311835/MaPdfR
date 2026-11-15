#pragma once

#include <QMainWindow>
#include <QStringList>

class QStackedWidget;
class QTabWidget;
class QWidget;
class QTimer;

class PdfWorkspace : public QMainWindow
{
    Q_OBJECT

public:
    explicit PdfWorkspace(QWidget* parent = nullptr);

    // Opening may show a password dialog. Requests arriving during a dialog
    // stay queued until the current operation has completed.
    void requestOpen(const QStringList& paths);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void browse();
    void openNext();
    void closeTab(int index);
    void updateWindowTitle();
    void activateWorkspace();

    QStackedWidget* m_pages;
    QWidget* m_empty;
    QTabWidget* m_tabs;
    QTimer* m_openTimer;
    QStringList m_pending;
    bool m_opening = false;
    bool m_activate = false;
};
