#include "FileAssociation.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#include <string>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

namespace MaPdfRFileType {

namespace {

const wchar_t kProgId[] = L"MaPdfR.PDFDocument";
const wchar_t kAppName[] = L"MaPdfR";

#ifdef _WIN32
QString join(const QString &base, const QString &child)
{
    return base + QLatin1Char('\\') + child;
}

bool openKey(const QString &path, HKEY *key, REGSAM access)
{
    *key = nullptr;
    return RegOpenKeyExW(HKEY_CURRENT_USER, path.toStdWString().c_str(), 0,
                         access, key)
           == ERROR_SUCCESS;
}

bool createKey(const QString &path, HKEY *key)
{
    *key = nullptr;
    return RegCreateKeyExW(HKEY_CURRENT_USER, path.toStdWString().c_str(),
                           0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, key,
                           nullptr)
           == ERROR_SUCCESS;
}

bool keyExists(const QString &path)
{
    HKEY key = nullptr;
    const bool exists = openKey(path, &key, KEY_READ);
    if (key)
        RegCloseKey(key);
    return exists;
}

bool setString(const QString &path, const wchar_t *name, const QString &value,
               QString &error)
{
    HKEY key = nullptr;

    if (!createKey(path, &key)) {
        error = QStringLiteral("无法创建注册表项 %1").arg(path);
        return false;
    }

    const std::wstring wide = value.toStdWString();
    const LONG rc = RegSetValueExW(
        key, name, 0, REG_SZ,
        reinterpret_cast<const BYTE *>(wide.c_str()),
        DWORD((wide.size() + 1) * sizeof(wchar_t)));

    RegCloseKey(key);

    if (rc != ERROR_SUCCESS) {
        error = QStringLiteral("无法写入注册表值 %1（错误 %2）")
                    .arg(path)
                    .arg(rc);
        return false;
    }

    return true;
}

bool setNone(const QString &path, const wchar_t *name, QString &error)
{
    HKEY key = nullptr;

    if (!createKey(path, &key)) {
        error = QStringLiteral("无法创建注册表项 %1").arg(path);
        return false;
    }

    const LONG rc = RegSetValueExW(key, name, 0, REG_NONE, nullptr, 0);
    RegCloseKey(key);

    if (rc != ERROR_SUCCESS) {
        error = QStringLiteral("无法写入注册表值 %1（错误 %2）")
                    .arg(path)
                    .arg(rc);
        return false;
    }

    return true;
}

bool readString(const QString &path, const wchar_t *name, QString &value)
{
    HKEY key = nullptr;

    if (!openKey(path, &key, KEY_READ))
        return false;

    DWORD type = 0;
    DWORD size = 0;
    LONG rc = RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);

    if (rc != ERROR_SUCCESS
        || (type != REG_SZ && type != REG_EXPAND_SZ)
        || size < sizeof(wchar_t)) {
        RegCloseKey(key);
        return false;
    }

    std::wstring buffer(size / sizeof(wchar_t) + 1, L'\0');
    rc = RegQueryValueExW(key, name, nullptr, &type,
                          reinterpret_cast<BYTE *>(buffer.data()), &size);
    RegCloseKey(key);

    if (rc != ERROR_SUCCESS)
        return false;

    value = QString::fromWCharArray(buffer.data());
    return true;
}

bool deleteValue(const QString &path, const wchar_t *name)
{
    HKEY key = nullptr;

    if (!openKey(path, &key, KEY_SET_VALUE))
        return true; // 不存在即已完成

    const LONG rc = RegDeleteValueW(key, name);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}

void deleteTree(const QString &path)
{
    RegDeleteTreeW(HKEY_CURRENT_USER, path.toStdWString().c_str());
}

void notifyShell()
{
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

QString openCommand(const QString &exePath)
{
    return QStringLiteral("\"%1\" \"%2\"")
        .arg(QDir::toNativeSeparators(exePath), QStringLiteral("%1"));
}

QString iconReference(const QString &exePath, const QString &iconPath)
{
    const QString icon = QDir::toNativeSeparators(iconPath);

    if (!iconPath.isEmpty() && QFileInfo(icon).isFile())
        return QStringLiteral("\"%1\",0").arg(icon);

    return QStringLiteral("\"%1\",0").arg(QDir::toNativeSeparators(exePath));
}

#else // !_WIN32

// freedesktop.org 的桌面项标识与 MIME 类型。桌面项安装到
// ~/.local/share/applications 后，xdg-mime 就能用它作为 PDF 的默认程序。
const char kDesktopFileId[] = "mapdfr.desktop";
const char kMimeType[] = "application/pdf";

QString userDesktopFilePath()
{
    const QString dataDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(dataDir).filePath(QStringLiteral("applications/")
                                  + QString::fromLatin1(kDesktopFileId));
}

QString mimeAppsPath()
{
    const QString configDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return QDir(configDir).filePath(QStringLiteral("mimeapps.list"));
}

// Desktop Entry 规范：值里的反斜杠和引号要转义，路径整体加引号。
QString desktopValue(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return escaped;
}

bool runProcess(const QString &program, const QStringList &arguments,
                QString *output, QString &error, bool allowFailure = false)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);

    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished();
        error = QStringLiteral("执行 %1 超时").arg(program);
        return false;
    }

    const QString text = QString::fromLocal8Bit(process.readAll()).trimmed();

    if (output)
        *output = text;

    if (process.exitStatus() != QProcess::NormalExit) {
        error = QStringLiteral("%1 异常退出").arg(program);
        return false;
    }

    if (process.exitCode() != 0 && !allowFailure) {
        error = text.isEmpty()
                    ? QStringLiteral("%1 返回 %2").arg(program).arg(process.exitCode())
                    : text;
        return false;
    }

    return true;
}

