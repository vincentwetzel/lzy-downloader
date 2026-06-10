#include "BaseBinaryUpdater.h"
#include <algorithm>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QJsonArray>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QTimer>
#include <QSaveFile>
#include <chrono>
#include "core/ProcessUtils.h"
#include "core/VersionParser.h"
#include "core/ConfigManager.h"
#include "core/BinaryVerifier.h"

BaseBinaryUpdater::BaseBinaryUpdater(const QString &binaryName, const QString &repoSlug, ConfigManager *configManager, QObject *parent)
    : QObject(parent), m_binaryName(binaryName), m_repoSlug(repoSlug), m_configManager(configManager), m_process(nullptr) {
    m_networkManager = new QNetworkAccessManager(this);
    m_currentLocalVersion = QStringLiteral("0.0.0");
    m_cachedVersion = loadStoredVersion();
}

BaseBinaryUpdater::~BaseBinaryUpdater() {
    stop();
}

void BaseBinaryUpdater::checkForUpdates() {
    fetchVersion();
}

void BaseBinaryUpdater::stop() {
    if (m_process) {
        disconnect(m_process, nullptr, this, nullptr);
        if (m_process->state() != QProcess::NotRunning) {
            ProcessUtils::terminateProcessTree(m_process);
        }
        m_process->deleteLater();
        m_process = nullptr;
    }

    for (auto *reply : m_networkManager->findChildren<QNetworkReply*>()) {
        if (reply->isRunning()) {
            reply->abort();
        }
    }
}

void BaseBinaryUpdater::setVersionParser(VersionParserFunc parser) {
    m_versionParser = std::move(parser);
}

void BaseBinaryUpdater::fetchVersion() {
    if (m_process) {
        return; // Already checking
    }

    m_process = new QProcess(); // No parent
    connect(m_process, &QProcess::finished, this, &BaseBinaryUpdater::onVersionFetchFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit versionFetched(QStringLiteral("Not Found"));
            emit updateFinished(Updater::UpdateStatus::Error, tr("Failed to start %1 to check version.").arg(m_binaryName));
            if (m_process) {
                m_process->deleteLater();
                m_process = nullptr;
            }
        }
    });
    connect(m_process, &QProcess::finished, m_process, &QObject::deleteLater);

    ProcessUtils::FoundBinary binary = ProcessUtils::resolveBinary(m_binaryName, m_configManager);
    if (binary.path.isEmpty() || binary.source == QStringLiteral("Not Found") || binary.source == QStringLiteral("Invalid Custom")) {
        emit versionFetched(QStringLiteral("Not Found"));
        emit updateFinished(Updater::UpdateStatus::Error, tr("%1 executable not found.").arg(m_binaryName));
        m_process->deleteLater();
        m_process = nullptr;
        return;
    }

    ProcessUtils::setProcessEnvironment(*m_process);

    QTimer *watchdog = new QTimer(m_process);
    watchdog->setSingleShot(true);
    QProcess *p = m_process;
    QString name = m_binaryName;
    connect(watchdog, &QTimer::timeout, p, [p, name]() {
        qWarning() << name << "--version timed out. Killing process.";
        if (p->state() != QProcess::NotRunning) p->kill();
    });
    watchdog->start(std::chrono::seconds(10)); // 10 seconds

    const QString versionArg = (m_binaryName == QStringLiteral("ffmpeg") || m_binaryName == QStringLiteral("ffprobe")) ? QStringLiteral("-version") : QStringLiteral("--version");
    m_process->start(binary.path, QStringList{versionArg});
}

void BaseBinaryUpdater::onVersionFetchFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    QProcess *process = qobject_cast<QProcess*>(sender());

    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
        emit versionFetched(QStringLiteral("Error"));
        qWarning() << "Failed to fetch" << m_binaryName << "version:" << (process ? process->readAllStandardError() : QByteArray());
        emit updateFinished(Updater::UpdateStatus::Error, tr("Failed to determine local %1 version.").arg(m_binaryName));
        m_process = nullptr;
        return;
    }

    if (exitCode == 0 && process) {
        QString versionOutput = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
        m_currentLocalVersion = m_versionParser ? m_versionParser(versionOutput) : versionOutput;
        m_cachedVersion = m_currentLocalVersion;
        saveStoredVersion(m_currentLocalVersion);
        emit versionFetched(m_currentLocalVersion);

        QUrl url;
        if (m_repoSlug.startsWith(QStringLiteral("http"))) {
            url = QUrl(m_repoSlug);
        } else {
            url = QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(m_repoSlug));
        }
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LzyDownloader"));
        constexpr int releaseCheckTimeoutMs = 15000;
        request.setTransferTimeout(releaseCheckTimeoutMs);
        QNetworkReply *reply = m_networkManager->get(request);
        connect(reply, &QNetworkReply::finished, this, &BaseBinaryUpdater::onReleaseCheckFinished);
    }
    m_process = nullptr;
}

