#include "batchmanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace BatchManager {

bool isCouplingBatch(const QString &batchDir)
{
    return QFileInfo(batchDir).fileName().startsWith(QStringLiteral("coupling_batch_"));
}

QStringList findBatches(const QString &resultsRoot)
{
    QDir dir(resultsRoot);
    QStringList paths;
    const QStringList names = dir.entryList(
        QStringList() << QStringLiteral("cpp_batch_*") << QStringLiteral("coupling_batch_*"),
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const QString &name : names)
        paths.append(dir.absoluteFilePath(name));
    return paths;
}

QString latestBatch(const QString &resultsRoot, const QString &projectRoot,
                    bool coupling)
{
    const QString pointerFile = coupling
        ? QStringLiteral("latest_coupling_batch.txt")
        : QStringLiteral("latest_cpp_batch.txt");
    QFile pointer(QDir(resultsRoot).filePath(pointerFile));
    if (pointer.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString path = QString::fromUtf8(pointer.readAll()).trimmed();
        if (!path.isEmpty()) {
            if (QDir::isRelativePath(path))
                path = QDir(projectRoot).absoluteFilePath(path);
            if (QFileInfo(path).isDir() && isCouplingBatch(path) == coupling)
                return QDir::cleanPath(path);
        }
    }
    for (const QString &path : findBatches(resultsRoot)) {
        if (isCouplingBatch(path) == coupling)
            return path;
    }
    return QString();
}

} // namespace BatchManager
