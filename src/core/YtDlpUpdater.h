#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QProcess>

#include "UpdateStatus.h"

class ConfigManager;

class YtDlpUpdater : public QObject {
    Q_OBJECT

public:
    explicit YtDlpUpdater(ConfigManager *configManager, QObject *parent = nullptr);
    ~YtDlpUpdater();
    void fetchVersion();

public slots:
    void checkForUpdates();
    void stop();

signals:
    void updateAvailable(const QString &version);
    void noUpdateAvailable();
    void updateCheckFailed(const QString &error);
    void updateFinished(Updater::UpdateStatus status, const QString &message);
    void versionFetched(const QString &version);

private slots:
    void onReleaseCheckFinished();
    void onDownloadFinished();
    void onVersionFetchFinished(int exitCode, QProcess::ExitStatus exitStatus);

    bool isVersionNewer(const QString &localVersion, const QString &remoteVersion) const;
    QString normalizeVersion(const QString &version) const;

private:
    QString loadStoredVersion() const;
    void saveStoredVersion(const QString &version) const;
    QString storedVersionPath() const;

    QNetworkAccessManager *m_networkManager;
    QProcess *m_process;
    QString m_currentLocalVersion;
    QString m_cachedVersion;
    ConfigManager *m_configManager;
};

