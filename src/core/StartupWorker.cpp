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

static void resolveAndValidateBinaries(ConfigManager *configManager, StartupWorker *worker) {
    qInfo() << "[StartupWorker::resolveAndValidateBinaries] Checking and logging binary statuses...";
    QStringList missing;
    const QStringList allBinaries = {QStringLiteral("yt-dlp"), QStringLiteral("ffmpeg"), QStringLiteral("ffprobe"), QStringLiteral("gallery-dl"), QStringLiteral("aria2c"), QStringLiteral("deno")};
    const QStringList requiredBinaries = {QStringLiteral("yt-dlp"), QStringLiteral("ffmpeg"), QStringLiteral("ffprobe"), QStringLiteral("deno")};

    for (const QString &binary : allBinaries) {
        const ProcessUtils::FoundBinary foundBinary = SmartBinaryResolver::resolve(binary, configManager);
        
        if (foundBinary.source == QStringLiteral("Not Found") || foundBinary.source == QStringLiteral("Invalid Custom")) {
            qWarning() << "[StartupWorker::resolveAndValidateBinaries] " << binary << "NOT FOUND or invalid.";
            if (requiredBinaries.contains(binary)) {
                missing << binary;
            }
        } else {
            qInfo() << "[StartupWorker::resolveAndValidateBinaries] resolved successfully:" << binary 
                    << "source:" << foundBinary.source << "path:" << foundBinary.path;
        }
    }
    qInfo() << "[StartupWorker::resolveAndValidateBinaries] Missing required binaries list:" << missing;
    emit worker->binariesChecked(missing);
}

