#include "LocalOpenRelay.h"
#include "PdfWorkspace.h"

#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QStringList>
#include <QTranslator>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("MaPdfR"));
    QApplication::setApplicationVersion(QStringLiteral(MAPDFR_APP_VERSION));
    QApplication::setOrganizationName(QStringLiteral("MaPdfR"));
    app.setWindowIcon(QIcon(QStringLiteral(":/app.ico")));

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
    workspace.show();
    if (!files.isEmpty())
        workspace.requestOpen(files);
    return app.exec();
}
