#include "StartupWorker.h"
#include "core/ProcessUtils.h"
#include "VersionParser.h"
#include "core/SmartBinaryResolver.h"
#include "core/BaseBinaryUpdater.h"

#include <QDebug> // Ensure this is included
#include <QMetaObject>
#include <QProcess>
#include <QRegularExpression>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>

StartupWorker::StartupWorker(ConfigManager *configManager, ExtractorJsonParser *extractorJsonParser, QObject *parent)
    : QObject(parent),
      m_configManager(configManager),
      m_extractorJsonParser(extractorJsonParser),
      m_ytDlpCheckDone(false),
      m_galleryDlCheckDone(false),
      m_ffmpegCheckDone(false),
      m_denoCheckDone(false),
      m_extractorsCheckDone(false)
{
    if (m_extractorJsonParser) {
        connect(m_extractorJsonParser, &ExtractorJsonParser::extractorsReady, this, &StartupWorker::onExtractorsReady);
    } else {
        qWarning() << "StartupWorker initialized with null ExtractorJsonParser.";
    }
}

StartupWorker::~StartupWorker()
{
    if (m_ytDlpUpdater) {
        m_ytDlpUpdater.release()->deleteLater();
    }
    if (m_galleryDlUpdater) {
        m_galleryDlUpdater.release()->deleteLater();
    }
    if (m_ffmpegUpdater) {
        m_ffmpegUpdater.release()->deleteLater();
    }
    if (m_denoUpdater) {
        m_denoUpdater.release()->deleteLater();
    }
}

void StartupWorker::start() {
    qInfo() << "[StartupWorker::start] >>> STARTUP CHECK SEQUENCE INITIATED <<<";
    // Instantiate updaters here so they natively belong to the worker thread's context
    if (!m_ytDlpUpdater) {
        m_ytDlpUpdater = std::make_unique<BaseBinaryUpdater>(QStringLiteral("yt-dlp"), QStringLiteral("yt-dlp/yt-dlp-nightly-builds"), m_configManager);
        m_ytDlpUpdater->setVersionParser([](const QString &output) {
            return output.trimmed();
        });
        connect(m_ytDlpUpdater.get(), &BaseBinaryUpdater::updateFinished, this, &StartupWorker::onYtDlpUpdateFinished);
        connect(m_ytDlpUpdater.get(), &BaseBinaryUpdater::versionFetched, this, &StartupWorker::ytDlpVersionFetched);
    }
    
    if (!m_galleryDlUpdater) {
        m_galleryDlUpdater = std::make_unique<BaseBinaryUpdater>(QStringLiteral("gallery-dl"), QStringLiteral("mikf/gallery-dl"), m_configManager);
        m_galleryDlUpdater->setVersionParser([](const QString &output) {
            QStringList parts = output.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            return parts.isEmpty() ? QStringLiteral("0.0.0") : parts.last();
        });
        connect(m_galleryDlUpdater.get(), &BaseBinaryUpdater::updateFinished, this, &StartupWorker::onGalleryDlUpdateFinished);
        connect(m_galleryDlUpdater.get(), &BaseBinaryUpdater::versionFetched, this, &StartupWorker::galleryDlVersionFetched);
    }

    if (!m_ffmpegUpdater) {
        m_ffmpegUpdater = std::make_unique<BaseBinaryUpdater>(QStringLiteral("ffmpeg"), QStringLiteral("GyanD/codexffmpeg"), m_configManager);
        m_ffmpegUpdater->setProperty("onlyCheck", true);
        m_ffmpegUpdater->setVersionParser([](const QString &output) {
            QRegularExpression re(QStringLiteral("ffmpeg version (.*?)(?:\\s+Copyright|\\s+built|$)"));
            QRegularExpressionMatch match = re.match(output);
            return match.hasMatch() ? match.captured(1).trimmed() : QString();
        });
        connect(m_ffmpegUpdater.get(), &BaseBinaryUpdater::updateFinished, this, &StartupWorker::onFfmpegUpdateFinished);
    }

    if (!m_denoUpdater) {
        m_denoUpdater = std::make_unique<BaseBinaryUpdater>(QStringLiteral("deno"), QStringLiteral("https://dl.deno.land/release-latest.txt"), m_configManager);
        m_denoUpdater->setProperty("onlyCheck", true);
        m_denoUpdater->setVersionParser([](const QString &output) {
            QRegularExpression re(QStringLiteral("deno\\s+([0-9]+(?:\\.[0-9]+)+)"));
            QRegularExpressionMatch match = re.match(output);
            return match.hasMatch() ? match.captured(1) : QString();
        });
        connect(m_denoUpdater.get(), &BaseBinaryUpdater::updateFinished, this, &StartupWorker::onDenoUpdateFinished);
    }

    logBinaryPaths();
    checkBinaries();
    checkFfmpegAndDenoVersions();
    checkYtDlpUpdate();
    checkGalleryDlUpdate();
}

