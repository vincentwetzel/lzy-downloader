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
#include <QSaveFile>
#include <QDebug>
#include <QTimer>
#include <chrono>
#include "core/ProcessUtils.h"
#include "core/VersionParser.h"
#include "core/ConfigManager.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

BaseBinaryUpdater::BaseBinaryUpdater(const QString &binaryName, const QString &repoSlug, ConfigManager *configManager, QObject *parent)
    : QObject(parent), m_binaryName(binaryName), m_repoSlug(repoSlug), m_configManager(configManager), m_process(nullptr), m_localVersionOnly(false) {
    m_networkManager = new QNetworkAccessManager(this);
    m_currentLocalVersion = QStringLiteral("0.0.0");
    m_cachedVersion = loadStoredVersion();
}

BaseBinaryUpdater::~BaseBinaryUpdater() {
    stop();
}

void BaseBinaryUpdater::fetchLocalVersionOnly() {
    m_localVersionOnly = true;
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

void BaseBinaryUpdater::checkForUpdate() {
    m_localVersionOnly = false;
    // Force a fresh local version probe first to avoid stale cache from old paths
    disconnect(this, &BaseBinaryUpdater::versionFetched, this, nullptr);
    connect(this, &BaseBinaryUpdater::versionFetched, this, [this](const QString &localVer) {
        disconnect(this, &BaseBinaryUpdater::versionFetched, this, nullptr);
        if (localVer == QStringLiteral("Not Found") || localVer == QStringLiteral("Error")) {
            emit updateFinished(Updater::UpdateStatus::Error, tr("Local binary not found or failed to probe."));
            return;
        }
        performUpdateCheck();
    });
    fetchVersion();
}

void BaseBinaryUpdater::performUpdateCheck() {
    QUrl url;
    if (m_repoSlug.startsWith(QStringLiteral("http"), Qt::CaseInsensitive)) {
        url = QUrl(m_repoSlug);
    } else {
        url = QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(m_repoSlug));
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LzyDownloader-Updater"));
    
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit updateFinished(Updater::UpdateStatus::Error, tr("Network error checking update: %1").arg(reply->errorString()));
            return;
        }

        const QByteArray response = reply->readAll();
        QString remoteVersion;

        if (m_repoSlug.startsWith(QStringLiteral("http"), Qt::CaseInsensitive)) {
            remoteVersion = QString::fromUtf8(response).trimmed();
        } else {
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(response, &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                emit updateFinished(Updater::UpdateStatus::Error, tr("JSON parse error: %1").arg(parseError.errorString()));
                return;
            }

            QJsonObject obj = doc.object();
            remoteVersion = obj.value(QStringLiteral("tag_name")).toString().trimmed();
            if (remoteVersion.isEmpty()) {
                remoteVersion = obj.value(QStringLiteral("name")).toString().trimmed();
            }
        }

        if (remoteVersion.isEmpty()) {
            emit updateFinished(Updater::UpdateStatus::Error, tr("Failed to parse remote version tag."));
            return;
        }

        setProperty("remoteVersionTag", remoteVersion);

        auto normalize = [](QString ver) -> QString {
            ver = ver.trimmed().toLower();
            if (ver.startsWith(QLatin1Char('v'))) ver.remove(0, 1);
            return ver.trimmed();
        };

        const QString normLocal = normalize(m_currentLocalVersion);
        const QString normRemote = normalize(remoteVersion);

        if (normLocal == normRemote) {
            emit updateFinished(Updater::UpdateStatus::UpToDate, tr("%1 is up to date (%2).").arg(m_binaryName, m_currentLocalVersion));
        } else {
            struct VerInfo {
                bool isDate = false;
                QList<int> segments;
            };

            auto parseVer = [](const QString &versionStr) -> VerInfo {
                VerInfo info;
                QString clean = versionStr.trimmed().toLower();
                if (clean.startsWith(QLatin1Char('v'))) {
                    clean.remove(0, 1);
                }

                static const QRegularExpression dateRe(QStringLiteral(R"(^(\d{4})[-.](\d{2})[-.](\d{2}))"));
                QRegularExpressionMatch dateMatch = dateRe.match(clean);
                if (dateMatch.hasMatch()) {
                    info.isDate = true;
                    info.segments << dateMatch.captured(1).toInt();
                    info.segments << dateMatch.captured(2).toInt();
                    info.segments << dateMatch.captured(3).toInt();
                    return info;
                }

                static const QRegularExpression segmentRe(QStringLiteral(R"(\d+)"));
                auto it = segmentRe.globalMatch(clean);
                while (it.hasNext()) {
                    info.segments << it.next().captured().toInt();
                }

                if (!info.segments.isEmpty() && info.segments.first() >= 1900) {
                    info.isDate = true;
                }
                return info;
            };

            auto isNewer = [parseVer, this](const QString &local, const QString &remote) -> bool {
                VerInfo vLocal = parseVer(local);
                VerInfo vRemote = parseVer(remote);

                if (vLocal.segments.isEmpty()) return true;
                if (vRemote.segments.isEmpty()) return false;

                if (m_binaryName == QStringLiteral("ffmpeg") || m_binaryName == QStringLiteral("ffprobe")) {
                    if (vLocal.isDate != vRemote.isDate) {
                        if (vRemote.isDate) {
                            int localMajor = vLocal.segments.first();
                            return localMajor < 5;
                        } else {
                            int remoteMajor = vRemote.segments.first();
                            return remoteMajor >= 5;
                        }
                    }
                }

                int maxLen = qMax(vLocal.segments.size(), vRemote.segments.size());
                for (int i = 0; i < maxLen; ++i) {
                    int localPart = i < vLocal.segments.size() ? vLocal.segments[i] : 0;
                    int remotePart = i < vRemote.segments.size() ? vRemote.segments[i] : 0;
                    if (remotePart > localPart) return true;
                    if (remotePart < localPart) return false;
                }
                return false;
            };

            if (isNewer(normLocal, normRemote)) {
                emit updateFinished(Updater::UpdateStatus::UpdateAvailable, tr("Update available for %1: %2 -> %3").arg(m_binaryName, m_currentLocalVersion, remoteVersion));
            } else {
                emit updateFinished(Updater::UpdateStatus::UpToDate, tr("%1 is up to date (%2, remote: %3).").arg(m_binaryName, m_currentLocalVersion, remoteVersion));
            }
        }
    });
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
#ifdef Q_OS_WIN
    if (binary.path.toLower().contains(QStringLiteral("windowsapps"))) {
        QString cmdLine;
        if (binary.path.contains(QLatin1Char(' '))) {
            cmdLine = QStringLiteral("\"%1\" %2").arg(binary.path, versionArg);
        } else {
            cmdLine = QStringLiteral("%1 %2").arg(binary.path, versionArg);
        }
        m_process->start(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), cmdLine});
    } else {
        m_process->start(binary.path, QStringList{versionArg});
    }
#else
    m_process->start(binary.path, QStringList{versionArg});
#endif
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
        if (m_versionParser) {
            m_currentLocalVersion = m_versionParser(versionOutput);
        } else {
            m_currentLocalVersion = versionOutput.split(QLatin1Char('\n')).first().trimmed();
            if (m_currentLocalVersion.length() > 65) {
                m_currentLocalVersion = m_currentLocalVersion.left(62) + QStringLiteral("...");
            }
        }
        m_cachedVersion = m_currentLocalVersion;
        saveStoredVersion(m_currentLocalVersion);
        emit versionFetched(m_currentLocalVersion);
    }
    m_process = nullptr;
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