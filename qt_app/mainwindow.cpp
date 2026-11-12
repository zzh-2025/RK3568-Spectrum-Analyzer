#include "mainwindow.h"
#include "batchmanager.h"
#include "csvutils.h"
#include "spectrumchart.h"
#include "coupling_analysis.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QScrollArea>
#include <QScreen>
#include <QGuiApplication>
#include <QGridLayout>
#include <QMargins>
#include <QSizePolicy>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

QString locateRoot()
{
    // Works both for qt_app/build/spectrum_qt and out-of-source layouts.
    const QStringList starts = {QCoreApplication::applicationDirPath(), QDir::currentPath()};
    for (const QString &start : starts) {
        QDir dir(start);
        for (int level = 0; level < 8; ++level) {
            if (QFileInfo(dir.filePath(QStringLiteral("include/lorentz.h"))).isFile() &&
                QFileInfo(dir.filePath(QStringLiteral("src/lorentz.cpp"))).isFile())
                return dir.absolutePath();
            if (!dir.cdUp()) break;
        }
    }
    return QDir::currentPath();
}

QDoubleSpinBox *makeWavelength(double value, QWidget *parent, double min = 1.0, double max = 2000.0)
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setRange(min,max);
    spin->setDecimals(2);
    spin->setSingleStep(2.0);
    spin->setSuffix(QStringLiteral(" nm"));
    spin->setValue(value);
    return spin;
}

QTableWidgetItem *tableItem(const QString &value)
{
    auto *item = new QTableWidgetItem(value);
    item->setToolTip(value);
    return item;
}

QString fmt(double value, int digits = 2)
{
    return std::isfinite(value) ? QString::number(value, 'f', digits) : QStringLiteral("—");
}

} // namespace

class MainWindow final : public QMainWindow
{
public:
    MainWindow()
    {
        m_projectRoot = locateRoot();
        m_resultsRoot = QDir(m_projectRoot).filePath(QStringLiteral("results"));
        m_dataDir = QDir(m_projectRoot).filePath(QStringLiteral("data"));
        setWindowTitle(QStringLiteral("RK3568 光谱分析平台 | 单峰 / 多峰 / 耦合分析"));
        resize(960, 500);  // Main.cpp fits the actual desktop work area, with margins for Xfce panels.
        buildUi();
        refreshBatchList();
        const QString recent = BatchManager::latestBatch(m_resultsRoot, m_projectRoot, true);
        if (!recent.isEmpty()) loadBatchDir(recent);
        else {
            const QString legacy = BatchManager::latestBatch(m_resultsRoot, m_projectRoot, false);
            if (!legacy.isEmpty()) loadBatchDir(legacy);
        }
    }

private:
    QString m_projectRoot, m_dataDir, m_resultsRoot, m_currentBatchDir;
    CsvUtils::Table m_summary;
    QMap<QString, QVector<MultiLorentzPeak>> m_peakById;
    QVector<int> m_displayRowToSummary;
    QString m_currentId, m_currentFilename;
    QString m_currentWarnings;
    bool m_couplingBatch = false;
    bool m_stopped = false;
    CouplingPairMetrics m_currentPair;

    QLineEdit *m_dataEdit = nullptr;
    QComboBox *m_modeCombo = nullptr, *m_peakCountCombo = nullptr;
    QComboBox *m_baselineCombo = nullptr, *m_pairModeCombo = nullptr;
    QComboBox *m_history = nullptr;
    QStackedWidget *m_pairSettings = nullptr;
    QDoubleSpinBox *m_targetUp = nullptr, *m_targetLp = nullptr;
    QDoubleSpinBox *m_winUpMin = nullptr, *m_winUpMax = nullptr;
    QDoubleSpinBox *m_winLpMin = nullptr, *m_winLpMax = nullptr;
    QSpinBox *m_pairA = nullptr, *m_pairB = nullptr;
    QPushButton *m_runButton = nullptr, *m_stopButton = nullptr;
    QLabel *m_status = nullptr, *m_peakInfo = nullptr, *m_couplingText = nullptr;
    QLabel *m_spectrumName = nullptr;
    QTableWidget *m_summaryTable = nullptr, *m_peakTable = nullptr, *m_statsTable = nullptr;
    QComboBox *m_manualUp = nullptr, *m_manualLp = nullptr;
    QTabWidget *m_pages = nullptr;
    QTextEdit *m_log = nullptr;
    QtCharts::QChartView *m_fitView = nullptr, *m_residualView = nullptr;
    QProcess *m_process = nullptr;

    QString executablePath(const QString &name) const
    {
        const QDir root(m_projectRoot);
        for (const QString &folder : {QStringLiteral("build_release"), QStringLiteral("build")}) {
            const QString path = root.filePath(folder + QLatin1Char('/') + name);
            if (QFileInfo(path).isExecutable()) return path;
        }
        return root.filePath(QStringLiteral("build_release/") + name);
    }

    QWidget *makeTargetPanel()
    {
        auto *widget = new QWidget(this);
        auto *line = new QHBoxLayout(widget); line->setContentsMargins(0,0,0,0);
        line->addWidget(new QLabel(QStringLiteral("UP 目标:")));
        m_targetUp = makeWavelength(612, widget); line->addWidget(m_targetUp);
        line->addWidget(new QLabel(QStringLiteral("LP 目标:")));
        m_targetLp = makeWavelength(705, widget); line->addWidget(m_targetLp);
        line->addStretch(1);
        return widget;
    }

    QWidget *makeIndexPanel()
    {
        auto *widget = new QWidget(this);
        auto *line = new QHBoxLayout(widget); line->setContentsMargins(0,0,0,0);
        line->addWidget(new QLabel(QStringLiteral("较短波长峰序号:")));
        m_pairA = new QSpinBox(widget); m_pairA->setRange(1,8); m_pairA->setValue(2);
        line->addWidget(m_pairA);
        line->addWidget(new QLabel(QStringLiteral("较长波长峰序号:")));
        m_pairB = new QSpinBox(widget); m_pairB->setRange(1,8); m_pairB->setValue(4);
        line->addWidget(m_pairB); line->addStretch(1);
        return widget;
    }

