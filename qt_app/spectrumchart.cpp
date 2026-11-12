#include "spectrumchart.h"
#include "csvutils.h"

#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QPen>
#include <QVector>
#include <QColor>

namespace {

void replaceChart(QtCharts::QChartView *view, QtCharts::QChart *next)
{
    if (!view) { delete next; return; }
    QtCharts::QChart *old = view->chart();
    view->setChart(next);
    if (old != next) delete old;
}

int firstColumn(const CsvUtils::Table &csv, const QStringList &names, int fallback)
{
    for (const QString &name : names) {
        const int col = csv.column(name);
        if (col >= 0) return col;
    }
    return fallback;
}

void setAxisTitles(QtCharts::QChart *chart, const QString &yTitle)
{
    const auto xs = chart->axes(Qt::Horizontal);
    const auto ys = chart->axes(Qt::Vertical);
    if (!xs.isEmpty()) xs.first()->setTitleText(QStringLiteral("波长 (nm)"));
    if (!ys.isEmpty()) ys.first()->setTitleText(yTitle);
}

} // namespace

namespace SpectrumChart {

void clear(QtCharts::QChartView *fitView,
           QtCharts::QChartView *residualView,
           const QString &fitTitle, const QString &residualTitle)
{
    auto *fit = new QtCharts::QChart();
    auto *res = new QtCharts::QChart();
    fit->setTitle(fitTitle);
    res->setTitle(residualTitle);
    replaceChart(fitView, fit);
    replaceChart(residualView, res);
}

bool loadFitCsv(QtCharts::QChartView *fitView,
                QtCharts::QChartView *residualView,
                const QString &csvPath, QString *error)
{
    CsvUtils::Table csv;
    if (!CsvUtils::readFile(csvPath, &csv, error)) {
        clear(fitView, residualView);
        return false;
    }

    const int xCol = firstColumn(csv, {QStringLiteral("wavelength_nm")}, 0);
    const int rawCol = firstColumn(csv, {QStringLiteral("measured"), QStringLiteral("raw_intensity")}, 1);
    const int fitCol = firstColumn(csv, {QStringLiteral("fit_total"), QStringLiteral("fitted_intensity")}, 2);
    const int residualCol = firstColumn(csv, {QStringLiteral("residual")}, 3);
    const int baselineCol = csv.column(QStringLiteral("fit_baseline"));

    auto *raw = new QtCharts::QLineSeries();
    auto *total = new QtCharts::QLineSeries();
    auto *resid = new QtCharts::QLineSeries();
    raw->setName(QStringLiteral("原始光谱"));
    total->setName(QStringLiteral("总拟合"));
    resid->setName(QStringLiteral("残差"));

    QPen rawPen(QColor(145, 156, 171)); rawPen.setWidthF(1.0);
    QPen fitPen(QColor(36, 77, 124)); fitPen.setWidthF(2.0);
    QPen resPen(QColor(186, 72, 109)); resPen.setWidthF(1.0);
    raw->setPen(rawPen);
    total->setPen(fitPen);
    resid->setPen(resPen);

    QtCharts::QLineSeries *base = nullptr;
    if (baselineCol >= 0) {
        base = new QtCharts::QLineSeries();
        base->setName(QStringLiteral("基线"));
        QPen pen(QColor(100, 109, 120)); pen.setStyle(Qt::DashLine);
        pen.setWidthF(1.0); base->setPen(pen);
    }
    const QVector<QColor> palette = {
        QColor(0, 156, 150), QColor(136, 92, 204),
        QColor(228, 145, 46), QColor(234, 94, 114),
        QColor(58, 147, 215), QColor(88, 163, 80),
        QColor(187, 118, 56), QColor(110, 100, 195)
    };
    QVector<int> componentCols;
    QVector<QtCharts::QLineSeries *> parts;
    for (int i = 0; i < csv.headers.size(); ++i) {
        if (csv.headers[i].startsWith(QStringLiteral("component_"), Qt::CaseInsensitive)) {
            auto *s = new QtCharts::QLineSeries();
            s->setName(QStringLiteral("分峰 %1").arg(parts.size() + 1));
            QPen pen(palette[parts.size() % palette.size()]);
            pen.setWidthF(1.4); s->setPen(pen);
            componentCols.append(i);
            parts.append(s);
        }
    }

    int validPoints = 0;
    for (const QStringList &line : csv.rows) {
        if (line.size() <= qMax(qMax(xCol,rawCol),qMax(fitCol,residualCol))) continue;
        bool xOk=false, yOk=false, fitOk=false, rOk=false;
        const double x = line[xCol].toDouble(&xOk);
        const double y = line[rawCol].toDouble(&yOk);
        const double fy = line[fitCol].toDouble(&fitOk);
        const double ry = line[residualCol].toDouble(&rOk);
        if (!xOk || !yOk || !fitOk || !rOk) continue;
        raw->append(x,y); total->append(x,fy); resid->append(x,ry);
        if (base && baselineCol < line.size()) {
            bool ok=false; const double v=line[baselineCol].toDouble(&ok);
            if (ok) base->append(x,v);
        }
        for (int i = 0; i < parts.size(); ++i) {
            if (componentCols[i] < line.size()) {
                bool ok=false;
                const double v=line[componentCols[i]].toDouble(&ok);
                if (ok) parts[i]->append(x,v);
            }
        }
        ++validPoints;
    }
    if (validPoints == 0) {
        delete raw; delete total; delete resid; delete base;
        for (auto *part : parts) delete part;
        if (error) *error = QStringLiteral("拟合曲线没有有效数值：") + csvPath;
        clear(fitView,residualView);
        return false;
    }

    auto *fitChart = new QtCharts::QChart();
    auto *resChart = new QtCharts::QChart();
    fitChart->setTitle(QStringLiteral("原始光谱 / 总拟合 / 分峰"));
    resChart->setTitle(QStringLiteral("拟合残差"));
    fitChart->addSeries(raw);
    fitChart->addSeries(total);
    if (base) fitChart->addSeries(base);
    for (auto *part : parts) fitChart->addSeries(part);
    fitChart->createDefaultAxes();
    fitChart->legend()->setVisible(true);
    fitChart->legend()->setAlignment(Qt::AlignBottom);
    setAxisTitles(fitChart, QStringLiteral("强度 (a.u.)"));

    resChart->addSeries(resid);
    resChart->createDefaultAxes();
    resChart->legend()->setVisible(false);
    setAxisTitles(resChart, QStringLiteral("残差"));
    replaceChart(fitView,fitChart);
    replaceChart(residualView,resChart);
    return true;
}

} // namespace SpectrumChart
