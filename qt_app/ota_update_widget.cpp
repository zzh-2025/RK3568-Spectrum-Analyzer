#include "ota_update_widget.h"
#include "app_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QStringList>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr qint64 kMaximumPackageSize = 512LL * 1024LL * 1024LL;

bool validVersion(const QString &version)
{
    static const QRegularExpression expression(QStringLiteral(R"(^[0-9]+\.[0-9]+\.[0-9]+$)"));
    return expression.match(version).hasMatch();
}

QString normalizedBaseUrl(const QString &input)
{
    QString value = input.trimmed();
    while (value.endsWith(QLatin1Char('/')))
        value.chop(1);
    return value;
}
} // namespace

OtaUpdateWidget::OtaUpdateWidget(const QString &currentVersion, QWidget *parent)
    : QWidget(parent),
      m_currentVersion(currentVersion),
      m_network(new QNetworkAccessManager(this))
{
    auto *page = new QVBoxLayout(this);
    page->setContentsMargins(10, 8, 10, 8);
    page->setSpacing(8);

    auto *title = new QLabel(QStringLiteral("软件更新"), this);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    title->setFont(titleFont);
    page->addWidget(title);

    auto *serverLabel = new QLabel(QStringLiteral("OTA 服务器地址"), this);
    page->addWidget(serverLabel);

    auto *serverRow = new QHBoxLayout();
    m_serverEdit = new QLineEdit(QStringLiteral("http://192.168.137.1:8000"), this);
    m_serverEdit->setPlaceholderText(QStringLiteral("例如：http://192.168.137.1:8000"));
    serverRow->addWidget(m_serverEdit, 1);
    m_checkButton = new QPushButton(QStringLiteral("检查更新"), this);
    m_checkButton->setMinimumWidth(90);
    serverRow->addWidget(m_checkButton);
    page->addLayout(serverRow);

    auto *versionGrid = new QGridLayout();
    versionGrid->setHorizontalSpacing(12);
    versionGrid->setVerticalSpacing(7);
    versionGrid->addWidget(new QLabel(QStringLiteral("当前版本："), this), 0, 0);
    m_currentVersionLabel = new QLabel(m_currentVersion, this);
    versionGrid->addWidget(m_currentVersionLabel, 0, 1);
    versionGrid->addWidget(new QLabel(QStringLiteral("服务器版本："), this), 1, 0);
    m_serverVersionLabel = new QLabel(QStringLiteral("尚未检查"), this);
    versionGrid->addWidget(m_serverVersionLabel, 1, 1);
    versionGrid->setColumnStretch(1, 1);
    page->addLayout(versionGrid);

    m_statusLabel = new QLabel(QStringLiteral("请确认 Windows OTA 服务器已启动，然后点击“检查更新”。"), this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    page->addWidget(m_statusLabel);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    m_progress->hide();
    page->addWidget(m_progress);

    m_downloadButton = new QPushButton(QStringLiteral("下载并校验安装包"), this);
    m_downloadButton->setMinimumHeight(34);
    m_downloadButton->setEnabled(false);
    page->addWidget(m_downloadButton);

    m_packagePathLabel = new QLabel(this);
    m_packagePathLabel->setWordWrap(true);
    m_packagePathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    page->addWidget(m_packagePathLabel);

    auto *hint = new QLabel(
        QStringLiteral("当前阶段：版本检查、下载校验、自动安装、版本切换及启动失败回滚。"
                       "\n提示：当前使用局域网 HTTP，请仅在可信任的本地网络中测试。"),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #64748b;"));
    page->addWidget(hint);
    page->addStretch(1);

    connect(m_checkButton, &QPushButton::clicked, this, [this]() { checkForUpdates(); });
    connect(m_downloadButton, &QPushButton::clicked, this, [this]() {
        if (m_downloadVerified)
            applyDownloadedUpdate();
        else
            startPackageDownload();
    });
}

void OtaUpdateWidget::setStatus(const QString &message, bool isError)
{
    m_statusLabel->setText(message);
    m_statusLabel->setStyleSheet(isError
        ? QStringLiteral("color: #b91c1c;")
        : QStringLiteral("color: palette(text);"));
}

void OtaUpdateWidget::setBusy(bool busy)
{
    m_checkButton->setEnabled(!busy);
    m_serverEdit->setEnabled(!busy);
    m_downloadButton->setEnabled(!busy && m_updateAvailable && m_packageReply == nullptr);
}

bool OtaUpdateWidget::isServerVersionNewer(const QString &candidate, const QString &current) const
{
    if (!validVersion(candidate) || !validVersion(current))
        return false;

    const QStringList candidateParts = candidate.split(QLatin1Char('.'));
    const QStringList currentParts = current.split(QLatin1Char('.'));
    for (int i = 0; i < 3; ++i) {
        bool candidateOk = false;
        bool currentOk = false;
        const qulonglong candidateValue = candidateParts.at(i).toULongLong(&candidateOk);
        const qulonglong currentValue = currentParts.at(i).toULongLong(&currentOk);
        if (!candidateOk || !currentOk)
            return false;
        if (candidateValue > currentValue)
            return true;
        if (candidateValue < currentValue)
            return false;
    }
    return false;
}

bool OtaUpdateWidget::validateManifest(const QByteArray &payload, QString *error)
{
    if (payload.size() > 64 * 1024) {
        *error = QStringLiteral("manifest.json 超过 64 KB 限制。");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("manifest.json 不是有效 JSON：%1").arg(parseError.errorString());
        return false;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schema_version")).toInt(-1) != 1 ||
        object.value(QStringLiteral("product")).toString() != QStringLiteral("RK3568-Spectrum-Analyzer") ||
        object.value(QStringLiteral("platform")).toString() != QStringLiteral("linux") ||
        object.value(QStringLiteral("architecture")).toString() != QStringLiteral("aarch64")) {
        *error = QStringLiteral("manifest 产品、平台、架构或 schema_version 不匹配。");
        return false;
    }

    const QString version = object.value(QStringLiteral("version")).toString();
    if (!validVersion(version)) {
        *error = QStringLiteral("服务器版本号格式错误，仅支持 x.y.z 格式。");
        return false;
    }

    const QString expectedName = QStringLiteral("rk3568-spectrum-analyzer-%1-linux-aarch64.tar.gz").arg(version);
    const QString packageName = object.value(QStringLiteral("package_file")).toString();
    if (packageName != expectedName || packageName.contains(QLatin1Char('/')) ||
        packageName.contains(QLatin1Char('\\'))) {
        *error = QStringLiteral("安装包文件名与版本号不匹配，或包含非法路径。");
        return false;
    }

    const double sizeValue = object.value(QStringLiteral("size_bytes")).toDouble(-1.0);
    if (sizeValue <= 0.0 || sizeValue > static_cast<double>(kMaximumPackageSize) ||
        sizeValue != static_cast<double>(static_cast<qint64>(sizeValue))) {
        *error = QStringLiteral("安装包大小无效或超过 512 MB 限制。");
        return false;
    }

    const QString sha256 = object.value(QStringLiteral("sha256")).toString().toLower();
    static const QRegularExpression hashExpression(QStringLiteral("^[0-9a-f]{64}$"));
    if (!hashExpression.match(sha256).hasMatch()) {
        *error = QStringLiteral("manifest 中的 SHA-256 格式错误。");
        return false;
    }

    m_manifestVersion = version;
    m_packageFileName = packageName;
    m_expectedSize = static_cast<qint64>(sizeValue);
    m_expectedSha256 = sha256;
    return true;
}

void OtaUpdateWidget::checkForUpdates()
{
    if (m_manifestReply || m_packageReply)
        return;

    m_updateAvailable = false;
    m_downloadVerified = false;
    m_downloadButton->setText(QStringLiteral("下载并校验安装包"));
    m_downloadButton->setEnabled(false);

    const QString baseText = normalizedBaseUrl(m_serverEdit->text());
    const QUrl baseUrl(baseText);
    if ((baseUrl.scheme() != QStringLiteral("http") && baseUrl.scheme() != QStringLiteral("https")) ||
        baseUrl.host().isEmpty() || !baseUrl.userInfo().isEmpty() ||
        !baseUrl.query().isEmpty() || !baseUrl.fragment().isEmpty()) {
        setStatus(QStringLiteral("服务器地址无效。请输入 http:// 或 https:// 开头的地址。"), true);
        return;
    }

    QUrl manifestUrl = baseUrl;
    QString basePath = manifestUrl.path();
    while (basePath.endsWith(QLatin1Char('/')))
        basePath.chop(1);
    manifestUrl.setPath(basePath + QStringLiteral("/manifest.json"));
    m_serverBaseUrl = baseText;

    m_packagePathLabel->clear();
    m_serverVersionLabel->setText(QStringLiteral("正在检查…"));
    m_progress->hide();
    setStatus(QStringLiteral("正在连接 OTA 服务器…"));
    setBusy(true);

    QNetworkRequest request(manifestUrl);
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    m_manifestReply = m_network->get(request);
    connect(m_manifestReply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = m_manifestReply;
        m_manifestReply = nullptr;
        handleManifestReply(reply);
    });
}

void OtaUpdateWidget::handleManifestReply(QNetworkReply *reply)
{
    const QByteArray payload = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    reply->deleteLater();
    setBusy(false);

    if (networkError != QNetworkReply::NoError) {
        m_serverVersionLabel->setText(QStringLiteral("获取失败"));
        setStatus(QStringLiteral("连接 OTA 服务器失败：%1").arg(networkErrorText), true);
        return;
    }

    QString validationError;
    if (!validateManifest(payload, &validationError)) {
        m_serverVersionLabel->setText(QStringLiteral("信息无效"));
        setStatus(validationError, true);
        return;
    }

    m_serverVersionLabel->setText(m_manifestVersion);
    m_updateAvailable = isServerVersionNewer(m_manifestVersion, m_currentVersion);
    m_downloadButton->setEnabled(m_updateAvailable);

    if (m_updateAvailable) {
        setStatus(QStringLiteral("发现新版本 v%1。点击“下载并校验安装包”继续。")
                  .arg(m_manifestVersion));
    } else if (m_manifestVersion == m_currentVersion) {
        setStatus(QStringLiteral("当前已是最新版本 v%1。").arg(m_currentVersion));
    } else {
        setStatus(QStringLiteral("服务器版本 v%1 不高于当前版本 v%2，不执行降级。")
                  .arg(m_manifestVersion, m_currentVersion));
    }
}

void OtaUpdateWidget::startPackageDownload()
{
    if (!m_updateAvailable || m_packageReply || m_manifestVersion.isEmpty())
        return;

    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cacheRoot.isEmpty()) {
        setStatus(QStringLiteral("无法确定应用缓存目录。"), true);
        return;
    }

    const QString updateDir = QDir(cacheRoot).filePath(QStringLiteral("ota-downloads"));
    if (!QDir().mkpath(updateDir)) {
        setStatus(QStringLiteral("无法创建 OTA 下载目录：%1").arg(updateDir), true);
        return;
    }

    m_downloadVerified = false;
    m_downloadButton->setText(QStringLiteral("下载并校验安装包"));
    m_downloadPath = QDir(updateDir).filePath(m_packageFileName);
    m_packageFile = new QSaveFile(m_downloadPath, this);
    if (!m_packageFile->open(QIODevice::WriteOnly)) {
        setStatus(QStringLiteral("无法创建安装包文件：%1").arg(m_packageFile->errorString()), true);
        m_packageFile->deleteLater();
        m_packageFile = nullptr;
        return;
    }

    m_hash = new QCryptographicHash(QCryptographicHash::Sha256);
    m_receivedBytes = 0;
    m_downloadError.clear();
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->show();
    m_packagePathLabel->clear();
    m_downloadButton->setEnabled(false);
    m_checkButton->setEnabled(false);
    m_serverEdit->setEnabled(false);
    setStatus(QStringLiteral("正在下载 v%1 安装包…").arg(m_manifestVersion));

    QUrl packageUrl(m_serverBaseUrl);
    QString packagePath = packageUrl.path();
    while (packagePath.endsWith(QLatin1Char('/')))
        packagePath.chop(1);
    packageUrl.setPath(packagePath + QLatin1Char('/') + m_packageFileName);

    QNetworkRequest request(packageUrl);
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    m_packageReply = m_network->get(request);

    connect(m_packageReply, &QNetworkReply::readyRead, this, [this]() {
        if (!m_packageReply || !m_packageFile || !m_hash)
            return;
        const QByteArray chunk = m_packageReply->readAll();
        if (m_receivedBytes + chunk.size() > m_expectedSize) {
            m_downloadError = QStringLiteral("下载数据超过 manifest 声明的文件大小，已中止。");
            m_packageReply->abort();
            return;
        }
        if (m_packageFile->write(chunk) != chunk.size()) {
            m_downloadError = QStringLiteral("写入安装包失败：%1").arg(m_packageFile->errorString());
            m_packageReply->abort();
            return;
        }
        m_hash->addData(chunk);
        m_receivedBytes += chunk.size();
    });

    connect(m_packageReply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
        if (total > 0) {
            m_progress->setRange(0, 100);
            m_progress->setValue(static_cast<int>((received * 100) / total));
        } else {
            m_progress->setRange(0, 0);
        }
    });

    connect(m_packageReply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = m_packageReply;
        m_packageReply = nullptr;
        finishPackageDownload(reply);
    });
}

