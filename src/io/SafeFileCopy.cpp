#include "SafeFileCopy.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace Reader {

bool copyFileAtomically(const QString& source,
                        const QString& destination,
                        QString* error)
{
    if (error)
        error->clear();

    const QFileInfo sourceInfo(source);
    const QFileInfo destinationInfo(destination);

    if (!sourceInfo.exists()
        || !sourceInfo.isFile()
        || !sourceInfo.isReadable()) {
        if (error) {
            *error = QStringLiteral(
                "源文件不存在、不是普通文件或不可读取");
        }
        return false;
    }

    if (destinationInfo.exists()) {
        const QString sourceCanonical =
            sourceInfo.canonicalFilePath();
        const QString destinationCanonical =
            destinationInfo.canonicalFilePath();

        if (!sourceCanonical.isEmpty()
            && sourceCanonical == destinationCanonical) {
            return true;
        }
    }

    QFile input(source);

    if (!input.open(QIODevice::ReadOnly)) {
        if (error)
            *error = input.errorString();
        return false;
    }

    QSaveFile output(destination);

    if (!output.open(QIODevice::WriteOnly)) {
        if (error)
            *error = output.errorString();
        return false;
    }

    constexpr qint64 ChunkSize = 1024 * 1024;

    while (!input.atEnd()) {
        const QByteArray chunk =
            input.read(ChunkSize);

        if (chunk.isEmpty()
            && input.error()
                != QFileDevice::NoError) {
            if (error)
                *error = input.errorString();

            output.cancelWriting();
            return false;
        }

        if (output.write(chunk) != chunk.size()) {
            if (error)
                *error = output.errorString();

            output.cancelWriting();
            return false;
        }
    }

    if (!output.commit()) {
        if (error)
            *error = output.errorString();
        return false;
    }

    return true;
}

}
