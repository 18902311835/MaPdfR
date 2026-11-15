#include "LocalOpenRelay.h"
#include "PdfWorkspace.h"

#include <QApplication>
#include <QFileInfo>
#ifdef Q_OS_MACOS
#include <QFileOpenEvent>
#endif
#include <QIcon>
#include <QMessageBox>
#include <QStringList>
#include <QTranslator>

#ifdef Q_OS_MACOS
namespace {

// Finder 把「打开方式」请求作为 QFileOpenEvent 发给应用（包括已经
// 在运行的实例），而不是命令行参数。
class MacOpenEventFilter : public QObject
{
public:
    explicit MacOpenEventFilter(PdfWorkspace* workspace)
        : m_workspace(workspace)
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::FileOpen) {
            const QString path = static_cast<QFileOpenEvent*>(event)->file();
            if (!path.isEmpty())
                m_workspace->requestOpen({path});
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    PdfWorkspace* m_workspace;
};

} // namespace
#endif

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("MaPdfR"));
    QApplication::setApplicationVersion(QStringLiteral(MAPDFR_APP_VERSION));
    QApplication::setOrganizationName(QStringLiteral("MaPdfR"));
#ifdef Q_OS_WIN
    app.setWindowIcon(QIcon(QStringLiteral(":/app.ico")));
#else
    // 与安装的 mapdfr.desktop 对应，桌面环境据此把窗口归到启动器图标下。
    QGuiApplication::setDesktopFileName(QStringLiteral("mapdfr"));
    // Linux 静态 Qt 不一定带 qico 图像插件，用 PNG 更稳。
    app.setWindowIcon(QIcon(QStringLiteral(":/app.png")));
#endif

    QTranslator qtTranslator;
    if (qtTranslator.load(QStringLiteral(":/i18n/qtbase_zh_CN.qm")))
        app.installTranslator(&qtTranslator);

    const QStringList arguments = app.arguments().mid(1);
    QStringList files;
    bool pathOnly = false;
    for (const QString& argument : arguments) {
        if (!pathOnly && argument == QStringLiteral("--")) {
            pathOnly = true;
            continue;
        }
        if (pathOnly || !argument.startsWith(QLatin1Char('-')))
            files.append(QFileInfo(argument).absoluteFilePath());
    }
    if (!arguments.isEmpty() && files.isEmpty() && !pathOnly)
        return 1;

    LocalOpenRelay relay;
    QString error;
    switch (relay.start(files, error)) {
    case LocalOpenRelay::Result::Forwarded:
        return 0;
    case LocalOpenRelay::Result::Failed:
        QMessageBox::warning(nullptr, QApplication::translate("main", "无法连接阅读窗口"), error);
        return 1;
    case LocalOpenRelay::Result::Primary:
        break;
    }

    PdfWorkspace workspace;
    QObject::connect(&relay, &LocalOpenRelay::openRequested, &workspace, &PdfWorkspace::requestOpen);
#ifdef Q_OS_MACOS
    MacOpenEventFilter macOpenEvents(&workspace);
    app.installEventFilter(&macOpenEvents);
#endif
    workspace.show();
    if (!files.isEmpty())
        workspace.requestOpen(files);
    return app.exec();
}