void OtaUpdateWidget::finishPackageDownload(QNetworkReply *reply)
{
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    const QByteArray tail = reply->readAll();
    reply->deleteLater();

    if (m_packageFile && !tail.isEmpty()) {
        if (m_receivedBytes + tail.size() > m_expectedSize) {
            m_downloadError = QStringLiteral("下载数据超过 manifest 声明的文件大小。");
        } else if (m_packageFile->write(tail) != tail.size()) {
            m_downloadError = QStringLiteral("写入安装包失败：%1").arg(m_packageFile->errorString());
        } else {
            m_hash->addData(tail);
            m_receivedBytes += tail.size();
        }
    }

    QString failure;
    if (!m_downloadError.isEmpty()) {
        failure = m_downloadError;
    } else if (networkError != QNetworkReply::NoError) {
        failure = QStringLiteral("下载失败：%1").arg(networkErrorText);
    } else if (m_receivedBytes != m_expectedSize) {
        failure = QStringLiteral("文件大小不匹配：期望 %1 bytes，实际 %2 bytes。")
                  .arg(m_expectedSize).arg(m_receivedBytes);
    } else if (m_hash->result().toHex() != m_expectedSha256.toLatin1()) {
        failure = QStringLiteral("SHA-256 校验失败，安装包可能损坏或已被替换。");
    }

    if (failure.isEmpty() && (!m_packageFile || !m_packageFile->commit()))
        failure = QStringLiteral("无法保存已下载的安装包。");

    if (m_packageFile) {
        if (!failure.isEmpty())
            m_packageFile->cancelWriting();
        m_packageFile->deleteLater();
        m_packageFile = nullptr;
    }
    delete m_hash;
    m_hash = nullptr;

    m_progress->setRange(0, 100);
    m_progress->setValue(failure.isEmpty() ? 100 : 0);
    m_checkButton->setEnabled(true);
    m_serverEdit->setEnabled(true);

    if (!failure.isEmpty()) {
        m_downloadVerified = false;
        m_downloadButton->setText(QStringLiteral("下载并校验安装包"));
        m_downloadButton->setEnabled(m_updateAvailable);
        QFile::remove(m_downloadPath);
        setStatus(failure, true);
        return;
    }

    m_downloadVerified = true;
    m_packagePathLabel->setText(QStringLiteral("已下载并通过 SHA-256 校验：\n%1").arg(m_downloadPath));
    m_downloadButton->setText(QStringLiteral("安装更新并重启"));
    m_downloadButton->setEnabled(true);
    setStatus(QStringLiteral("安装包下载完成，大小和 SHA-256 均验证通过。点击“安装更新并重启”继续。"));
}

