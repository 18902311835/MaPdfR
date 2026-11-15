#include "LocalOpenRelay.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

constexpr int MaxRequestBytes = 256 * 1024;
constexpr int MaxFiles = 256;
constexpr int ReplyTimeoutMs = 4000;
constexpr int MaxRequestAgeMs = 6000;

QString instanceKey()
{
    QString executable = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    if (executable.isEmpty())
        executable = QCoreApplication::applicationFilePath();
#ifdef Q_OS_WIN
    executable = executable.toCaseFolded();
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    const QString desktop = QString::number(session);
#else
    const QString desktop = QGuiApplication::platformName() + QLatin1Char('/')
        + qEnvironmentVariable("DISPLAY") + QLatin1Char('/')
        + qEnvironmentVariable("WAYLAND_DISPLAY") + QLatin1Char('/')
        + qEnvironmentVariable("XDG_SESSION_ID");
#endif
    const QByteArray scope = (executable + QLatin1Char('\n') + desktop).toUtf8();
    const QByteArray digest = QCryptographicHash::hash(scope, QCryptographicHash::Sha256).toHex();
    return QStringLiteral("document-relay-v1-") + QString::fromLatin1(digest.left(32));
}

bool validPath(const QString& path)
{
    return QDir::isAbsolutePath(path) && !path.contains(QChar::Null);
}

}

LocalOpenRelay::LocalOpenRelay(QObject* parent)
    : QObject(parent)
{
    m_poll.setInterval(250);
    m_poll.setTimerType(Qt::CoarseTimer);
    connect(&m_poll, &QTimer::timeout, this, &LocalOpenRelay::receiveRequests);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &LocalOpenRelay::receiveRequests);
}

LocalOpenRelay::~LocalOpenRelay() = default;

LocalOpenRelay::Result LocalOpenRelay::start(const QStringList& files, QString& error)
{
    error.clear();
    const QString location = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    m_directory = QDir(location).filePath(instanceKey());
    if (location.isEmpty() || !QDir().mkpath(m_directory)) {
        error = tr("无法创建本机文档转发目录。");
        return Result::Failed;
    }
#ifndef Q_OS_WIN
    if (!QFile::setPermissions(m_directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) {
        error = tr("无法为文档转发目录设置用户权限。");
        return Result::Failed;
    }
#endif

    const QByteArray request = QJsonDocument(QJsonArray::fromStringList(files))
        .toJson(QJsonDocument::Compact);
    if (request.size() > MaxRequestBytes || files.size() > MaxFiles ||
        !std::all_of(files.cbegin(), files.cend(), validPath)) {
        error = tr("打开请求包含无效路径或文件数过多。");
        return Result::Failed;
    }

    const QDir directory(m_directory);
    m_lock = std::make_unique<QLockFile>(directory.filePath(QStringLiteral("instance.lock")));
    m_lock->setStaleLockTime(0);
    if (m_lock->tryLock()) {
        startReceiving();
        return Result::Primary;
    }
    if (m_lock->error() != QLockFile::LockFailedError) {
        error = tr("无法锁定本机文档转发目录。");
        return Result::Failed;
    }

#ifdef Q_OS_WIN
    qint64 primaryPid = 0;
    QString host;
    QString application;
    if (m_lock->getLockInfo(&primaryPid, &host, &application) && primaryPid > 0 &&
        primaryPid < MAXDWORD) {
        // Pass on foreground rights granted by Explorer where Windows permits
        // it, before the running instance restores the workspace.
        AllowSetForegroundWindow(static_cast<DWORD>(primaryPid));
    }
#endif

    const QString path = directory.filePath(QStringLiteral("%1-%2.request")
        .arg(QDateTime::currentMSecsSinceEpoch())
        .arg(QUuid::createUuid().toString(QUuid::Id128)));
    QSaveFile pending(path);
    if (!pending.open(QIODevice::WriteOnly) || pending.write(request) != request.size() ||
        !pending.commit()) {
        error = tr("无法发送本机打开请求。");
        return Result::Failed;
    }

    // Removing a valid request gives exactly one participant the right to
    // handle it. A timed-out sender can withdraw it, or become the primary
    // if the previous process stopped before accepting it.
    QDeadlineTimer deadline(ReplyTimeoutMs);
    while (!deadline.hasExpired()) {
        if (!QFileInfo::exists(path))
            return Result::Forwarded;
        if (m_lock->tryLock()) {
            if (!QFile::remove(path) && QFileInfo::exists(path)) {
                error = tr("无法接管本机打开请求。");
                return Result::Failed;
            }
            startReceiving();
            return Result::Primary;
        }
        QThread::msleep(40);
    }
    if (!QFile::remove(path) && !QFileInfo::exists(path))
        return Result::Forwarded;

    error = tr("正在运行的 MaPdfR 未确认打开请求。请在原窗口中重试。");
    return Result::Failed;
}

void LocalOpenRelay::startReceiving()
{
    m_watcher.addPath(m_directory);
    // Also poll in case the local file system does not deliver a watch event.
    m_poll.start();
}

void LocalOpenRelay::receiveRequests()
{
    const auto requests = QDir(m_directory).entryInfoList(
        {QStringLiteral("*.request")}, QDir::Files | QDir::NoSymLinks, QDir::Name);
    for (const QFileInfo& entry : requests) {
        const QString path = entry.absoluteFilePath();
        if (entry.lastModified().msecsTo(QDateTime::currentDateTime()) > MaxRequestAgeMs) {
            QFile::remove(path);
            continue;
        }

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray bytes = file.read(MaxRequestBytes + 1);
        file.close();
        if (bytes.size() > MaxRequestBytes)
            continue;
        const QJsonDocument message = QJsonDocument::fromJson(bytes);
        const QJsonArray array = message.array();
        if (!message.isArray() || array.size() > MaxFiles)
            continue;

        QStringList paths;
        paths.reserve(array.size());
        for (const auto& item : array)
            paths.append(item.toString());
        if (std::all_of(paths.cbegin(), paths.cend(), validPath) && QFile::remove(path))
            emit openRequested(paths);
    }
}
