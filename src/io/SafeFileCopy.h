#pragma once

#include <QString>

namespace Reader {

// 使用 QSaveFile 原子覆盖目标文件。
// 如果目标就是源文件，则视为成功且不执行写入。
bool copyFileAtomically(const QString& source,
                        const QString& destination,
                        QString* error);

}
