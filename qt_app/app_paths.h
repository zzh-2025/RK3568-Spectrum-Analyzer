#pragma once

#include <QString>

namespace AppPaths {

// Detects either the source checkout root or a packaged release root.
QString applicationRoot();

// In a source checkout, these resolve to <root>/data and <root>/results.
// In an installed release, they resolve to persistent per-user storage.
// SPECTRUM_DATA_DIR and SPECTRUM_RESULTS_DIR override both defaults.
QString dataDirectory(const QString &applicationRoot);
QString resultsDirectory(const QString &applicationRoot);

// Finds a batch executable in a packaged release or a development build tree.
QString batchExecutable(const QString &applicationRoot, const QString &name);

bool isDevelopmentTree(const QString &path);

} // namespace AppPaths
