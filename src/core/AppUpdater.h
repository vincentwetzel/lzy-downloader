#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QStringList>
#include "VersionParser.h" // Include the new Version Parser struct

/**
 * @class AppUpdater
 * @brief Handles application update checks against a GitHub repository.
 *
 * This class connects to the GitHub API to check for new releases of the application.
 * It compares the latest release tag with the current application version and emits
 * signals indicating whether an update is available. It also provides a method to
 * download and trigger the installation of the new version.
 */
class AppUpdater : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Constructs an AppUpdater.
     * @param repoUrls A list of GitHub repository URLs (e.g., "https://api.github.com/repos/user/repo") to check for updates. They are tried in order.
     * @param currentVersion The current version of the running application.
     * @param parent The parent QObject.
     */
    explicit AppUpdater(const QStringList &repoUrls, const QString &currentVersion, QObject *parent = nullptr);

    /**
     * @brief Starts the process of checking for a new application update.
     *
     * Results are delivered asynchronously via the `updateAvailable`, `noUpdateAvailable`,
     * or `updateCheckFailed` signals.
     */
    void checkForUpdates();

    /**
     * @brief Downloads the update installer from the given URL and runs it.
     * @param downloadUrl The direct URL to the installer executable.
     */
    void downloadAndInstall(const QUrl &downloadUrl);

signals:
    /**
     * @brief Emitted when a newer version of the application is found.
     * @param latestVersion The version string of the new release.
     * @param releaseNotes The body/notes of the new release.
     * @param downloadUrl The URL to the installer for the new release.
     */
    void updateAvailable(const QString &latestVersion, const QString &releaseNotes, const QUrl &downloadUrl);

    /**
     * @brief Emitted when the application is already up-to-date.
     */
    void noUpdateAvailable();

    /**
     * @brief Emitted when the update check fails.
     * @param error A string describing the reason for the failure.
     */
    void updateCheckFailed(const QString &error);

    /**
     * @brief Emitted periodically during the installer download.
     * @param bytesReceived The number of bytes received so far.
     * @param bytesTotal The total number of bytes to download.
     */
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);

    /**
     * @brief Emitted when the installer has been successfully downloaded.
     */
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