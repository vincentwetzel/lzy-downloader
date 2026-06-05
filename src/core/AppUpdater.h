#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QStringList>

class AppUpdater : public QObject {
    Q_OBJECT

public:
    explicit AppUpdater(const QStringList &repoUrls, const QString &currentVersion, QObject *parent = nullptr);
    void checkForUpdates();
    void downloadAndInstall(const QUrl &downloadUrl);

signals:
    void updateAvailable(const QString &latestVersion, const QString &releaseNotes, const QUrl &downloadUrl);
    void noUpdateAvailable();
    void updateCheckFailed(const QString &error);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void downloadFinished();

private slots:
    void onCheckFinished(QNetworkReply *reply);
    void onDownloadFinished(QNetworkReply *reply);

private:
    void fetchNextUrl();

    QStringList m_repoUrls;
    int m_currentUrlIndex;
    QString m_currentVersion;
    QNetworkAccessManager *m_networkManager;
};