void refreshDesktopDatabase()
{
    const QString tool =
        QStandardPaths::findExecutable(QStringLiteral("update-desktop-database"));

    if (tool.isEmpty())
        return;

    QString error;
    runProcess(tool,
               {QDir(QStandardPaths::writableLocation(
                         QStandardPaths::GenericDataLocation))
                    .filePath(QStringLiteral("applications"))},
               nullptr, error, true);
}

// 从 mimeapps.list 里摘掉本程序的条目，让它不再作为 PDF 的处理程序。
void removeFromMimeApps()
{
    const QString path = mimeAppsPath();

    if (!QFileInfo::exists(path))
        return;

    QSettings settings(path, QSettings::IniFormat);
    static const char *const groups[] = {"Added Associations",
                                         "Default Applications"};
    const QString key = QString::fromLatin1(kMimeType);
    const QString id = QString::fromLatin1(kDesktopFileId);

    for (const char *group : groups) {
        settings.beginGroup(QString::fromLatin1(group));
        const QStringList entries = settings.value(key).toStringList();

        if (entries.contains(id)) {
            QStringList remaining = entries;
            remaining.removeAll(id);

            if (remaining.isEmpty())
                settings.remove(key);
            else
                settings.setValue(key, remaining);
        }

        settings.endGroup();
    }

    settings.sync();
}

#endif // _WIN32

} // namespace

QString progId()
{
#ifdef _WIN32
    return QString::fromWCharArray(kProgId);
#else
    return QString::fromLatin1(kDesktopFileId);
#endif
}

RegistrationStatus query(const QString &exePath, const RegistryScope &scope)
{
    RegistrationStatus status;
    status.progId = progId();

#ifdef _WIN32
    const QString progIdPath = join(scope.classesRoot, status.progId);
    status.registered =
        keyExists(progIdPath) && keyExists(join(progIdPath, QStringLiteral("shell\\open\\command")));

    if (!status.registered)
        return status;

    readString(join(progIdPath, QStringLiteral("shell\\open\\command")),
               nullptr, status.command);
    readString(progIdPath, L"FriendlyTypeName", status.friendlyName);
    readString(join(progIdPath, QStringLiteral("DefaultIcon")), nullptr,
               status.iconPath);

    // 命令形如 "<exe>" "%1"，取出第一个引号内的可执行文件路径。
    const int firstQuote = status.command.indexOf(QLatin1Char('"'));
    const int secondQuote =
        status.command.indexOf(QLatin1Char('"'), firstQuote + 1);

    if (firstQuote >= 0 && secondQuote > firstQuote) {
        status.exePath =
            status.command.mid(firstQuote + 1, secondQuote - firstQuote - 1);
    }

    if (!exePath.isEmpty()) {
        status.registered =
            status.command.compare(openCommand(exePath), Qt::CaseInsensitive) == 0;
    }
#else
    Q_UNUSED(exePath);
    Q_UNUSED(scope);
    status.friendlyName = QStringLiteral("PDF 文档");
    // registered 表示本程序已写入用户桌面项；command 是系统当前的 PDF
    // 默认程序（xdg-mime query default application/pdf）。
    status.registered = QFileInfo::exists(userDesktopFilePath());

    const QString xdgMime =
        QStandardPaths::findExecutable(QStringLiteral("xdg-mime"));

    if (!xdgMime.isEmpty()) {
        QString output;
        QString ignored;
        runProcess(xdgMime,
                   {QStringLiteral("query"), QStringLiteral("default"),
                    QString::fromLatin1(kMimeType)},
                   &output, ignored, true);
        status.command = output;
    }
#endif

    return status;
}