void StartupWorker::logBinaryPaths() {
    qInfo() << "[StartupWorker::logBinaryPaths] Logging resolved binary locations:";
    const QStringList binaries = {QStringLiteral("yt-dlp"), QStringLiteral("ffmpeg"), QStringLiteral("ffprobe"), QStringLiteral("gallery-dl"), QStringLiteral("aria2c"), QStringLiteral("deno")};
    for (const QString &binary : binaries) {
        const ProcessUtils::FoundBinary foundBinary = SmartBinaryResolver::resolve(binary, m_configManager);
        if (foundBinary.source == QStringLiteral("Not Found")) {
            qWarning() << "[StartupWorker::logBinaryPaths]   Required tool NOT FOUND:" << binary;
        } else {
            qInfo() << "[StartupWorker::logBinaryPaths]   Binary resolved successfully:" << binary << "source:" << foundBinary.source << "path:" << foundBinary.path;
        }
    }
}

void StartupWorker::checkBinaries() {
    qInfo() << "[StartupWorker::checkBinaries] Running missing binaries validation check...";
    QStringList missing;
    const QStringList requiredBinaries = {QStringLiteral("yt-dlp"), QStringLiteral("ffmpeg"), QStringLiteral("ffprobe"), QStringLiteral("deno")};
    for (const QString &binary : requiredBinaries) {
        QString source = SmartBinaryResolver::resolve(binary, m_configManager).source;
        qInfo() << "[StartupWorker::checkBinaries]   Presence check of" << binary << "resolved source:" << source;
        if (source == QStringLiteral("Not Found") || source == QStringLiteral("Invalid Custom")) {
            missing << binary;
        }
    }
    qInfo() << "[StartupWorker::checkBinaries] Missing required binaries list:" << missing;
    emit binariesChecked(missing);
}

void StartupWorker::checkFfmpegAndDenoVersions() {
    qInfo() << "[StartupWorker::checkFfmpegAndDenoVersions] Probing local FFmpeg and Deno versions...";
    if (m_ffmpegUpdater) {
        QMetaObject::invokeMethod(m_ffmpegUpdater.get(), &BaseBinaryUpdater::checkForUpdates, Qt::QueuedConnection);
    } else {
        m_ffmpegCheckDone = true;
    }
    if (m_denoUpdater) {
        QMetaObject::invokeMethod(m_denoUpdater.get(), &BaseBinaryUpdater::checkForUpdates, Qt::QueuedConnection);
    } else {
        m_denoCheckDone = true;
    }
}

void StartupWorker::checkYtDlpUpdate() {
    qInfo() << QStringLiteral("Checking for yt-dlp updates.");
    QMetaObject::invokeMethod(m_ytDlpUpdater.get(), &BaseBinaryUpdater::checkForUpdates, Qt::QueuedConnection);
}

void StartupWorker::checkGalleryDlUpdate() {
    qInfo() << QStringLiteral("Checking for gallery-dl updates.");
    QMetaObject::invokeMethod(m_galleryDlUpdater.get(), &BaseBinaryUpdater::checkForUpdates, Qt::QueuedConnection);
}

