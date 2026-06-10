#include "AppUpdater.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QProcess>
#include <QStandardPaths>
#include <QFile>
#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>
#include <QVersionNumber>
#include <QSaveFile>
#include "VersionParser.h" // Ensure this is included

namespace {
QUrl selectInstallerAsset(const QJsonArray &assets)
{
    QUrl fallbackExeUrl;

    for (const QJsonValue &value : assets) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject asset = value.toObject();
        const QString assetName = asset[QStringLiteral("name")].toString();
        const QUrl downloadUrl(asset[QStringLiteral("browser_download_url")].toString());

        if (!assetName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) || !downloadUrl.isValid()) {
            continue;
        }

        if (assetName.startsWith(QStringLiteral("LzyDownloader-Setup-"), Qt::CaseInsensitive)) {
            return downloadUrl;
        }

        if (!fallbackExeUrl.isValid()) {
            fallbackExeUrl = downloadUrl;
        }
    }

    return fallbackExeUrl;
}

} // namespace

AppUpdater::AppUpdater(const QStringList &repoUrls, const QString &currentVersion, QObject *parent)
    : QObject(parent), m_repoUrls(repoUrls), m_currentVersion(currentVersion), m_currentUrlIndex(0) {

    m_networkManager = new QNetworkAccessManager(this);
}

void AppUpdater::checkForUpdates() {
    m_currentUrlIndex = 0;
    fetchNextUrl();
}

void AppUpdater::fetchNextUrl() {
    if (m_currentUrlIndex >= m_repoUrls.size()) {
        emit updateCheckFailed(tr("Could not find updates at any repository URL."));
        return;
    }

    QUrl url(QStringLiteral("%1/releases/latest").arg(m_repoUrls[m_currentUrlIndex]));
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LzyDownloader"));
    request.setTransferTimeout(15000);
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply](){
        onCheckFinished(reply);
    });
}

void AppUpdater::onCheckFinished(QNetworkReply *reply) {
    if (reply->error() != QNetworkReply::NoError) {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (statusCode == 403) {
            emit updateCheckFailed(tr("Update check rate-limited by GitHub. Please try again later."));
        } else if (statusCode == 404) {
            emit updateCheckFailed(tr("GitHub release repository not found."));
        } else {
        qWarning() << "Update check failed for URL:" << reply->request().url() 
                   << "Error:" << reply->errorString();
        emit updateCheckFailed(tr("Update check failed: %1").arg(reply->errorString()));
        }
        reply->deleteLater();
        
        // Try the next fallback URL
        m_currentUrlIndex++;
        fetchNextUrl();
        return;
    }

    if (reply->bytesAvailable() > 5 * 1024 * 1024) { // 5MB limit
        emit updateCheckFailed(tr("Update check response is too large."));
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        emit updateCheckFailed(tr("Failed to parse release JSON: %1").arg(parseError.errorString()));
        reply->deleteLater();
        return;
    }

    QJsonObject release = doc.object();

    if (!release.contains(QStringLiteral("tag_name")) || !release[QStringLiteral("tag_name")].isString()) {
        emit updateCheckFailed(tr("Invalid release JSON format: missing or empty tag_name."));
        reply->deleteLater();
        return;
    }

    const QString remoteVersionTag = release[QStringLiteral("tag_name")].toString();
    const Version localVersion = Version::parse(m_currentVersion);
    const Version remoteVersion = Version::parse(remoteVersionTag);
    
    QString releaseNotes;
    if (release.contains(QStringLiteral("body")) && release[QStringLiteral("body")].isString()) {
        releaseNotes = release[QStringLiteral("body")].toString();
    }

    if (remoteVersion > localVersion) {
        qInfo() << "Update available! Local:" << localVersion.toString() << "Remote:" << remoteVersion.toString();
        if (!release.contains(QStringLiteral("assets")) || !release[QStringLiteral("assets")].isArray()) {
            emit updateCheckFailed(tr("Invalid release JSON format: missing assets."));
            reply->deleteLater();
            return;
        }
        const QUrl downloadUrl = selectInstallerAsset(release[QStringLiteral("assets")].toArray());
        if (downloadUrl.isValid()) {
            emit updateAvailable(remoteVersionTag, releaseNotes, downloadUrl);
            reply->deleteLater();
            return;
        }

        emit updateCheckFailed(tr("No suitable installer found in the latest release."));
    } else {
        emit noUpdateAvailable();
    }

    reply->deleteLater();
}

void AppUpdater::downloadAndInstall(const QUrl &downloadUrl) {
    QNetworkRequest request(downloadUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LzyDownloader"));
    request.setTransferTimeout(300000); // 5 minutes for installer payload
    QNetworkReply *reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 bytesReceived, qint64 bytesTotal) {
        const qint64 maxSize = 250LL * 1024 * 1024; // 250 MB
        if (bytesTotal > maxSize || bytesReceived > maxSize) {
            reply->setProperty("payloadTooLarge", true);
            reply->abort();
        } else {
            emit downloadProgress(bytesReceived, bytesTotal);
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply](){
        onDownloadFinished(reply);
    });
}

void AppUpdater::onDownloadFinished(QNetworkReply *reply) {
    if (reply->error() != QNetworkReply::NoError) {
        if (reply->property("payloadTooLarge").toBool()) {
            emit updateCheckFailed(tr("Download aborted: payload too large."));
        } else if (reply->error() != QNetworkReply::OperationCanceledError) {
            emit updateCheckFailed(tr("Failed to download update: %1").arg(reply->errorString()));
        }
        reply->deleteLater();
        return;
    }

    const QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString installerPath = QDir(tempPath).filePath(QStringLiteral("LzyDownloader-Setup.exe"));

    QSaveFile installerFile(installerPath);
    if (!installerFile.open(QIODevice::WriteOnly)) {
        emit updateCheckFailed(tr("Failed to save installer."));
        reply->deleteLater();
        return;
    }

    installerFile.write(reply->readAll());
    if (!installerFile.commit()) {
        emit updateCheckFailed(tr("Failed to save installer: %1").arg(installerFile.errorString()));
        reply->deleteLater();
        return;
    }
    reply->deleteLater();

    emit downloadFinished();

    // Run the installer silently
    QStringList args;
    args << QStringLiteral("/S"); // Silent install
    args << QStringLiteral("/D=%1").arg(QDir::toNativeSeparators(QCoreApplication::applicationDirPath()));

    QProcess::startDetached(installerPath, args);
    QCoreApplication::quit();
}