bool registerPdfType(const QString &exePath, const QString &iconPath,
                     QString &error, const RegistryScope &scope)
{
#ifdef _WIN32
    const QString progIdPath = join(scope.classesRoot, progId());

    if (!setString(progIdPath, nullptr, QStringLiteral("PDF 文档"), error)
        || !setString(progIdPath, L"FriendlyTypeName",
                      QStringLiteral("PDF 文档"), error)
        || !setString(join(progIdPath, QStringLiteral("DefaultIcon")), nullptr,
                      iconReference(exePath, iconPath), error)
        || !setString(join(progIdPath, QStringLiteral("shell\\open\\command")),
                      nullptr, openCommand(exePath), error)
        || !setString(join(progIdPath, QStringLiteral("Application")),
                      L"ApplicationName", QString::fromWCharArray(kAppName),
                      error)
        || !setString(join(progIdPath, QStringLiteral("Application")),
                      L"ApplicationIcon",
                      QStringLiteral("\"%1\",0")
                          .arg(QDir::toNativeSeparators(exePath)),
                      error)) {
        return false;
    }

    // 把 ProgID 加进 .pdf 的「打开方式」列表；不修改 .pdf 的默认值。
    if (!setNone(join(join(scope.classesRoot, QStringLiteral(".pdf")),
                      QStringLiteral("OpenWithProgids")),
                 kProgId, error)) {
        return false;
    }

    // Applications\<exe> 让资源管理器把本程序列进「打开方式」。
    const QString appPath = join(
        scope.classesRoot,
        QStringLiteral("Applications\\MaPdfR.exe"));

    if (!setString(appPath, L"FriendlyAppName",
                   QString::fromWCharArray(kAppName), error)
        || !setString(join(appPath, QStringLiteral("shell\\open\\command")),
                      nullptr, openCommand(exePath), error)) {
        return false;
    }

    notifyShell();
    return true;
#else
    Q_UNUSED(scope);

    const QString exe = exePath.isEmpty()
                            ? QCoreApplication::applicationFilePath()
                            : exePath;

    if (exe.isEmpty()) {
        error = QStringLiteral("无法确定 MaPdfR 的可执行文件路径");
        return false;
    }

    const QString path = userDesktopFilePath();
    const QString dir = QFileInfo(path).absolutePath();

    if (!QDir().mkpath(dir)) {
        error = QStringLiteral("无法创建目录 %1").arg(dir);
        return false;
    }

    QString icon = QStringLiteral("mapdfr");

    if (!iconPath.isEmpty() && QFileInfo(iconPath).isFile())
        icon = QFileInfo(iconPath).absoluteFilePath();

    const QString escapedExe = desktopValue(exe);
    const QString content =
        QStringLiteral(
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Version=1.0\n"
            "Name=MaPdfR\n"
            "Name[zh_CN]=MaPdfR PDF 阅读器\n"
            "GenericName=PDF Viewer\n"
            "GenericName[zh_CN]=PDF 阅读器\n"
            "Comment=Read, search, print and export PDF documents\n"
            "Comment[zh_CN]=阅读、查找、打印和导出 PDF 文档\n"
            "Exec=\"%1\" %F\n"
            "TryExec=%2\n"
            "Icon=%3\n"
            "Terminal=false\n"
            "Categories=Office;Viewer;\n"
            "MimeType=%4;\n"
            "StartupNotify=true\n"
            "StartupWMClass=MaPdfR\n")
            .arg(escapedExe, escapedExe, desktopValue(icon),
                 QString::fromLatin1(kMimeType));

    QFile file(path);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = QStringLiteral("无法写入 %1：%2").arg(path, file.errorString());
        return false;
    }

    file.write(content.toUtf8());
    file.close();

    refreshDesktopDatabase();
    return true;
#endif
}

bool unregisterPdfType(QString &error, const RegistryScope &scope)
{
#ifdef _WIN32
    deleteTree(join(scope.classesRoot, progId()));
    deleteTree(join(scope.classesRoot,
                    QStringLiteral("Applications\\MaPdfR.exe")));

    if (!deleteValue(join(join(scope.classesRoot, QStringLiteral(".pdf")),
                          QStringLiteral("OpenWithProgids")),
                     kProgId)) {
        error = QStringLiteral("无法从 .pdf 的打开方式列表中移除 MaPdfR");
        return false;
    }

    notifyShell();
    return true;
#else
    Q_UNUSED(scope);

    const QString path = userDesktopFilePath();

    if (QFileInfo::exists(path) && !QFile::remove(path)) {
        error = QStringLiteral("无法删除 %1").arg(path);
        return false;
    }

    removeFromMimeApps();
    refreshDesktopDatabase();
    return true;
#endif
}