void StartupWorker::start() {
    qInfo() << "[StartupWorker::start] >>> STARTUP CHECK SEQUENCE INITIATED <<<";

    auto handleFinished = [this](const QString &binaryName, Updater::UpdateStatus status, const QString &message, bool onlyCheck, BaseBinaryUpdater* updater, bool &checkDoneFlag, const std::function<void()>& onSuccessExtra = nullptr) {
        if (status == Updater::UpdateStatus::UpdateAvailable || status == Updater::UpdateStatus::UpToDate) {
            bool available = (status == Updater::UpdateStatus::UpdateAvailable);
            m_configManager->set(QStringLiteral("Binaries"), QStringLiteral("%1_update_available").arg(binaryName), available);
            if (onlyCheck) {
                if (available) {
                    QString latestVer = updater->property("remoteVersionTag").toString();
                    m_configManager->set(QStringLiteral("Binaries"), QStringLiteral("%1_latest_version").arg(binaryName), latestVer);
                    m_configManager->save();
                    
                    QString alertMsg;
                    if (binaryName == QStringLiteral("ffmpeg")) {
                        alertMsg = tr("Installed FFmpeg version is outdated. The latest stable version is %1. Please update it.").arg(latestVer);
                    } else if (binaryName == QStringLiteral("deno")) {
                        alertMsg = tr("Installed Deno version is outdated. The latest stable version is %1. Please update it.").arg(latestVer);
                    } else {
                        alertMsg = tr("A newer version of %1 is available. The latest stable version is %2. Please update it.").arg(binaryName, latestVer);
                    }
                    emit binaryUpdateRequired(binaryName, alertMsg);
                } else {
                    m_configManager->save();
                }
            } else {
                m_configManager->save();
                if (available) {
                    emit binaryUpdateRequired(binaryName, tr("A newer version of %1 is available. Please update it in settings.").arg(binaryName));
                } else {
                    qInfo() << message;
                }
            }
            if (onSuccessExtra) {
                onSuccessExtra();
            }
        } else {
            qWarning() << QStringLiteral("%1 auto-update failed:").arg(binaryName) << message;
            emit binaryUpdateRequired(binaryName, tr("Update check or auto-update failed: %1").arg(message));
        }
        checkDoneFlag = true;
        this->checkAllFinished();
    };

    // Inline helper to initialize and connect updaters, eliminating duplicate boilerplate
    auto setupUpdater = [this, &handleFinished](
        std::unique_ptr<BaseBinaryUpdater> &updater,
        const QString &name,
        const QString &repo,
        const std::function<QString(const QString&)> &parser,
        bool onlyCheck,
        bool &checkDoneFlag,
        const std::function<void()>& onSuccessExtra = nullptr)
    {
        if (!updater) {
            updater = std::make_unique<BaseBinaryUpdater>(name, repo, m_configManager);
            updater->setVersionParser(parser);
            if (onlyCheck) {
                updater->setProperty("onlyCheck", true);
            }
            if (name == QStringLiteral("yt-dlp")) {
                connect(updater.get(), &BaseBinaryUpdater::versionFetched, this, &StartupWorker::ytDlpVersionFetched);
            } else if (name == QStringLiteral("gallery-dl")) {
                connect(updater.get(), &BaseBinaryUpdater::versionFetched, this, &StartupWorker::galleryDlVersionFetched);
            }
            connect(updater.get(), &BaseBinaryUpdater::updateFinished, this, [this, name, onlyCheck, ptr = updater.get(), &checkDoneFlag, onSuccessExtra, handleFinished](Updater::UpdateStatus status, const QString &message) {
                handleFinished(name, status, message, onlyCheck, ptr, checkDoneFlag, onSuccessExtra);
            });
        }
    };

    setupUpdater(m_ytDlpUpdater, QStringLiteral("yt-dlp"), QStringLiteral("yt-dlp/yt-dlp-nightly-builds"), [](const QString &output) {
        return output.trimmed();
    }, false, m_ytDlpCheckDone, [this]() {
        qInfo() << QStringLiteral("Starting extractor list generation.");
        if (m_extractorJsonParser) {
            QMetaObject::invokeMethod(m_extractorJsonParser, &ExtractorJsonParser::startGeneration, Qt::QueuedConnection);
        } else {
            m_extractorsCheckDone = true;
        }
    });

    setupUpdater(m_galleryDlUpdater, QStringLiteral("gallery-dl"), QStringLiteral("mikf/gallery-dl"), [](const QString &output) {
        QStringList parts = output.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        return parts.isEmpty() ? QStringLiteral("0.0.0") : parts.last();
    }, false, m_galleryDlCheckDone);

    setupUpdater(m_ffmpegUpdater, QStringLiteral("ffmpeg"), QStringLiteral("GyanD/codexffmpeg"), [](const QString &output) {
        QRegularExpression re(QStringLiteral("ffmpeg version (.*?)(?:\\s+Copyright|\\s+built|$)"));
        QRegularExpressionMatch match = re.match(output);
        return match.hasMatch() ? match.captured(1).trimmed() : QString();
    }, true, m_ffmpegCheckDone);

    setupUpdater(m_denoUpdater, QStringLiteral("deno"), QStringLiteral("https://dl.deno.land/release-latest.txt"), [](const QString &output) {
        QRegularExpression re(QStringLiteral("deno\\s+([0-9]+(?:\\.[0-9]+)+)"));
        QRegularExpressionMatch match = re.match(output);
        return match.hasMatch() ? match.captured(1) : QString();
    }, true, m_denoCheckDone);

    resolveAndValidateBinaries(m_configManager, this);
    checkFfmpegAndDenoVersions();
    checkYtDlpUpdate();
    checkGalleryDlUpdate();
}

void StartupWorker::checkFfmpegAndDenoVersions() {
    qInfo() << "[StartupWorker::checkFfmpegAndDenoVersions] Probing local FFmpeg and Deno versions...";
    if (m_ffmpegUpdater) {
        QMetaObject::invokeMethod(m_ffmpegUpdater.get(), &BaseBinaryUpdater::checkForUpdate, Qt::QueuedConnection);
    } else {
        m_ffmpegCheckDone = true;
    }
    if (m_denoUpdater) {
        QMetaObject::invokeMethod(m_denoUpdater.get(), &BaseBinaryUpdater::checkForUpdate, Qt::QueuedConnection);
    } else {
        m_denoCheckDone = true;
    }
}

void StartupWorker::checkYtDlpUpdate() {
    qInfo() << QStringLiteral("Checking for yt-dlp updates.");
    QMetaObject::invokeMethod(m_ytDlpUpdater.get(), &BaseBinaryUpdater::checkForUpdate, Qt::QueuedConnection);
}

void StartupWorker::checkGalleryDlUpdate() {
    qInfo() << QStringLiteral("Checking for gallery-dl updates.");
    QMetaObject::invokeMethod(m_galleryDlUpdater.get(), &BaseBinaryUpdater::checkForUpdate, Qt::QueuedConnection);
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
