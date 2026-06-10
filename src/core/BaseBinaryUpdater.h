#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QProcess>
#include <functional>
#include "core/UpdateStatus.h"
#include "core/AppUpdater.h"

class ConfigManager;

class BaseBinaryUpdater : public QObject {
    Q_OBJECT
public:
    explicit BaseBinaryUpdater(const QString &binaryName, const QString &repoSlug, ConfigManager *configManager, QObject *parent = nullptr);
    ~BaseBinaryUpdater() override;

    void fetchLocalVersionOnly();
    void stop();
    void checkForUpdate();

    using VersionParserFunc = std::function<QString(const QString &)>;
    void setVersionParser(VersionParserFunc parser);

    [[nodiscard]] QString storedVersionPath() const;
    [[nodiscard]] QString loadStoredVersion() const;
    void saveStoredVersion(const QString &version) const;
    [[nodiscard]] QString getExpectedAssetName() const;

signals:
    void versionFetched(const QString &version);
    void updateFinished(Updater::UpdateStatus status, const QString &message);

private slots:
    void fetchVersion();
    void onVersionFetchFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:

    QString m_binaryName;
    QString m_repoSlug;
    ConfigManager *m_configManager;
    QNetworkAccessManager *m_networkManager;
    QProcess *m_process;
    VersionParserFunc m_versionParser;

    QString m_currentLocalVersion;
    QString m_cachedVersion;
    bool m_localVersionOnly = false;
};