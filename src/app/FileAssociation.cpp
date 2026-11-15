#include "FileAssociation.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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

#endif // _WIN32

} // namespace

QString progId()
{
    return QString::fromWCharArray(kProgId);
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
    Q_UNUSED(exePath);
    Q_UNUSED(iconPath);
    Q_UNUSED(scope);
    error = QStringLiteral("仅支持 Windows");
    return false;
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
    error = QStringLiteral("仅支持 Windows");
    return false;
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
    error = QStringLiteral("仅支持 Windows");
    return false;
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
    error = QStringLiteral("仅支持 Windows");
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
    error = QStringLiteral("仅支持 Windows");
    return false;
#endif
}

} // namespace MaPdfRFileType
