#pragma once

#include <QFileSystemWatcher>
#include <QStringList>
#include <QTimer>
#include <memory>

class QLockFile;

class LocalOpenRelay : public QObject
{
    Q_OBJECT

public:
    enum class Result { Primary, Forwarded, Failed };

    explicit LocalOpenRelay(QObject* parent = nullptr);
    ~LocalOpenRelay() override;

    Result start(const QStringList& files, QString& error);

signals:
    void openRequested(const QStringList& files);

private:
    void startReceiving();
    void receiveRequests();

    QString m_directory;
    std::unique_ptr<QLockFile> m_lock;
    QFileSystemWatcher m_watcher;
    QTimer m_poll;
};
