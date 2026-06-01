#include "GalleryDlUpdater.h"
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
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QSaveFile>
#include "core/ProcessUtils.h"
#include "core/ConfigManager.h"

GalleryDlUpdater::GalleryDlUpdater(ConfigManager *configManager, QObject *parent) : QObject(parent), m_configManager(configManager), m_process(nullptr) {
    m_networkManager = new QNetworkAccessManager(this);
    m_currentLocalVersion = "0.0.0";
    m_cachedVersion = loadStoredVersion();
}

GalleryDlUpdater::~GalleryDlUpdater() {
    stop();
}

void GalleryDlUpdater::checkForUpdates() {
    fetchVersion();
}

void GalleryDlUpdater::stop() {
    if (m_process) {
        disconnect(m_process, nullptr, this, nullptr);
        if (m_process->state() != QProcess::NotRunning) {
            ProcessUtils::terminateProcessTree(m_process);
        }
        m_process->deleteLater();
        m_process = nullptr;
    }

    for (auto reply : m_networkManager->findChildren<QNetworkReply*>()) {
        if (reply->isRunning()) {
            reply->abort();
        }
    }
}

void GalleryDlUpdater::fetchVersion() {
    if (m_process) {
        return; // Already checking
    }

    m_process = new QProcess(this);
    connect(m_process, &QProcess::finished, this, &GalleryDlUpdater::onVersionCheckFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit versionFetched("Not Found");
            emit updateFinished(Updater::UpdateStatus::Error, tr("Failed to start gallery-dl to check version."));
            if (m_process) {
                m_process->deleteLater();
                m_process = nullptr;
            }
        }
    });
    connect(m_process, &QProcess::finished, m_process, &QObject::deleteLater);

    ProcessUtils::setProcessEnvironment(*m_process);

    ProcessUtils::FoundBinary binary = ProcessUtils::resolveBinary("gallery-dl", m_configManager);
    if (binary.path.isEmpty() || binary.source == "Not Found" || binary.source == "Invalid Custom") {
        emit versionFetched("Not Found");
        emit updateFinished(Updater::UpdateStatus::Error, tr("gallery-dl executable not found."));
        m_process->deleteLater();
        m_process = nullptr;
        return;
    }

    m_process->start(binary.path, {"--version"});
}

void GalleryDlUpdater::onReleaseCheckFinished() {
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

    if (!release.contains("tag_name") || !release["tag_name"].isString()) {
        emit updateFinished(Updater::UpdateStatus::Error, tr("Invalid release JSON format: missing tag_name."));
        reply->deleteLater();
        return;
    }

    QString rawRemoteVersion = release["tag_name"].toString();
    QString normalizedRemoteVersion = normalizeVersion(rawRemoteVersion);
    QString remoteVersionForDisplay = normalizedRemoteVersion.isEmpty() ? rawRemoteVersion : normalizedRemoteVersion;
    QString comparisonVersion = m_cachedVersion.isEmpty() ? m_currentLocalVersion : m_cachedVersion;

    if (!isVersionNewer(comparisonVersion, normalizedRemoteVersion)) {
        emit updateFinished(Updater::UpdateStatus::UpToDate, tr("gallery-dl is already up to date (%1).").arg(comparisonVersion));
        reply->deleteLater();
        return;
    }

    if (!release.contains("assets") || !release["assets"].isArray()) {
        emit updateFinished(Updater::UpdateStatus::Error, tr("Invalid release JSON format: missing assets."));
        reply->deleteLater();
        return;
    }

    QJsonArray assets = release["assets"].toArray();
    QUrl downloadUrl;
    for (const QJsonValue &value : assets) {
        QJsonObject asset = value.toObject();
        if (asset["name"].toString() == "gallery-dl.exe") {
            downloadUrl = QUrl(asset["browser_download_url"].toString());
            break;
        }
    }

    if (downloadUrl.isValid()) {
        QNetworkRequest request(downloadUrl);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader, "LzyDownloader");
        request.setTransferTimeout(60000);
        QNetworkReply *dlReply = m_networkManager->get(request);
        dlReply->setProperty("newVersion", remoteVersionForDisplay);
        connect(dlReply, &QNetworkReply::downloadProgress, this, [dlReply](qint64 bytesReceived, qint64 bytesTotal) {
            const qint64 maxSize = 100LL * 1024 * 1024; // 100 MB limit
            if (bytesTotal > maxSize || bytesReceived > maxSize) {
                dlReply->setProperty("payloadTooLarge", true);
                dlReply->abort();
            }
        });
        connect(dlReply, &QNetworkReply::finished, this, &GalleryDlUpdater::onDownloadFinished);
    } else {
        emit updateFinished(Updater::UpdateStatus::Error, tr("gallery-dl.exe not found in release assets."));
    }

    reply->deleteLater();
}

