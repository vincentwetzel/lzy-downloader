#include "AppUpdater.h"

#include "VersionParser.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVersionNumber>

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

#ifdef Q_OS_WIN
        const QString expectedExt = QStringLiteral(".exe");
#elif defined(Q_OS_LINUX)
        const QString expectedExt = QStringLiteral(".AppImage");
#else
        const QString expectedExt = QStringLiteral(".dmg");
#endif

        if (!assetName.endsWith(expectedExt, Qt::CaseInsensitive) || !downloadUrl.isValid()) {
            continue;
        }

        if (assetName.startsWith(QStringLiteral("LzyDownloader-"), Qt::CaseInsensitive)) {
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
            qWarning() << "Update check rate-limited for URL:" << reply->request().url();
        } else if (statusCode == 404) {
            qWarning() << "Update check repository not found for URL:" << reply->request().url();
        } else {
            qWarning() << "Update check failed for URL:" << reply->request().url() << "Error:" << reply->errorString();
        }
        reply->deleteLater();
        
        // Try the next fallback URL quietly
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
#ifdef Q_OS_WIN
    const QString installerPath = QDir(tempPath).filePath(QStringLiteral("LzyDownloader-Setup.exe"));
#elif defined(Q_OS_LINUX)
    const QString installerPath = QDir(tempPath).filePath(QStringLiteral("LzyDownloader-Update.AppImage"));
#else
    const QString installerPath = QDir(tempPath).filePath(QStringLiteral("LzyDownloader-Update"));
#endif

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

#ifdef Q_OS_WIN
    // Run the installer silently
    QStringList args;
    args << QStringLiteral("/S"); // Silent install
    args << QStringLiteral("/D=%1").arg(QDir::toNativeSeparators(QCoreApplication::applicationDirPath()));

    QProcess::startDetached(installerPath, args);
#else
    // Make the downloaded AppImage/binary executable and launch it
    QFile::setPermissions(installerPath, QFile::permissions(installerPath) | QFileDevice::ExeUser);
    QProcess::startDetached(installerPath, QStringList());
#endif
    QCoreApplication::quit();
}