void StartupWorker::onYtDlpUpdateFinished(Updater::UpdateStatus status, const QString &message) {
    if (status == Updater::UpdateStatus::UpdateAvailable || status == Updater::UpdateStatus::UpToDate) {
        qInfo() << message;
        if (status == Updater::UpdateStatus::UpdateAvailable) {
            m_configManager->set(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_update_available"), true);
            m_configManager->save();
            emit binaryUpdateRequired(QStringLiteral("yt-dlp"), tr("A newer version of yt-dlp is available. Please update it in settings."));
        } else {
            m_configManager->set(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_update_available"), false);
            m_configManager->save();
        }
    } else { // Error
        qWarning() << QStringLiteral("yt-dlp auto-update failed:") << message;
        emit binaryUpdateRequired(QStringLiteral("yt-dlp"), tr("Update check or auto-update failed: %1").arg(message));
    }
    m_ytDlpCheckDone = true;

    // Now that the update is done, we can safely generate the extractor list.
    qInfo() << QStringLiteral("Starting extractor list generation.");
    
    // Ensure we cross thread boundaries safely, as the parser lives on the main thread
    if (m_extractorJsonParser) {
        QMetaObject::invokeMethod(m_extractorJsonParser, &ExtractorJsonParser::startGeneration, Qt::QueuedConnection);
    } else {
        m_extractorsCheckDone = true;
    }

    this->checkAllFinished();
}

void StartupWorker::onGalleryDlUpdateFinished(Updater::UpdateStatus status, const QString &message) {
    if (status == Updater::UpdateStatus::UpdateAvailable || status == Updater::UpdateStatus::UpToDate) {
        qInfo() << message;
        if (status == Updater::UpdateStatus::UpdateAvailable) {
            m_configManager->set(QStringLiteral("Binaries"), QStringLiteral("gallery-dl_update_available"), true);
            m_configManager->save();
            emit binaryUpdateRequired(QStringLiteral("gallery-dl"), tr("A newer version of gallery-dl is available. Please update it in settings."));
        } else {
            m_configManager->set(QStringLiteral("Binaries"), QStringLiteral("gallery-dl_update_available"), false);
            m_configManager->save();
        }
    } else { // Error
        qWarning() << QStringLiteral("gallery-dl auto-update failed:") << message;
        emit binaryUpdateRequired(QStringLiteral("gallery-dl"), tr("Update check or auto-update failed: %1").arg(message));
    }
    m_galleryDlCheckDone = true;
    this->checkAllFinished();
}

void StartupWorker::onFfmpegUpdateFinished(Updater::UpdateStatus status, const QString &message) {
    if (status == Updater::UpdateStatus::UpdateAvailable) {
        m_configManager->set(QStringLiteral("Binaries"), QStringLiteral("ffmpeg_update_available"), true);
        if (m_ffmpegUpdater) {
            m_configManager->set(QStringLiteral("Binaries"), QStringLiteral("ffmpeg_latest_version"), m_ffmpegUpdater->property("remoteVersionTag").toString());
        }
        m_configManager->save();
        emit binaryUpdateRequired(QStringLiteral("ffmpeg"),
            tr("Installed FFmpeg version is outdated. The latest stable version is %1. Please update it.")
            .arg(m_ffmpegUpdater ? m_ffmpegUpdater->property("remoteVersionTag").toString() : QString()));
    } else if (status == Updater::UpdateStatus::UpToDate) {
        m_configManager->set(QStringLiteral("Binaries"), QStringLiteral("ffmpeg_update_available"), false);
        m_configManager->save();
    }
    m_ffmpegCheckDone = true;
    this->checkAllFinished();
}

void StartupWorker::onDenoUpdateFinished(Updater::UpdateStatus status, const QString &message) {
    if (status == Updater::UpdateStatus::UpdateAvailable) {
        m_configManager->set(QStringLiteral("Binaries"), QStringLiteral("deno_update_available"), true);
        if (m_denoUpdater) {
            m_configManager->set(QStringLiteral("Binaries"), QStringLiteral("deno_latest_version"), m_denoUpdater->property("remoteVersionTag").toString());
        }
        m_configManager->save();
        emit binaryUpdateRequired(QStringLiteral("deno"),
            tr("Installed Deno version is outdated. The latest stable version is %1. Please update it.")
            .arg(m_denoUpdater ? m_denoUpdater->property("remoteVersionTag").toString() : QString()));
    } else if (status == Updater::UpdateStatus::UpToDate) {
        m_configManager->set(QStringLiteral("Binaries"), QStringLiteral("deno_update_available"), false);
        m_configManager->save();
    }
    m_denoCheckDone = true;
    this->checkAllFinished();
}

void StartupWorker::onExtractorsReady() {
    qInfo() << QStringLiteral("Extractor list generation finished.");
    m_extractorsCheckDone = true;
    this->checkAllFinished();
}

void StartupWorker::checkAllFinished() {
    if (m_ytDlpCheckDone && m_galleryDlCheckDone && m_extractorsCheckDone && m_ffmpegCheckDone && m_denoCheckDone) {
        qInfo() << QStringLiteral("Startup checks finished.");
        emit finished();
    }
}
