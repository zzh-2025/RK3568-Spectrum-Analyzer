#pragma once

#include <QCryptographicHash>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QProgressBar;
class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;

class OtaUpdateWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit OtaUpdateWidget(const QString &currentVersion, QWidget *parent = nullptr);

private:
    void checkForUpdates();
    void handleManifestReply(QNetworkReply *reply);
    void startPackageDownload();
    void finishPackageDownload(QNetworkReply *reply);
    void setStatus(const QString &message, bool isError = false);
    void setBusy(bool busy);
    bool isServerVersionNewer(const QString &candidate, const QString &current) const;
    bool validateManifest(const QByteArray &payload, QString *error);

    QString m_currentVersion;
    QString m_serverBaseUrl;
    QString m_manifestVersion;
    QString m_packageFileName;
    QString m_expectedSha256;
    QString m_downloadPath;
    QString m_downloadError;
    qint64 m_expectedSize = 0;
    qint64 m_receivedBytes = 0;
    bool m_updateAvailable = false;

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_manifestReply = nullptr;
    QNetworkReply *m_packageReply = nullptr;
    QSaveFile *m_packageFile = nullptr;
    QCryptographicHash *m_hash = nullptr;

    QLineEdit *m_serverEdit = nullptr;
    QLabel *m_currentVersionLabel = nullptr;
    QLabel *m_serverVersionLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_packagePathLabel = nullptr;
    QPushButton *m_checkButton = nullptr;
    QPushButton *m_downloadButton = nullptr;
    QProgressBar *m_progress = nullptr;
};