void BaseBinaryUpdater::onReleaseCheckFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() != QNetworkReply::OperationCanceledError) {
            emit updateFinished(Updater::UpdateStatus::Error, tr("Update check failed: %1").arg(reply->errorString()));
        }
        reply->deleteLater();
        return;
    }

    if (reply->bytesAvailable() > 5 * 1024 * 1024) { // 5MB limit
        emit updateFinished(Updater::UpdateStatus::Error, tr("Update check response is too large."));
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        emit updateFinished(Updater::UpdateStatus::Error, tr("Failed to parse release JSON: %1").arg(parseError.errorString()));
        reply->deleteLater();
        return;
    }

    QJsonObject release = doc.object();

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (statusCode == 403) {
        emit updateFinished(Updater::UpdateStatus::Error, tr("Update check rate-limited by GitHub. Please try again later."));
        reply->deleteLater();
        return;
    } else if (statusCode == 404) {
        emit updateFinished(Updater::UpdateStatus::Error, tr("GitHub release repository not found."));
        reply->deleteLater();
        return;
    }

    m_remoteVersionTag = release.value(QStringLiteral("tag_name")).toString();
    if (m_remoteVersionTag.isEmpty()) {
        emit updateFinished(Updater::UpdateStatus::Error, tr("Invalid release JSON format: missing or empty tag_name."));
        reply->deleteLater();
        return;
    }

    const Version localVersion = Version::parse(m_currentLocalVersion);
    const Version remoteVersion = Version::parse(m_remoteVersionTag);

    if (!(remoteVersion > localVersion)) {
        emit updateFinished(Updater::UpdateStatus::UpToDate, tr("%1 is already up to date (%2).").arg(m_binaryName, localVersion.toString()));
        reply->deleteLater();
        return;
    }

    const QJsonValue assetsVal = release.value(QStringLiteral("assets"));
    if (!assetsVal.isArray()) {
        emit updateFinished(Updater::UpdateStatus::Error, tr("Invalid release JSON format: missing assets."));
        reply->deleteLater();
        return;
    }

    const QJsonArray assets = assetsVal.toArray();
    QUrl downloadUrl;
    m_sha256DownloadUrl.clear();
    m_expectedSha256.clear();

    const QString expectedAssetName = getExpectedAssetName();

    for (const QJsonValue &value : assets) {
        if (value.isObject()) {
            QJsonObject asset = value.toObject();
            const QJsonValue nameVal = asset.value(QStringLiteral("name"));
            if (nameVal.isString()) {
                QString assetName = nameVal.toString();
                if (assetName.compare(expectedAssetName, Qt::CaseInsensitive) == 0) {
                    downloadUrl = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
                    if (asset.contains(QStringLiteral("sha256")) && asset.value(QStringLiteral("sha256")).isString()) {
                        m_expectedSha256 = asset.value(QStringLiteral("sha256")).toString();
                    }
                } else if (assetName.contains(QStringLiteral("SHA256SUMS"), Qt::CaseInsensitive)) {
                    m_sha256DownloadUrl = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
                }
            }
        }
    }

    if (downloadUrl.isValid()) {
        if (m_sha256DownloadUrl.isValid() && m_expectedSha256.isEmpty()) {
            QNetworkRequest request(m_sha256DownloadUrl);
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LzyDownloader"));
            QNetworkReply *shaReply = m_networkManager->get(request);
            shaReply->setProperty("binaryDownloadUrl", downloadUrl);
            shaReply->setProperty("remoteVersion", m_remoteVersionTag);
            connect(shaReply, &QNetworkReply::finished, this, &BaseBinaryUpdater::onSha256DownloadFinished);
        } else {
            initiateBinaryDownload(downloadUrl, m_remoteVersionTag);
        }
    } else {
        emit updateFinished(Updater::UpdateStatus::Error, tr("Target executable not found in release assets."));
    }

    reply->deleteLater();
}

void BaseBinaryUpdater::onSha256DownloadFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    QUrl binaryDownloadUrl = reply->property("binaryDownloadUrl").toUrl();
    QString remoteVersion = reply->property("remoteVersion").toString();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Failed to download SHA256SUMS file:" << reply->errorString();
        m_expectedSha256.clear();
    } else {
        QByteArray sha256Data = reply->readAll();
        const QString expectedAssetName = getExpectedAssetName();

        QStringList lines = QString::fromUtf8(sha256Data).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                QString filename = parts.last();
                if (filename.contains(expectedAssetName, Qt::CaseInsensitive)) {
                    m_expectedSha256 = parts.first();
                    qDebug() << "Found SHA256 hash for" << expectedAssetName << ":" << m_expectedSha256;
                    break;
                }
            }
        }
        if (m_expectedSha256.isEmpty()) {
            qWarning() << "Could not find SHA256 hash for" << expectedAssetName << "in SHA256SUMS file.";
        }
    }
    reply->deleteLater();

    if (binaryDownloadUrl.isValid()) {
        initiateBinaryDownload(binaryDownloadUrl, remoteVersion);
    } else {
        emit updateFinished(Updater::UpdateStatus::Error, tr("Binary download URL was invalid after SHA256 check."));
    }
}