bool openWithDialog(QString &error)
{
#ifdef _WIN32
    QTemporaryDir dir;

    if (!dir.isValid()) {
        error = QStringLiteral("无法创建临时目录");
        return false;
    }

    // SHOpenWithDialog 需要一个文件，用临时 .pdf 文件触发。
    const QString sample =
        dir.filePath(QStringLiteral("mapdfr_association_sample.pdf"));

    {
        QFile file(sample);

        if (!file.open(QIODevice::WriteOnly)) {
            error = QStringLiteral("无法创建临时文件 %1").arg(sample);
            return false;
        }
    }

    const std::wstring path = QDir::toNativeSeparators(sample).toStdWString();
    OPENASINFO info{};
    info.pcszFile = path.c_str();
    info.pcszClass = nullptr;
    // OAIF_ALLOW_REGISTRATION 显示「始终使用此应用」；不加 OAIF_EXEC，
    // 避免用户确认后还会启动示例文件。
    info.oaifInFlags = OAIF_ALLOW_REGISTRATION;

    const HRESULT hr = SHOpenWithDialog(nullptr, &info);

    if (hr == S_FALSE || hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        error = QStringLiteral("已取消");
        return false;
    }

    if (FAILED(hr)) {
        error = QStringLiteral("无法打开「打开方式」对话框（0x%1）")
                    .arg(quint32(hr), 8, 16, QLatin1Char('0'));
        return false;
    }

    notifyShell();
    return true;
#else
    // Linux 没有统一的「打开方式」对话框，等价操作是直接把本程序设为
    // application/pdf 的默认处理程序（写入 ~/.config/mimeapps.list）。
    const QString xdgMime =
        QStandardPaths::findExecutable(QStringLiteral("xdg-mime"));

    if (xdgMime.isEmpty()) {
        error = QStringLiteral(
            "未找到 xdg-mime，请先安装 xdg-utils 包，或在系统设置 → "
            "默认应用中手动选择 MaPdfR。");
        return false;
    }

    if (!runProcess(xdgMime,
                    {QStringLiteral("default"),
                     QString::fromLatin1(kDesktopFileId),
                     QString::fromLatin1(kMimeType)},
                    nullptr, error)) {
        return false;
    }

    const RegistrationStatus status = query();

    if (status.command
        != QString::fromLatin1(kDesktopFileId)) {
        error = QStringLiteral(
            "xdg-mime 已执行，但系统未把 MaPdfR 记为 PDF 默认程序；"
            "请在系统设置 → 默认应用中手动选择。");
        return false;
    }

    return true;
#endif
}

bool openDefaultAppsSettings(QString &error)
{
#ifdef _WIN32
    const INT_PTR result = reinterpret_cast<INT_PTR>(ShellExecuteW(
        nullptr, L"open", L"ms-settings:defaultapps", nullptr, nullptr,
        SW_SHOWNORMAL));

    if (result <= 32) {
        error = QStringLiteral("无法打开 Windows 默认应用设置（%1）").arg(result);
        return false;
    }

    return true;
#else
    error = QStringLiteral(
        "请在系统设置 → 默认应用中选择 PDF 的打开程序。");
    return false;
#endif
}

bool selfTest(QString &error)
{
#ifdef _WIN32
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString root = QStringLiteral("Software\\MaPdfR\\SelfTest\\") + id;

    RegistryScope scope;
    scope.classesRoot = join(root, QStringLiteral("Classes"));

    auto cleanup = [&root]() {
        deleteTree(root);
    };

    auto fail = [&](const QString &message) {
        cleanup();
        error = message;
        return false;
    };

    const QString exe = QStringLiteral("C:\\Self Test\\MaPdfR.exe");
    const QString icon = QStringLiteral("C:\\Self Test\\pdf.ico");

    if (!registerPdfType(exe, icon, error, scope))
        return fail(QStringLiteral("注册自测失败：") + error);

    const RegistrationStatus status = query(exe, scope);

    if (!status.registered)
        return fail(QStringLiteral("注册自测：查询不到已注册的 ProgID"));
    if (status.command.compare(openCommand(exe), Qt::CaseInsensitive) != 0)
        return fail(QStringLiteral("注册自测：打开命令不匹配"));
    if (status.iconPath
        != QStringLiteral("\"%1\",0").arg(QDir::toNativeSeparators(exe)))
        return fail(QStringLiteral("注册自测：图标回退路径不正确"));

    if (!unregisterPdfType(error, scope))
        return fail(QStringLiteral("注销自测失败：") + error);

    if (query(exe, scope).registered)
        return fail(QStringLiteral("注册自测：注销后 ProgID 仍存在"));

    cleanup();
    return true;
#else
    Q_UNUSED(error);
    // 注册表隔离自测只对 Windows 有意义；Linux 的关联由桌面项和
    // mimeapps.list 决定，测试套件在非 Windows 平台跳过本项。
    error = QStringLiteral("当前平台没有注册表自测");
    return false;
#endif
}

} // namespace MaPdfRFileType
