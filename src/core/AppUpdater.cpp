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

namespace {

QString normalizeVersionString(QString version)
{
    version = version.trimmed();
    static const QRegularExpression stripPrefixRe(QStringLiteral("^[^0-9]+"));
    version.remove(stripPrefixRe);

    static const QRegularExpression matchRe(QStringLiteral(R"((\d+(?:\.\d+)*))"));
    const QRegularExpressionMatch match = matchRe.match(version);
    return match.hasMatch() ? match.captured(1) : QString();
}

bool isNewerVersion(const QString &latestVersion, const QString &currentVersion)
{
    const QString normalizedLatest = normalizeVersionString(latestVersion);
    const QString normalizedCurrent = normalizeVersionString(currentVersion);

    const QVersionNumber latest = QVersionNumber::fromString(normalizedLatest);
    const QVersionNumber current = QVersionNumber::fromString(normalizedCurrent);

    if (!latest.isNull() && !current.isNull()) {
        return QVersionNumber::compare(latest, current) > 0;
    }

    return normalizedLatest > normalizedCurrent;
}

QUrl selectInstallerAsset(const QJsonArray &assets)
{
    QUrl fallbackExeUrl;

    for (const QJsonValue &value : assets) {
        const QJsonObject asset = value.toObject();
        const QString assetName = asset["name"].toString();
        const QUrl downloadUrl(asset["browser_download_url"].toString());

        if (!assetName.endsWith(".exe", Qt::CaseInsensitive) || !downloadUrl.isValid()) {
            continue;
        }

        if (assetName.startsWith("LzyDownloader-Setup-", Qt::CaseInsensitive)) {
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

    QUrl url(m_repoUrls[m_currentUrlIndex] + "/releases/latest");
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, "LzyDownloader");
    request.setTransferTimeout(15000);
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply](){
        onCheckFinished(reply);
    });
}

void AppUpdater::onCheckFinished(QNetworkReply *reply) {
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Update check failed for URL:" << reply->request().url() 
                   << "Error:" << reply->errorString();
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
    const QString latestVersion = normalizeVersionString(release["tag_name"].toString());
    QString releaseNotes = release["body"].toString();

    if (latestVersion.isEmpty()) {
        emit updateCheckFailed(tr("Latest release did not contain a usable version tag."));
        reply->deleteLater();
        return;
    }

    if (isNewerVersion(latestVersion, m_currentVersion)) {
        const QUrl downloadUrl = selectInstallerAsset(release["assets"].toArray());
        if (downloadUrl.isValid()) {
            emit updateAvailable(latestVersion, releaseNotes, downloadUrl);
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
    request.setHeader(QNetworkRequest::UserAgentHeader, "LzyDownloader");
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

    QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString installerPath = QDir(tempPath).filePath(QStringLiteral("LzyDownloader-Setup.exe"));

    QSaveFile installerFile(installerPath);
    if (!installerFile.open(QIODevice::WriteOnly)) {
        emit updateCheckFailed(tr("Failed to save installer."));
        reply->deleteLater();
        return;
    }

    installerFile.write(reply->readAll());
    installerFile.commit();
    reply->deleteLater();

    emit downloadFinished();

    // Run the installer silently
    QStringList args;
    args << QStringLiteral("/S"); // Silent install
    args << QStringLiteral("/D=%1").arg(QDir::toNativeSeparators(QCoreApplication::applicationDirPath()));

    QProcess::startDetached(installerPath, args);
    QCoreApplication::quit();
}
