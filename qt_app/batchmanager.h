#pragma once

#include <QString>
#include <QStringList>

namespace BatchManager {

// 包含单峰 cpp_batch_* 和多峰 coupling_batch_*，按时间降序。
QStringList findBatches(const QString &resultsRoot);

// 优先读取所选模式的最新批次指针；失效时回退到该模式最新目录。
QString latestBatch(const QString &resultsRoot, const QString &projectRoot,
                    bool coupling);

bool isCouplingBatch(const QString &batchDir);

} // namespace BatchManager