void BaseBinaryUpdater::initiateBinaryDownload(const QUrl &binaryDownloadUrl, const QString &remoteVersion) {
    QNetworkRequest request(binaryDownloadUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LzyDownloader"));
    constexpr int downloadTimeoutMs = 60000;
    request.setTransferTimeout(downloadTimeoutMs);
    QNetworkReply *dlReply = m_networkManager->get(request);
    dlReply->setProperty("newVersion", remoteVersion);
    connect(dlReply, &QNetworkReply::downloadProgress, this, [dlReply](qint64 bytesReceived, qint64 bytesTotal) {
        const qint64 maxSize = 100LL * 1024 * 1024; // 100 MB limit
        if (bytesTotal > maxSize || bytesReceived > maxSize) {
            dlReply->setProperty("payloadTooLarge", true);
            dlReply->abort();
        }
    });
    connect(dlReply, &QNetworkReply::finished, this, &BaseBinaryUpdater::onDownloadFinished);
}

void BaseBinaryUpdater::onDownloadFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (reply->error() != QNetworkReply::NoError) {
        if (reply->property("payloadTooLarge").toBool()) {
            emit updateFinished(Updater::UpdateStatus::Error, tr("Download aborted: payload too large."));
        } else if (reply->error() != QNetworkReply::OperationCanceledError) {
            emit updateFinished(Updater::UpdateStatus::Error, tr("Download failed: %1").arg(reply->errorString()));
        }
        reply->deleteLater();
        return;
    }

    const QString newVersion = m_remoteVersionTag;

    ProcessUtils::FoundBinary binary = ProcessUtils::resolveBinary(m_binaryName, m_configManager);
    QString targetPath = binary.path;

    if (binary.source != QStringLiteral("Custom") && binary.source != QStringLiteral("App Directory") && binary.source != QStringLiteral("Not Found")) {
        emit updateFinished(Updater::UpdateStatus::Error, 
            tr("%1 is managed by %2. Please update it using your package manager or Advanced Settings -> External Tools.").arg(m_binaryName, binary.source));
        reply->deleteLater();
        return;
    }

    if (targetPath.isEmpty() || binary.source == QStringLiteral("Not Found")) {
        QString appDir = QCoreApplication::applicationDirPath();
        const QString ext = getExpectedAssetName();
        targetPath = QDir(appDir).filePath(ext);
    }

    QSaveFile file(targetPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(reply->readAll());
        file.commit();

        if (!m_expectedSha256.isEmpty()) {
            if (!BinaryVerifier::verifyBinaryIntegrity(targetPath, m_expectedSha256)) {
                emit updateFinished(Updater::UpdateStatus::Error, tr("SHA256 verification failed for downloaded binary."));
                reply->deleteLater();
                return;
            }
        }
        if (file.commit()) {
#ifndef Q_OS_WIN
            QFile::setPermissions(targetPath, QFile::permissions(targetPath) | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
#endif

            m_currentLocalVersion = newVersion;
            m_cachedVersion = newVersion;
            saveStoredVersion(newVersion);

            ProcessUtils::clearCache();

            emit versionFetched(m_currentLocalVersion);
            emit updateFinished(Updater::UpdateStatus::UpdateAvailable, tr("%1 updated successfully to %2.").arg(m_binaryName, newVersion));
        } else {
            emit updateFinished(Updater::UpdateStatus::Error, tr("Failed to commit file: %1").arg(file.errorString()));
        }
    } else {
        emit updateFinished(Updater::UpdateStatus::Error, tr("Failed to write file: %1").arg(file.errorString()));
    }

    reply->deleteLater();
}

QString BaseBinaryUpdater::storedVersionPath() const {
    return QDir(m_configManager->getConfigDir()).filePath(m_binaryName + QStringLiteral(".version"));
}

QString BaseBinaryUpdater::loadStoredVersion() const {
    QFile file(storedVersionPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    QString version = QString::fromUtf8(file.readAll()).trimmed();
    return version;
}

void BaseBinaryUpdater::saveStoredVersion(const QString &version) const {
    QSaveFile file(storedVersionPath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(version.toUtf8());
        if (!file.commit()) {
            qWarning() << "Failed to commit" << m_binaryName << "version cache:" << file.errorString();
        }
    } else {
        qWarning() << "Failed to persist" << m_binaryName << "version cache:" << file.errorString();
    }
}

QString BaseBinaryUpdater::getExpectedAssetName() const {
#ifdef Q_OS_WIN
    return m_binaryName + QStringLiteral(".exe");
#elif defined(Q_OS_MACOS)
    return m_binaryName + QStringLiteral("_macos");
#else
    return m_binaryName;
#endif
}