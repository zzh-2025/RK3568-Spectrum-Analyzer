#include "app_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

namespace AppPaths {
namespace {

bool isExecutableFile(const QString &path)
{
    const QFileInfo info(path);
    return info.isFile() && info.isExecutable();
}

bool isPackagedReleaseRoot(const QString &path)
{
    const QDir root(path);
    return isExecutableFile(root.filePath(QStringLiteral("bin/spectrum_qt"))) &&
           isExecutableFile(root.filePath(QStringLiteral("bin/spectrum_batch"))) &&
           isExecutableFile(root.filePath(QStringLiteral("bin/coupling_batch")));
}

QString absoluteCleanPath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString userDataRoot()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::home().filePath(QStringLiteral(".local/share/rk3568-spectrum-analyzer"));
    return QDir::cleanPath(base);
}

QString environmentPath(const char *name)
{
    const QString value = qEnvironmentVariable(name).trimmed();
    return value.isEmpty() ? QString() : absoluteCleanPath(value);
}

} // namespace

bool isDevelopmentTree(const QString &path)
{
    const QDir root(path);
    return QFileInfo(root.filePath(QStringLiteral("include/lorentz.h"))).isFile() &&
           QFileInfo(root.filePath(QStringLiteral("src/lorentz.cpp"))).isFile();
}

QString applicationRoot()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList starts = {appDir, QDir::currentPath()};

    for (const QString &start : starts) {
        QDir dir(start);
        for (int level = 0; level < 10; ++level) {
            if (isDevelopmentTree(dir.absolutePath()) ||
                isPackagedReleaseRoot(dir.absolutePath()))
                return dir.absolutePath();

            // A packaged application is launched from <release>/bin.
            if (dir.dirName() == QStringLiteral("bin") &&
                isExecutableFile(dir.filePath(QStringLiteral("spectrum_qt"))) &&
                isExecutableFile(dir.filePath(QStringLiteral("spectrum_batch"))) &&
                isExecutableFile(dir.filePath(QStringLiteral("coupling_batch")))) {
                if (dir.cdUp())
                    return dir.absolutePath();
            }

            if (!dir.cdUp())
                break;
        }
    }

    // Fallback is useful for diagnostics; installed packages should match the
    // <release>/bin layout and development builds should find the source root.
    return appDir;
}

QString dataDirectory(const QString &root)
{
    const QString overridePath = environmentPath("SPECTRUM_DATA_DIR");
    if (!overridePath.isEmpty())
        return overridePath;

    if (isDevelopmentTree(root))
        return QDir(root).filePath(QStringLiteral("data"));

    return QDir(userDataRoot()).filePath(QStringLiteral("data"));
}

QString resultsDirectory(const QString &root)
{
    const QString overridePath = environmentPath("SPECTRUM_RESULTS_DIR");
    if (!overridePath.isEmpty())
        return overridePath;

    if (isDevelopmentTree(root))
        return QDir(root).filePath(QStringLiteral("results"));

    return QDir(userDataRoot()).filePath(QStringLiteral("results"));
}

QString batchExecutable(const QString &root, const QString &name)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(name),
        QDir(root).filePath(QStringLiteral("bin/") + name),
        QDir(root).filePath(QStringLiteral("build_release/") + name),
        QDir(root).filePath(QStringLiteral("build/") + name)
    };

    for (const QString &candidate : candidates) {
        if (isExecutableFile(candidate))
            return candidate;
    }

    return candidates.last();
}

} // namespace AppPaths