void GalleryDlUpdater::onDownloadFinished() {
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

    QString newVersion = reply->property("newVersion").toString();

    ProcessUtils::FoundBinary binary = ProcessUtils::resolveBinary("gallery-dl", m_configManager);
    QString targetPath = binary.path;

    // Prevent corruption of package-managed system environments
    if (binary.source != "Custom" && binary.source != "App Directory" && binary.source != "Not Found") {
        emit updateFinished(Updater::UpdateStatus::Error, 
            tr("gallery-dl is managed by %1. Please update it using your package manager or Advanced Settings -> External Tools.").arg(binary.source));
        reply->deleteLater();
        return;
    }

    if (targetPath.isEmpty() || binary.source == "Not Found") {
        QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
        targetPath = QDir(appDir).filePath(QStringLiteral("gallery-dl.exe"));
#else
        targetPath = QDir(appDir).filePath(QStringLiteral("gallery-dl"));
#endif
    }

    QSaveFile file(targetPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(reply->readAll());
        if (file.commit()) {
#ifndef Q_OS_WIN
            // Ensure it's executable on Unix platforms
            QFile::setPermissions(targetPath, QFile::permissions(targetPath) | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
#endif

        m_currentLocalVersion = newVersion;
        m_cachedVersion = newVersion;
        saveStoredVersion(newVersion);

        ProcessUtils::clearCache(); // Ensure next run uses the freshly downloaded binary

        emit versionFetched(m_currentLocalVersion);
        emit updateFinished(Updater::UpdateStatus::UpdateAvailable, tr("gallery-dl updated successfully to %1.").arg(newVersion));
        } else {
            emit updateFinished(Updater::UpdateStatus::Error, tr("Failed to commit file: %1").arg(file.errorString()));
        }
    } else {
        emit updateFinished(Updater::UpdateStatus::Error, tr("Failed to write file: %1").arg(file.errorString()));
    }

    reply->deleteLater();
}

void GalleryDlUpdater::onVersionCheckFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    QProcess *process = qobject_cast<QProcess*>(sender());

    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
        emit versionFetched("Error");
        qWarning() << "Failed to fetch gallery-dl version:" << (process ? process->readAllStandardError() : QByteArray());
        emit updateFinished(Updater::UpdateStatus::Error, tr("Failed to determine local gallery-dl version."));
        m_process = nullptr;
        return;
    }

    if (exitCode == 0) {
        QString versionOutput = process->readAllStandardOutput().trimmed();
        QStringList parts = versionOutput.split(' ');
        m_currentLocalVersion = parts.isEmpty() ? "0.0.0" : parts.last();
        m_cachedVersion = m_currentLocalVersion;
        saveStoredVersion(m_currentLocalVersion);
        emit versionFetched(m_currentLocalVersion);

        QUrl url("https://api.github.com/repos/mikf/gallery-dl/releases/latest");
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader, "LzyDownloader");
        request.setTransferTimeout(15000);
        QNetworkReply *reply = m_networkManager->get(request);
        connect(reply, &QNetworkReply::finished, this, &GalleryDlUpdater::onReleaseCheckFinished);
    }
    m_process = nullptr;
}

bool GalleryDlUpdater::isVersionNewer(const QString &local, const QString &remote) const {
    QString normalizedLocal = normalizeVersion(local);
    QString normalizedRemote = normalizeVersion(remote);
    if (normalizedRemote.isEmpty()) return false;
    if (normalizedLocal.isEmpty()) return true;
    QStringList localParts = normalizedLocal.split('.', Qt::SkipEmptyParts);
    QStringList remoteParts = normalizedRemote.split('.', Qt::SkipEmptyParts);
    for (int i = 0; i < std::min(localParts.size(), remoteParts.size()); ++i) {
        int localNum = localParts[i].toInt();
        int remoteNum = remoteParts[i].toInt();
        if (remoteNum > localNum) return true;
        if (remoteNum < localNum) return false;
    }
    return remoteParts.size() > localParts.size();
}

QString GalleryDlUpdater::normalizeVersion(const QString &version) const {
    QString trimmed = version.trimmed();
    if (trimmed.isEmpty()) return QString();
    static const QRegularExpression regex(R"(\d+(?:\.\d+)*)");
    QRegularExpressionMatch match = regex.match(trimmed);
    return match.hasMatch() ? match.captured(0) : trimmed;
}

QString GalleryDlUpdater::storedVersionPath() const {
    return QDir(QCoreApplication::applicationDirPath()).filePath("gallery-dl.version");
}

QString GalleryDlUpdater::loadStoredVersion() const {
    QFile file(storedVersionPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    QString version = QString::fromUtf8(file.readAll()).trimmed();
    return version;
}

void GalleryDlUpdater::saveStoredVersion(const QString &version) const {
    QSaveFile file(storedVersionPath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(version.toUtf8());
        if (!file.commit()) {
            qWarning() << "Failed to commit gallery-dl version cache:" << file.errorString();
        }
    } else {
        qWarning() << "Failed to persist gallery-dl version cache:" << file.errorString();
    }
}
