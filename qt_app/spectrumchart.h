#pragma once

#include <QString>
#include <QtCharts/QChartView>

namespace SpectrumChart {

// 自动识别旧单峰 CSV (4列) 和第七阶段多峰 CSV (5+N列)。
bool loadFitCsv(QtCharts::QChartView *fitView,
                QtCharts::QChartView *residualView,
                const QString &csvPath,
                QString *error = nullptr);

void clear(QtCharts::QChartView *fitView,
           QtCharts::QChartView *residualView,
           const QString &fitTitle = QStringLiteral("原始光谱 / Lorentz 拟合"),
           const QString &residualTitle = QStringLiteral("拟合残差"));

} // namespace SpectrumChart