void OtaUpdateWidget::applyDownloadedUpdate()
{
    if (!m_downloadVerified || m_downloadPath.isEmpty() || m_manifestVersion.isEmpty())
        return;

    const QString installRoot =
        QDir::home().filePath(QStringLiteral(".local/opt/rk3568-spectrum-analyzer"));
    const QString currentRoot =
        QFileInfo(QDir(installRoot).filePath(QStringLiteral("current"))).canonicalFilePath();
    const QString runningRoot =
        QFileInfo(AppPaths::applicationRoot()).canonicalFilePath();

    if (currentRoot.isEmpty() || runningRoot.isEmpty() || currentRoot != runningRoot) {
        QMessageBox::information(
            this,
            QStringLiteral("需要先安装正式版本"),
            QStringLiteral("当前程序不是从正式安装目录启动的。\n\n"
                           "请先安装当前稳定版本到：\n%1\n\n"
                           "然后从该目录的 current/launch.sh 启动程序，再执行 OTA 更新。")
                .arg(installRoot));
        return;
    }

    const QString helperPath = QDir(runningRoot).filePath(QStringLiteral("ota_apply_update.sh"));
    const QFileInfo helperInfo(helperPath);
    if (!helperInfo.isFile() || !helperInfo.isExecutable()) {
        QMessageBox::warning(
            this,
            QStringLiteral("升级组件缺失"),
            QStringLiteral("当前安装版本没有 ota_apply_update.sh。请重新打包并安装包含 OTA 安装器的基线版本。"));
        return;
    }

    if (!QFileInfo(m_downloadPath).isFile()) {
        m_downloadVerified = false;
        m_downloadButton->setText(QStringLiteral("下载并校验安装包"));
        m_downloadButton->setEnabled(true);
        setStatus(QStringLiteral("已下载的安装包不存在，请重新下载。"), true);
        return;
    }

    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("确认安装更新"),
        QStringLiteral("即将安装版本 v%1。\n\n"
                       "程序将自动关闭，切换版本并启动新程序。\n"
                       "如果新版本未能通过启动健康检查，将自动恢复旧版本。\n\n"
                       "实验数据和分析结果保存在版本目录之外，不会被本次升级覆盖。\n\n"
                       "现在继续吗？")
            .arg(m_manifestVersion),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (answer != QMessageBox::Yes)
        return;

    const QString helperProgram = QStringLiteral("/bin/bash");
    const QStringList arguments = {
        helperPath,
        m_downloadPath,
        m_manifestVersion,
        m_expectedSha256,
        QString::number(m_expectedSize),
        QString::number(QCoreApplication::applicationPid())
    };

    if (!QProcess::startDetached(helperProgram, arguments, runningRoot)) {
        QMessageBox::warning(
            this,
            QStringLiteral("无法启动升级程序"),
            QStringLiteral("OTA 安装器启动失败，请检查安装目录权限和日志。"));
        return;
    }

    m_checkButton->setEnabled(false);
    m_downloadButton->setEnabled(false);
    m_serverEdit->setEnabled(false);
    setStatus(QStringLiteral("升级程序已启动。当前窗口即将关闭；请等待新版本启动。"));
    QTimer::singleShot(500, this, []() {
        QCoreApplication::quit();
    });
}
