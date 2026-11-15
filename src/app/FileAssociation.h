#pragma once

// PDF 文件类型注册（仅当前用户 HKCU）。参考同级 VSDX 项目的 OleRegistration：
// 所有写入都在 HKCU\Software\Classes 下，不碰 HKLM，也不擅自修改系统的
// 默认打开程序（Windows 8+ 由系统的「打开方式」对话框让用户确认）。

#include <QString>

namespace MaPdfRFileType {

// ProgID，写成反向域名风格的稳定标识，改动会让已有注册失效。
QString progId();

// 允许自测把注册表根换成隔离的临时子树。正常调用用默认值即可。
struct RegistryScope {
    QString classesRoot = QStringLiteral("Software\\Classes");
};

struct RegistrationStatus {
    bool registered = false;
    QString progId;
    QString command;
    QString iconPath;
    QString exePath;
    QString friendlyName;
};

// exePath 为空时只判断 ProgID 与打开命令是否存在。
RegistrationStatus query(const QString &exePath = QString(),
                         const RegistryScope &scope = RegistryScope());

// 写入 ProgID、DefaultIcon、shell\open\command、Applications 项，
// 并把 ProgID 加进 .pdf 的 OpenWithProgids（出现在「打开方式」列表）。
bool registerPdfType(const QString &exePath, const QString &iconPath,
                     QString &error,
                     const RegistryScope &scope = RegistryScope());

// 只删除本程序写入的内容，不动 .pdf 扩展名本身的默认处理程序。
bool unregisterPdfType(QString &error,
                       const RegistryScope &scope = RegistryScope());

// 调用 Windows 的「打开方式」对话框，让用户自行勾选默认程序。
bool openWithDialog(QString &error);

// 打开 Windows 设置 → 默认应用。
bool openDefaultAppsSettings(QString &error);

// 在隔离的注册表子树里做一次 注册 → 查询 → 注销 往返，不触碰真实关联。
bool selfTest(QString &error);

} // namespace MaPdfRFileType