    QWidget *makeWindowPanel()
    {
        // Two rows instead of a long horizontal row: works even on 800x480 screens.
        auto *widget = new QWidget(this);
        auto *grid = new QGridLayout(widget);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(6);
        grid->setVerticalSpacing(5);
        m_winUpMin = makeWavelength(580, widget); m_winUpMax = makeWavelength(650, widget);
        m_winLpMin = makeWavelength(675, widget); m_winLpMax = makeWavelength(740, widget);
        grid->addWidget(new QLabel(QStringLiteral("UP 范围:")), 0, 0);
        grid->addWidget(m_winUpMin, 0, 1);
        grid->addWidget(new QLabel(QStringLiteral("至")), 0, 2);
        grid->addWidget(m_winUpMax, 0, 3);
        grid->addWidget(new QLabel(QStringLiteral("LP 范围:")), 1, 0);
        grid->addWidget(m_winLpMin, 1, 1);
        grid->addWidget(new QLabel(QStringLiteral("至")), 1, 2);
        grid->addWidget(m_winLpMax, 1, 3);
        grid->setColumnStretch(4, 1);
        return widget;
    }

    void buildUi()
    {
        auto *central = new QWidget(this);
        auto *page = new QVBoxLayout(central);
        page->setContentsMargins(5, 3, 5, 5);
        page->setSpacing(2);

        // Compact persistent header. All feature controls now live in separate pages.
        auto *header = new QHBoxLayout();
        auto *title = new QLabel(QStringLiteral("RK3568 光谱分析平台"), central);
        QFont font = title->font(); font.setPointSize(11); font.setBold(true);
        title->setFont(font);
        header->addWidget(title);
        m_status = new QLabel(QStringLiteral("就绪 | 请选择分析方式"), central);
        m_status->setWordWrap(false);
        m_status->setMaximumHeight(25);
        m_status->setMinimumWidth(0);
        m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        header->addWidget(m_status, 1);
        header->setSpacing(7);
        page->addLayout(header);

        m_pages = new QTabWidget(central);
        m_pages->setUsesScrollButtons(true);
        m_pages->setDocumentMode(true);
        page->addWidget(m_pages, 1);

        // Page 1: fitting setup and batch management, vertically scrollable.
        auto *settingsScroll = new QScrollArea(m_pages);
        settingsScroll->setWidgetResizable(true);
        settingsScroll->setFrameShape(QFrame::NoFrame);
        auto *settingsPage = new QWidget(settingsScroll);
        auto *settingsLayout = new QVBoxLayout(settingsPage);
        settingsLayout->setContentsMargins(5, 5, 5, 5);
        settingsLayout->setSpacing(9);

        settingsLayout->addWidget(new QLabel(QStringLiteral("ASC 数据目录"), settingsPage));
        auto *filesRow = new QHBoxLayout();
        m_dataEdit = new QLineEdit(m_dataDir, settingsPage);
        m_dataEdit->setMinimumWidth(0);
        filesRow->addWidget(m_dataEdit, 1);
        auto *browse = new QPushButton(QStringLiteral("选择目录"), settingsPage);
        filesRow->addWidget(browse);
        settingsLayout->addLayout(filesRow);

        auto *parameters = new QGridLayout();
        parameters->setHorizontalSpacing(8);
        parameters->setVerticalSpacing(7);
        parameters->addWidget(new QLabel(QStringLiteral("分析类型:")), 0, 0);
        m_modeCombo = new QComboBox(settingsPage);
        m_modeCombo->addItem(QStringLiteral("多峰 + 表观耦合"), QStringLiteral("coupling_batch"));
        m_modeCombo->addItem(QStringLiteral("原版单峰"), QStringLiteral("spectrum_batch"));
        parameters->addWidget(m_modeCombo, 0, 1);
        parameters->addWidget(new QLabel(QStringLiteral("峰数:")), 0, 2);
        m_peakCountCombo = new QComboBox(settingsPage);
        for (int n = 2; n <= 5; ++n) m_peakCountCombo->addItem(QString::number(n));
        m_peakCountCombo->addItem(QStringLiteral("自动 (1–5)"), QStringLiteral("auto"));
        m_peakCountCombo->setCurrentText(QStringLiteral("4"));
        parameters->addWidget(m_peakCountCombo, 0, 3);
        parameters->addWidget(new QLabel(QStringLiteral("基线:")), 1, 0);
        m_baselineCombo = new QComboBox(settingsPage);
        m_baselineCombo->addItem(QStringLiteral("常数"), QStringLiteral("constant"));
        m_baselineCombo->addItem(QStringLiteral("无"), QStringLiteral("none"));
        m_baselineCombo->addItem(QStringLiteral("线性"), QStringLiteral("linear"));
        m_baselineCombo->addItem(QStringLiteral("自动选择"), QStringLiteral("auto"));
        parameters->addWidget(m_baselineCombo, 1, 1);
        parameters->addWidget(new QLabel(QStringLiteral("选峰方式:")), 1, 2);
        m_pairModeCombo = new QComboBox(settingsPage);
        m_pairModeCombo->addItem(QStringLiteral("目标波长"));
        m_pairModeCombo->addItem(QStringLiteral("分峰序号"));
        m_pairModeCombo->addItem(QStringLiteral("波段窗口"));
        m_pairModeCombo->addItem(QStringLiteral("最强两峰（需复核）"));
        parameters->addWidget(m_pairModeCombo, 1, 3);
        parameters->setColumnStretch(1, 1);
        parameters->setColumnStretch(3, 1);
        settingsLayout->addLayout(parameters);

        m_pairSettings = new QStackedWidget(settingsPage);
        m_pairSettings->addWidget(makeTargetPanel());
        m_pairSettings->addWidget(makeIndexPanel());
        m_pairSettings->addWidget(makeWindowPanel());
        auto *strongNotice = new QLabel(QStringLiteral("提示：最强两峰仅供数值初筛，请人工复核。"), settingsPage);
        strongNotice->setWordWrap(true);
        m_pairSettings->addWidget(strongNotice);
        settingsLayout->addWidget(m_pairSettings);

        auto *actionRow = new QHBoxLayout();
        m_runButton = new QPushButton(QStringLiteral("开始批量拟合"), settingsPage);
        m_stopButton = new QPushButton(QStringLiteral("停止"), settingsPage);
        m_stopButton->setEnabled(false);
        m_runButton->setMinimumHeight(34);
        m_stopButton->setMinimumHeight(34);
        actionRow->addWidget(m_runButton, 2);
        actionRow->addWidget(m_stopButton, 1);
        settingsLayout->addLayout(actionRow);

        settingsLayout->addWidget(new QLabel(QStringLiteral("历史批次"), settingsPage));
        m_history = new QComboBox(settingsPage);
        m_history->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        settingsLayout->addWidget(m_history);
        auto *historyActions = new QHBoxLayout();
        auto *refresh = new QPushButton(QStringLiteral("刷新"), settingsPage);
        auto *load = new QPushButton(QStringLiteral("加载批次"), settingsPage);
        auto *folder = new QPushButton(QStringLiteral("打开结果目录"), settingsPage);
        historyActions->addWidget(refresh);
        historyActions->addWidget(load);
        historyActions->addWidget(folder);
        settingsLayout->addLayout(historyActions);
        settingsLayout->addStretch(1);
        settingsScroll->setWidget(settingsPage);
        m_pages->addTab(settingsScroll, QStringLiteral("设置"));

        // Page 2: one combined analysis page (fit chart + compact results/residual).
        // Only the main fit is always shown. The lower panel switches in place
        // instead of consuming three separate top-level pages.
        auto *analysisPage = new QWidget(m_pages);
        auto *analysisLayout = new QVBoxLayout(analysisPage);
        analysisLayout->setContentsMargins(2, 1, 2, 2);
        analysisLayout->setSpacing(2);
        auto *analysisSplitter = new QSplitter(Qt::Vertical, analysisPage);
        analysisSplitter->setChildrenCollapsible(false);
        analysisSplitter->setHandleWidth(5);

        auto *fitPane = new QWidget(analysisSplitter);
        auto *fitLayout = new QVBoxLayout(fitPane);
        fitLayout->setContentsMargins(0, 0, 0, 0);
        fitLayout->setSpacing(1);
        auto *fitToolbar = new QHBoxLayout();
        fitToolbar->setContentsMargins(3, 0, 3, 0);
        m_spectrumName = new QLabel(QStringLiteral("未选择光谱"), fitPane);
        m_spectrumName->setMinimumWidth(0);
        m_spectrumName->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        fitToolbar->addWidget(new QLabel(QStringLiteral("光谱:"), fitPane));
        fitToolbar->addWidget(m_spectrumName, 1);
        auto *resetZoomButton = new QPushButton(QStringLiteral("还原"), fitPane);
        auto *expandFitButton = new QPushButton(QStringLiteral("放大"), fitPane);
        resetZoomButton->setMinimumHeight(27);
        expandFitButton->setMinimumHeight(27);
        fitToolbar->addWidget(resetZoomButton);
        fitToolbar->addWidget(expandFitButton);
        fitLayout->addLayout(fitToolbar);
        auto *compactLegend = new QLabel(QStringLiteral("灰：原始  深蓝：总拟合  彩色：分峰  虚线：基线（双击曲线可放大）"), fitPane);
        compactLegend->setStyleSheet(QStringLiteral("color: #64748b; font-size: 9pt;"));
        fitLayout->addWidget(compactLegend);
        m_fitView = new QtCharts::QChartView(new QtCharts::QChart(), fitPane);
        m_fitView->setRenderHint(QPainter::Antialiasing);
        m_fitView->setRubberBand(QtCharts::QChartView::RectangleRubberBand);
        // QChartView's intrinsic hint must never push the bottom half off-screen.
        m_fitView->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        m_fitView->setMinimumSize(160, 135);
        m_fitView->installEventFilter(this);
        fitLayout->addWidget(m_fitView, 1);
        fitPane->setMinimumHeight(185);
        analysisSplitter->addWidget(fitPane);

        auto *detailTabs = new QTabWidget(analysisSplitter);
        detailTabs->setDocumentMode(true);
        detailTabs->setUsesScrollButtons(true);
        detailTabs->setMinimumHeight(135);
        // Lower tab 1: batch rows; selecting a row updates the chart above.
        auto *summaryPage = new QWidget(detailTabs);
        auto *summaryLayout = new QVBoxLayout(summaryPage);
        summaryLayout->setContentsMargins(2, 1, 2, 2);
        summaryLayout->setSpacing(2);
        auto *resultBar = new QHBoxLayout();
        resultBar->addWidget(new QLabel(QStringLiteral("点击文件行查看拟合"), summaryPage));
        resultBar->addStretch(1);
        auto *viewPeaksFromResults = new QPushButton(QStringLiteral("查看耦合参数"), summaryPage);
        resultBar->addWidget(viewPeaksFromResults);
        summaryLayout->addLayout(resultBar);
        m_summaryTable = new QTableWidget(summaryPage);
        setupTable(m_summaryTable);
        m_summaryTable->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        m_summaryTable->setMinimumHeight(95);
        summaryLayout->addWidget(m_summaryTable, 1);
        detailTabs->addTab(summaryPage, QStringLiteral("批量结果"));

        // Lower tab 2: residual plot; its x axis remains synchronized.
        auto *residualPage = new QWidget(detailTabs);
        auto *residualLayout = new QVBoxLayout(residualPage);
        residualLayout->setContentsMargins(1, 1, 1, 1);
        residualLayout->setSpacing(0);
        m_residualView = new QtCharts::QChartView(new QtCharts::QChart(), residualPage);
        m_residualView->setRenderHint(QPainter::Antialiasing);
        m_residualView->setRubberBand(QtCharts::QChartView::RectangleRubberBand);
        m_residualView->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        m_residualView->setMinimumSize(120, 100);
        m_residualView->installEventFilter(this);
        residualLayout->addWidget(m_residualView, 1);
        detailTabs->addTab(residualPage, QStringLiteral("残差"));
        analysisSplitter->addWidget(detailTabs);
        analysisSplitter->setStretchFactor(0, 3);
        analysisSplitter->setStretchFactor(1, 2);
        analysisSplitter->setSizes({275, 175});
        analysisLayout->addWidget(analysisSplitter, 1);
        m_pages->addTab(analysisPage, QStringLiteral("光谱分析"));

        // Page 3: peaks and batch statistics in a single split view.
        // Both sides scroll independently; no need to hide one behind a main tab.
        auto *couplingPage = new QSplitter(Qt::Horizontal, m_pages);
        couplingPage->setChildrenCollapsible(false);
        couplingPage->setHandleWidth(6);
        auto *peaksScroll = new QScrollArea(couplingPage);
        peaksScroll->setWidgetResizable(true);
        peaksScroll->setFrameShape(QFrame::NoFrame);
        peaksScroll->setMinimumWidth(350);
        auto *peaksPage = new QWidget(peaksScroll);
        auto *peaksLayout = new QVBoxLayout(peaksPage);
        peaksLayout->setContentsMargins(4, 3, 4, 3);
        peaksLayout->setSpacing(4);
        m_peakInfo = new QLabel(QStringLiteral("选择多峰批次中的文件查看分峰。"), peaksPage);
        m_peakInfo->setWordWrap(true);
        peaksLayout->addWidget(m_peakInfo);
        m_peakTable = new QTableWidget(0, 5, peaksPage);
        m_peakTable->setHorizontalHeaderLabels({QStringLiteral("序号"), QStringLiteral("中心 nm"),
            QStringLiteral("FWHM nm"), QStringLiteral("峰高"), QStringLiteral("面积")});
        setupTable(m_peakTable);
        m_peakTable->setMinimumHeight(105);
        m_peakTable->setMaximumHeight(155);
        peaksLayout->addWidget(m_peakTable);
        auto *reselect = new QHBoxLayout();
        reselect->addWidget(new QLabel(QStringLiteral("UP:"), peaksPage));
        m_manualUp = new QComboBox(peaksPage); reselect->addWidget(m_manualUp, 1);
        reselect->addWidget(new QLabel(QStringLiteral("LP:"), peaksPage));
        m_manualLp = new QComboBox(peaksPage); reselect->addWidget(m_manualLp, 1);
        peaksLayout->addLayout(reselect);
        auto *buttons = new QHBoxLayout();
        auto *calculate = new QPushButton(QStringLiteral("计算 ΔE"), peaksPage);
        auto *save = new QPushButton(QStringLiteral("保存选峰"), peaksPage);
        calculate->setMinimumHeight(30);
        save->setMinimumHeight(30);
        buttons->addWidget(calculate);
        buttons->addWidget(save);
        peaksLayout->addLayout(buttons);
        m_couplingText = new QLabel(QStringLiteral("ΔE：—"), peaksPage);
        m_couplingText->setWordWrap(true);
        m_couplingText->setTextInteractionFlags(Qt::TextSelectableByMouse);
        peaksLayout->addWidget(m_couplingText);
        peaksLayout->addStretch(1);
        peaksScroll->setWidget(peaksPage);
        couplingPage->addWidget(peaksScroll);

        auto *statsPage = new QWidget(couplingPage);
        auto *statsLayout = new QVBoxLayout(statsPage);
        statsLayout->setContentsMargins(2, 3, 2, 3);
        statsLayout->setSpacing(3);
        statsLayout->addWidget(new QLabel(QStringLiteral("当前批次统计"), statsPage));
        m_statsTable = new QTableWidget(0, 2, statsPage);
        m_statsTable->setHorizontalHeaderLabels({QStringLiteral("统计指标"), QStringLiteral("数值")});
        setupTable(m_statsTable);
        m_statsTable->setMinimumWidth(180);
        m_statsTable->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        m_statsTable->horizontalHeader()->setStretchLastSection(true);
        statsLayout->addWidget(m_statsTable, 1);
        couplingPage->addWidget(statsPage);
        couplingPage->setStretchFactor(0, 7);
        couplingPage->setStretchFactor(1, 3);
        couplingPage->setSizes({670, 300});
        m_pages->addTab(couplingPage, QStringLiteral("耦合统计"));

        // Page 4: processing log.
        m_log = new QTextEdit(m_pages);
        m_log->setReadOnly(true);
        m_pages->addTab(m_log, QStringLiteral("日志"));
        setCentralWidget(central);

        connect(viewPeaksFromResults, &QPushButton::clicked, this, [this, couplingPage]() {
            m_pages->setCurrentWidget(couplingPage);
        });
        connect(expandFitButton, &QPushButton::clicked, this, [this]() {
            showLargeChart(m_fitView);
        });
        connect(resetZoomButton, &QPushButton::clicked, this, [this]() {
            if (m_fitView->chart()) m_fitView->chart()->zoomReset();
            if (m_residualView->chart()) m_residualView->chart()->zoomReset();
        });

        m_process = new QProcess(this);
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        m_process->setWorkingDirectory(m_projectRoot);

        connect(browse, &QPushButton::clicked,this,[this]() {
            const QString path=QFileDialog::getExistingDirectory(this,QStringLiteral("选择 ASC 文件夹"),m_dataEdit->text());
            if (!path.isEmpty()) m_dataEdit->setText(path);
        });
        connect(m_modeCombo,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int index) {
            const bool enabled=(index==0);
            m_peakCountCombo->setEnabled(enabled); m_baselineCombo->setEnabled(enabled);
            m_pairModeCombo->setEnabled(enabled); m_pairSettings->setEnabled(enabled);
        });
        connect(m_pairModeCombo,QOverload<int>::of(&QComboBox::currentIndexChanged),
                m_pairSettings,&QStackedWidget::setCurrentIndex);
        connect(refresh,&QPushButton::clicked,this,[this]() {refreshBatchList();});
        connect(load,&QPushButton::clicked,this,[this]() {loadSelectedBatch();});
        connect(folder,&QPushButton::clicked,this,[this]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_currentBatchDir.isEmpty() ? m_resultsRoot : m_currentBatchDir));
        });
        connect(m_runButton,&QPushButton::clicked,this,[this]() {runBatch();});
        connect(m_stopButton,&QPushButton::clicked,this,[this]() {
            if (m_process->state()!=QProcess::NotRunning) {
                m_stopped=true;
                m_process->kill();
                m_status->setText(QStringLiteral("正在停止批量分析…"));
            }
        });
        connect(m_summaryTable,&QTableWidget::itemSelectionChanged,this,[this]() {updateSelectedSpectrum();});
        connect(calculate,&QPushButton::clicked,this,[this]() {recalculatePair();});
        connect(save,&QPushButton::clicked,this,[this]() {saveManualPair();});
        connect(m_process,&QProcess::readyReadStandardOutput,this,[this]() {
            m_log->moveCursor(QTextCursor::End);
            m_log->insertPlainText(QString::fromLocal8Bit(m_process->readAllStandardOutput()));
            m_log->moveCursor(QTextCursor::End);
        });
        connect(m_process,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,
                [this](int code,QProcess::ExitStatus state) {
            m_runButton->setEnabled(true); m_stopButton->setEnabled(false);
            if (m_stopped) {m_status->setText(QStringLiteral("用户已停止分析，已完成的文件可能仍保存在结果目录中。")); return;}
            if (state!=QProcess::NormalExit || (code!=0 && code!=2)) {
                m_status->setText(QStringLiteral("批量分析失败，退出码 %1；请查看日志").arg(code));
                return;
            }
            const bool coupling=m_modeCombo->currentData().toString()==QStringLiteral("coupling_batch");
            const QString latest=BatchManager::latestBatch(m_resultsRoot,m_projectRoot,coupling);
            if (!latest.isEmpty() && loadBatchDir(latest)) {
                m_status->setText(code==2
                    ? QStringLiteral("批量分析完成（部分文件失败或无有效选峰，请查看状态列）")
                    : QStringLiteral("批量分析完成，已加载最新批次"));
            } else {
                m_status->setText(QStringLiteral("进程结束，但未找到可加载的批次结果。"));
            }
        });
        connect(m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error) {
            if (error==QProcess::FailedToStart) {
                m_runButton->setEnabled(true); m_stopButton->setEnabled(false);
                m_status->setText(QStringLiteral("程序启动失败；检查 build_release 是否已编译目标程序。"));
            }
        });
    }

    static void setupTable(QTableWidget *table)
    {
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setAlternatingRowColors(true);
        table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        table->verticalHeader()->setVisible(false);
    }

    void appendLog(const QString &message)
    {
        if (m_log) m_log->append(message.toHtmlEscaped());
    }

    void runBatch()
    {
        if (m_process->state()!=QProcess::NotRunning) return;
        const QString source=m_dataEdit->text().trimmed();
        if (!QFileInfo(source).isDir()) {
            QMessageBox::warning(this,QStringLiteral("数据目录错误"),QStringLiteral("ASC 数据文件夹不存在。"));
            return;
        }
        const bool coupling=m_modeCombo->currentData().toString()==QStringLiteral("coupling_batch");
        const QString program=executablePath(coupling ? QStringLiteral("coupling_batch")
                                                       : QStringLiteral("spectrum_batch"));
        if (!QFileInfo(program).isExecutable()) {
            QMessageBox::critical(this,QStringLiteral("缺少批量分析程序"),
                QStringLiteral("找不到可执行文件：\n%1\n\n请先在 cpp-version 下构建对应目标。")
                .arg(program));
            return;
        }
        QStringList args;
        args << source;
        if (coupling) {
            const QString n = m_peakCountCombo->currentData().toString().isEmpty()
                ? m_peakCountCombo->currentText() : m_peakCountCombo->currentData().toString();
            args << QStringLiteral("--peaks") << n
                 << QStringLiteral("--baseline") << m_baselineCombo->currentData().toString();
            switch(m_pairModeCombo->currentIndex()) {
                case 0:
                    if (m_targetUp->value() >= m_targetLp->value()) {
                        QMessageBox::warning(this,QStringLiteral("目标峰错误"),QStringLiteral("UP 目标波长必须小于 LP 目标波长。"));
                        return;
                    }
                    args << QStringLiteral("--up-target") << QString::number(m_targetUp->value(),'g',12)
                         << QStringLiteral("--lp-target") << QString::number(m_targetLp->value(),'g',12);
                    break;
                case 1:
                    if (m_pairA->value() == m_pairB->value()) {
                        QMessageBox::warning(this,QStringLiteral("峰号错误"),QStringLiteral("请指定两个不同的分峰序号。"));
                        return;
                    }
                    args << QStringLiteral("--pair-index") << QString::number(m_pairA->value()) << QString::number(m_pairB->value());
                    break;
                case 2:
                    if (m_winUpMin->value()>=m_winUpMax->value() || m_winLpMin->value()>=m_winLpMax->value()) {
                        QMessageBox::warning(this,QStringLiteral("波段错误"),QStringLiteral("每个波段的起点必须小于终点。"));
                        return;
                    }
                    args << QStringLiteral("--up-window") << QString::number(m_winUpMin->value(),'g',12)
                         << QString::number(m_winUpMax->value(),'g',12)
                         << QStringLiteral("--lp-window") << QString::number(m_winLpMin->value(),'g',12)
                         << QString::number(m_winLpMax->value(),'g',12);
                    break;
                default:
                    break;
            }
            args << QStringLiteral("--out") << m_resultsRoot;
        } else {
            args << m_resultsRoot;
        }
        QDir().mkpath(m_resultsRoot);
        m_log->clear();
        appendLog(QStringLiteral("执行：") + program + QLatin1Char(' ') + args.join(QLatin1Char(' ')));
        m_stopped=false;
        m_runButton->setEnabled(false); m_stopButton->setEnabled(true);
        m_status->setText(QStringLiteral("正在运行批量分析…"));
        m_process->start(program,args);
    }

    void refreshBatchList(const QString &preferred = QString())
    {
        const QString selected = preferred.isEmpty()
            ? (m_currentBatchDir.isEmpty() ? m_history->currentData().toString() : m_currentBatchDir)
            : preferred;
        const QSignalBlocker blocker(m_history);
        m_history->clear();
        const QStringList paths=BatchManager::findBatches(m_resultsRoot);
        int matched=-1;
        for (const QString &path : paths) {
            const bool coupling=BatchManager::isCouplingBatch(path);
            const QString label= (coupling ? QStringLiteral("[多峰] ") : QStringLiteral("[单峰] "))
                                + QFileInfo(path).fileName();
            m_history->addItem(label,path);
            if (QDir::cleanPath(path)==QDir::cleanPath(selected)) matched=m_history->count()-1;
        }
        if (matched>=0) m_history->setCurrentIndex(matched);
    }

    void loadSelectedBatch()
    {
        const QString path=m_history->currentData().toString();
        if (path.isEmpty()) {
            QMessageBox::information(this,QStringLiteral("没有历史批次"),QStringLiteral("results 下尚无分析结果。"));
            return;
        }
        loadBatchDir(path);
    }

    void renderSummary()
    {
        struct Field {const char *name; const char *label;};
        const QVector<Field> couplingFields = {
            {"filename","文件名"},{"status","状态"},{"n","峰数"},{"baseline","基线"},
            {"r2","R²"},{"up_nm","UP (nm)"},{"lp_nm","LP (nm)"},
            {"splitting_mev","ΔE (meV)"},{"apparent_g_mev","g_app (meV)"},
            {"linewidth_criterion","线宽判据"},{"warnings","复核提示"}
        };
        const QVector<Field> legacyFields = {
            {"filename","文件名"},{"peak_position_nm","峰位 (nm)"},
            {"fwhm_nm","FWHM (nm)"},{"peak_height","峰高"},
            {"peak_area","峰面积"},{"r_squared","R²"},
            {"iterations","迭代次数"},{"converged","收敛"}
        };
        const QVector<Field> &fields=m_couplingBatch ? couplingFields : legacyFields;
        QSignalBlocker blocker(m_summaryTable);
        m_displayRowToSummary.clear();
        m_summaryTable->clear();
        m_summaryTable->setColumnCount(fields.size());
        QStringList labels;
        for (const Field &f : fields) labels << QString::fromUtf8(f.label);
        m_summaryTable->setHorizontalHeaderLabels(labels);
        m_summaryTable->setRowCount(0);
        for (int i=0;i<m_summary.rows.size();++i) {
            const int row=m_summaryTable->rowCount();
            m_summaryTable->insertRow(row);
            m_displayRowToSummary.append(i);
            for (int col=0;col<fields.size();++col) {
                const QString value=m_summary.value(i,QString::fromLatin1(fields[col].name));
                m_summaryTable->setItem(row,col,tableItem(value));
            }
        }
        m_summaryTable->setColumnWidth(0,175);
        for (int i=1;i<fields.size();++i) m_summaryTable->setColumnWidth(i,95);
        if (m_couplingBatch && fields.size()>10) m_summaryTable->setColumnWidth(10,220);
    }

    bool loadPeaks(const QString &batchDir)
    {
        m_peakById.clear();
        if (!m_couplingBatch) return true;
        CsvUtils::Table data;
        QString error;
        if (!CsvUtils::readFile(QDir(batchDir).filePath(QStringLiteral("all_peaks.csv")),&data,&error)) {
            appendLog(error);
            return false;
        }
        // The exported data is sorted by peak_index within each spectrum id.
        for (int i=0;i<data.rows.size();++i) {
            bool ok1=false,ok2=false,ok3=false,ok4=false;
            MultiLorentzPeak p;
            p.peak_position_nm=data.value(i,QStringLiteral("center_nm")).toDouble(&ok1);
            p.fwhm_nm=data.value(i,QStringLiteral("fwhm_nm")).toDouble(&ok2);
            p.peak_height=data.value(i,QStringLiteral("height")).toDouble(&ok3);
            p.peak_area=data.value(i,QStringLiteral("area")).toDouble(&ok4);
            const QString id=data.value(i,QStringLiteral("id"));
            if (id.isEmpty() || !(ok1&&ok2&&ok3&&ok4)) continue;
            m_peakById[id].append(p);
        }
        return true;
    }

    bool loadBatchDir(const QString &batchDir)
    {
        CsvUtils::Table next;
        QString error;
        if (!CsvUtils::readFile(QDir(batchDir).filePath(QStringLiteral("summary.csv")),&next,&error)) {
            QMessageBox::warning(this,QStringLiteral("加载失败"),error);
            return false;
        }
        m_summary=std::move(next);
        m_currentBatchDir=QDir::cleanPath(batchDir);
        m_couplingBatch=BatchManager::isCouplingBatch(batchDir)
                        || m_summary.column(QStringLiteral("splitting_mev"))>=0;
        loadPeaks(batchDir);
        renderSummary();
        renderStats();
        refreshBatchList(m_currentBatchDir);
        appendLog(QStringLiteral("加载批次：")+m_currentBatchDir);
        m_status->setText(QStringLiteral("已加载 %1（%2 条记录）")
                          .arg(QFileInfo(batchDir).fileName()).arg(m_summary.rows.size()));
        if (m_summaryTable->rowCount()>0) m_summaryTable->selectRow(0);
        else {
            SpectrumChart::clear(m_fitView,m_residualView);
            clearPeakPane();
        }
        return true;
    }

    void clearPeakPane()
    {
        m_peakTable->setRowCount(0);
        m_manualUp->clear(); m_manualLp->clear();
        m_currentPair=CouplingPairMetrics{};
        m_peakInfo->setText(QStringLiteral("此文件没有可用的多峰参数。"));
        m_couplingText->setText(QStringLiteral("ΔE：—"));
    }

    void updateSelectedSpectrum()
    {
        const int viewRow=m_summaryTable->currentRow();
        if (viewRow<0 || viewRow>=m_displayRowToSummary.size()) return;
        const int sourceRow=m_displayRowToSummary[viewRow];
        m_currentFilename=m_summary.value(sourceRow,QStringLiteral("filename"));
        m_currentId=m_couplingBatch ? m_summary.value(sourceRow,QStringLiteral("id")) : QString();
        m_currentWarnings=m_summary.value(sourceRow,QStringLiteral("warnings"));
        QString csvPath;
        if (m_couplingBatch) {
            csvPath=QDir(m_currentBatchDir).filePath(QStringLiteral("curves/")
                        + m_currentId + QStringLiteral("_fit.csv"));
        } else {
            csvPath=QDir(m_currentBatchDir).filePath(QStringLiteral("fits/")
                        + QFileInfo(m_currentFilename).completeBaseName()
                        + QStringLiteral("_fit.csv"));
        }
        QString error;
        if (!SpectrumChart::loadFitCsv(m_fitView,m_residualView,csvPath,&error)) appendLog(error);
        if (m_spectrumName) {
            m_spectrumName->setText(m_currentFilename);
            m_spectrumName->setToolTip(m_currentFilename);
        }
        // Full-size QtCharts title + 7-item bottom legend consume valuable
        // plot height on a small screen. The thin caption replaces both.
        for (auto *view : {m_fitView, m_residualView}) {
            if (!view || !view->chart()) continue;
            view->chart()->setTitle(QString());
            view->chart()->legend()->setVisible(false);
            view->chart()->setMargins(QMargins(1, 1, 1, 1));
        }
        syncChartAxes();
        if (m_couplingBatch) renderPeaks(sourceRow);
        else clearPeakPane();
    }

    void renderPeaks(int sourceRow)
    {
        const auto data=m_peakById.value(m_currentId);
        m_peakInfo->setText(QStringLiteral("文件：%1\n状态：%2\n风险提示：%3")
                .arg(m_currentFilename,
                     m_summary.value(sourceRow,QStringLiteral("status")),
                     m_currentWarnings.isEmpty()?QStringLiteral("无"):m_currentWarnings));
        const QSignalBlocker block1(m_manualUp), block2(m_manualLp);
        m_manualUp->clear();m_manualLp->clear();
        m_peakTable->setRowCount(data.size());
        for (int i=0;i<data.size();++i) {
            const auto &p=data[i];
            const QString text=QStringLiteral("P%1 · %2 nm").arg(i+1).arg(fmt(p.peak_position_nm));
            m_manualUp->addItem(text,i+1); m_manualLp->addItem(text,i+1);
            const QStringList fields={QString::number(i+1),fmt(p.peak_position_nm,3),
                fmt(p.fwhm_nm,3),fmt(p.peak_height,3),fmt(p.peak_area,2)};
            for (int c=0;c<fields.size();++c) m_peakTable->setItem(i,c,tableItem(fields[c]));
        }
        if (data.size()<2) {
            m_currentPair=CouplingPairMetrics{};
            m_couplingText->setText(QStringLiteral("拟合峰不足两个，无法计算 ΔE。"));
            return;
        }
        bool okUp=false,okLp=false;
        const int upIndex=m_summary.value(sourceRow,QStringLiteral("up_index")).toInt(&okUp);
        const int lpIndex=m_summary.value(sourceRow,QStringLiteral("lp_index")).toInt(&okLp);
        m_manualUp->setCurrentIndex(okUp && upIndex>0 && upIndex<=data.size()?upIndex-1:0);
        m_manualLp->setCurrentIndex(okLp && lpIndex>0 && lpIndex<=data.size()?lpIndex-1:data.size()-1);
        recalculatePair();
    }

    void recalculatePair()
    {
        const auto data=m_peakById.value(m_currentId);
        if (!m_couplingBatch || data.size()<2 || m_manualUp->currentIndex()<0 || m_manualLp->currentIndex()<0) {
            m_currentPair=CouplingPairMetrics{};
            m_couplingText->setText(QStringLiteral("请先加载有效的多峰结果。"));
            return;
        }
        CouplingPairSelection selected;
        selected.mode=CouplingPairSelection::Mode::Indices;
        selected.index_a=m_manualUp->currentData().toInt();
        selected.index_b=m_manualLp->currentData().toInt();
        const std::vector<MultiLorentzPeak> peaks(data.begin(),data.end());
        m_currentPair=calculate_coupling_pair(peaks,selected);
        if (!m_currentPair.valid) {
            m_couplingText->setText(QStringLiteral("无法计算：")+QString::fromStdString(m_currentPair.warning));
            return;
        }
        const auto &r=m_currentPair;
        m_couplingText->setText(
            QStringLiteral("UP: P%1  %2 nm  |  LP: P%3  %4 nm\n"
                           "表观分裂 ΔE: %5 meV   |   g_app = ΔE/2: %6 meV\n"
                           "ΓUP: %7 meV  |  ΓLP: %8 meV\n"
                           "ΔE/平均Γ: %9  |  线宽判据: %10\n"
                           "选峰提示: %11\n"
                           "说明：此处为表观谱峰分离，并非已验证的真实耦合强度。")
            .arg(r.up_index).arg(fmt(r.up_nm,2))
            .arg(r.lp_index).arg(fmt(r.lp_nm,2))
            .arg(fmt(r.splitting_mev,3)).arg(fmt(r.apparent_g_mev,3))
            .arg(fmt(r.up_gamma_mev,2)).arg(fmt(r.lp_gamma_mev,2))
            .arg(fmt(r.splitting_to_width_ratio,3))
            .arg(r.linewidth_criterion_pass?QStringLiteral("PASS"):QStringLiteral("FAIL"))
            .arg(r.warning.empty()?QStringLiteral("无"):QString::fromStdString(r.warning)));
    }

    void saveManualPair()
    {
        if (!m_couplingBatch || !m_currentPair.valid) {
            QMessageBox::information(this,QStringLiteral("未计算"),QStringLiteral("请先选择两个有效分峰并重新计算。"));
            return;
        }
        const QString path=QDir(m_currentBatchDir).filePath(QStringLiteral("manual_pairs.csv"));
        const bool hasHeader=QFileInfo(path).exists() && QFileInfo(path).size()>0;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            QMessageBox::warning(this,QStringLiteral("保存失败"),QStringLiteral("无法写入：")+path);
            return;
        }
        QTextStream stream(&file); stream.setCodec("UTF-8");
        if (!hasHeader) stream << "time,id,filename,up_index,lp_index,up_nm,lp_nm,splitting_mev,apparent_g_mev,up_gamma_mev,lp_gamma_mev,linewidth_criterion,warnings\n";
        const auto &r=m_currentPair;
        stream << CsvUtils::escapeField(QDateTime::currentDateTime().toString(Qt::ISODate)) << ','
               << CsvUtils::escapeField(m_currentId) << ','
               << CsvUtils::escapeField(m_currentFilename) << ','
               << r.up_index << ',' << r.lp_index << ','
               << QString::number(r.up_nm,'g',12) << ',' << QString::number(r.lp_nm,'g',12) << ','
               << QString::number(r.splitting_mev,'g',12) << ','
               << QString::number(r.apparent_g_mev,'g',12) << ','
               << QString::number(r.up_gamma_mev,'g',12) << ','
               << QString::number(r.lp_gamma_mev,'g',12) << ','
               << (r.linewidth_criterion_pass?QStringLiteral("PASS"):QStringLiteral("FAIL")) << ','
               << CsvUtils::escapeField(QString::fromStdString(r.warning)) << '\n';
        stream.flush();
        if (stream.status()!=QTextStream::Ok) {
            QMessageBox::warning(this,QStringLiteral("保存失败"),QStringLiteral("写入 CSV 时出错。"));
            return;
        }
        appendLog(QStringLiteral("已保存手动选峰至：")+path);
        m_status->setText(QStringLiteral("已保存手动选峰。原始 summary.csv 未修改。"));
    }

    void renderStats()
    {
        int fitOk=0,review=0,pass=0,validSplits=0;
        double r2Sum=0, splitSum=0;
        int r2Count=0;
        for (int i=0;i<m_summary.rows.size();++i) {
            bool r2Ok=false,splitOk=false;
            const QString r2Text=m_summary.value(i,m_couplingBatch?QStringLiteral("r2"):QStringLiteral("r_squared"));
            const double r2=r2Text.toDouble(&r2Ok);
            if (r2Ok && std::isfinite(r2)) {r2Sum+=r2;++r2Count;}
            if (m_couplingBatch) {
                const QString status=m_summary.value(i,QStringLiteral("status"));
                if (status==QStringLiteral("OK") || status==QStringLiteral("REVIEW")) ++fitOk;
                if (status==QStringLiteral("REVIEW")) ++review;
                if (m_summary.value(i,QStringLiteral("linewidth_criterion"))==QStringLiteral("PASS")) ++pass;
                const double split=m_summary.value(i,QStringLiteral("splitting_mev")).toDouble(&splitOk);
                if (splitOk && std::isfinite(split)) {splitSum+=split;++validSplits;}
            } else {
                if (m_summary.value(i,QStringLiteral("converged"))==QStringLiteral("true")) ++fitOk;
            }
        }
        QVector<QPair<QString,QString>> stats;
        stats.append({QStringLiteral("总光谱数"),QString::number(m_summary.rows.size())});
        stats.append({m_couplingBatch?QStringLiteral("有效数值拟合及选峰"):QStringLiteral("已收敛"),QString::number(fitOk)});
        stats.append({QStringLiteral("平均 R²"),r2Count?fmt(r2Sum/r2Count,5):QStringLiteral("—")});
        if (m_couplingBatch) {
            stats.append({QStringLiteral("需要复核 REVIEW"),QString::number(review)});
            stats.append({QStringLiteral("线宽判据 PASS"),QString::number(pass)});
            stats.append({QStringLiteral("有 ΔE 结果文件数"),QString::number(validSplits)});
            stats.append({QStringLiteral("平均 ΔE (meV)"),validSplits?fmt(splitSum/validSplits,2):QStringLiteral("—")});
        }
        m_statsTable->setRowCount(stats.size());
        for (int i=0;i<stats.size();++i) {
            m_statsTable->setItem(i,0,tableItem(stats[i].first));
            m_statsTable->setItem(i,1,tableItem(stats[i].second));
        }
        m_statsTable->setColumnWidth(0,200);
    }

    void syncChartAxes()
    {
        const auto upper=m_fitView->chart()->axes(Qt::Horizontal);
        const auto lower=m_residualView->chart()->axes(Qt::Horizontal);
        if (upper.isEmpty() || lower.isEmpty()) return;
        auto *a=qobject_cast<QtCharts::QValueAxis *>(upper.first());
        auto *b=qobject_cast<QtCharts::QValueAxis *>(lower.first());
        if (!a || !b) return;
        {
            const QSignalBlocker blocker(b); b->setRange(a->min(),a->max());
        }
        connect(a,&QtCharts::QValueAxis::rangeChanged,b,[b](qreal min,qreal max) {
            const QSignalBlocker blocker(b); b->setRange(min,max);
        });
        connect(b,&QtCharts::QValueAxis::rangeChanged,a,[a](qreal min,qreal max) {
            const QSignalBlocker blocker(a); a->setRange(min,max);
        });
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type()==QEvent::MouseButtonDblClick &&
            (watched==m_fitView || watched==m_residualView)) {
            showLargeChart(static_cast<QtCharts::QChartView *>(watched));
            return true;
        }
        return QMainWindow::eventFilter(watched,event);
    }

    void showLargeChart(QtCharts::QChartView *source)
    {
        if (!source || !source->chart()) return;
        auto *copy=new QtCharts::QChart();
        copy->setTitle(source->chart()->title());
        for (QtCharts::QAbstractSeries *series : source->chart()->series()) {
            auto *line=qobject_cast<QtCharts::QLineSeries *>(series);
            if (!line) continue;
            auto *duplicate=new QtCharts::QLineSeries();
            duplicate->setName(line->name());
            for (const QPointF &p : line->pointsVector()) duplicate->append(p);
            duplicate->setPen(line->pen());
            copy->addSeries(duplicate);
        }
        copy->createDefaultAxes();
        // The enlarged view has more room: show the full per-component legend.
        copy->legend()->setVisible(true);
        copy->legend()->setAlignment(Qt::AlignBottom);
        const auto sourceX=source->chart()->axes(Qt::Horizontal);
        const auto sourceY=source->chart()->axes(Qt::Vertical);
        const auto copyX=copy->axes(Qt::Horizontal);
        const auto copyY=copy->axes(Qt::Vertical);
        if (!sourceX.isEmpty() && !copyX.isEmpty()) {
            auto *from=qobject_cast<QtCharts::QValueAxis *>(sourceX.first());
            auto *to=qobject_cast<QtCharts::QValueAxis *>(copyX.first());
            if (from && to) to->setRange(from->min(),from->max());
        }
        if (!sourceY.isEmpty() && !copyY.isEmpty()) {
            auto *from=qobject_cast<QtCharts::QValueAxis *>(sourceY.first());
            auto *to=qobject_cast<QtCharts::QValueAxis *>(copyY.first());
            if (from && to) to->setRange(from->min(),from->max());
        }
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("放大查看曲线"));
        const QScreen *screen = QGuiApplication::primaryScreen();
        const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1024, 600);
        dialog.resize(qMin(1000, qMax(320, available.width() - 42)),
                      qMin(650, qMax(220, available.height() - 100)));
        auto *layout=new QVBoxLayout(&dialog);
        auto *view=new QtCharts::QChartView(copy,&dialog);
        view->setRenderHint(QPainter::Antialiasing);
        view->setRubberBand(QtCharts::QChartView::RectangleRubberBand);
        layout->addWidget(view);
        dialog.exec();
    }
};

QMainWindow *createMainWindow()
{
    return new MainWindow();
}
