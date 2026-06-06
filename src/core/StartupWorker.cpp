#include "StartupWorker.h"
#include "core/ProcessUtils.h"

#include <QDebug>
#include <QMetaObject>

StartupWorker::StartupWorker(ConfigManager *configManager, ExtractorJsonParser *extractorJsonParser, QObject *parent)
    : QObject(parent),
      m_configManager(configManager),
      m_extractorJsonParser(extractorJsonParser),
      m_ytDlpCheckDone(false),
      m_galleryDlCheckDone(false),
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
}

void StartupWorker::start() {
    // Instantiate updaters here so they natively belong to the worker thread's context
    if (!m_ytDlpUpdater) {
        m_ytDlpUpdater = std::make_unique<YtDlpUpdater>(m_configManager);
        connect(m_ytDlpUpdater.get(), &YtDlpUpdater::updateFinished, this, &StartupWorker::onYtDlpUpdateFinished);
        connect(m_ytDlpUpdater.get(), &YtDlpUpdater::versionFetched, this, &StartupWorker::ytDlpVersionFetched);
    }
    
    if (!m_galleryDlUpdater) {
        m_galleryDlUpdater = std::make_unique<GalleryDlUpdater>(m_configManager);
        connect(m_galleryDlUpdater.get(), &GalleryDlUpdater::updateFinished, this, &StartupWorker::onGalleryDlUpdateFinished);
        connect(m_galleryDlUpdater.get(), &GalleryDlUpdater::versionFetched, this, &StartupWorker::galleryDlVersionFetched);
    }

    logBinaryPaths();
    checkBinaries();
    checkYtDlpUpdate();
    checkGalleryDlUpdate();
}

void StartupWorker::logBinaryPaths() {
    const QStringList binaries = {QStringLiteral("yt-dlp"), QStringLiteral("ffmpeg"), QStringLiteral("ffprobe"), QStringLiteral("gallery-dl"), QStringLiteral("aria2c"), QStringLiteral("deno")};
    for (const QString &binary : binaries) {
        const ProcessUtils::FoundBinary foundBinary = ProcessUtils::findBinary(binary, m_configManager);
        if (foundBinary.source == QStringLiteral("Not Found")) {
            qWarning() << "Binary not found:" << binary;
        } else {
            qInfo() << "Binary resolved:" << binary << "source:" << foundBinary.source << "path:" << foundBinary.path;
        }
    }
}

void StartupWorker::checkBinaries() {
    QStringList missing;
    const QStringList requiredBinaries = {QStringLiteral("yt-dlp"), QStringLiteral("ffmpeg"), QStringLiteral("ffprobe"), QStringLiteral("deno")};
    for (const QString &binary : requiredBinaries) {
        QString source = ProcessUtils::findBinary(binary, m_configManager).source;
        if (source == QStringLiteral("Not Found") || source == QStringLiteral("Invalid Custom")) {
            missing << binary;
        }
    }
    emit binariesChecked(missing);
}

void StartupWorker::checkYtDlpUpdate() {
    qInfo() << QStringLiteral("Checking for yt-dlp updates.");
    QMetaObject::invokeMethod(m_ytDlpUpdater.get(), &YtDlpUpdater::checkForUpdates, Qt::QueuedConnection);
}

void StartupWorker::checkGalleryDlUpdate() {
    qInfo() << QStringLiteral("Checking for gallery-dl updates.");
    QMetaObject::invokeMethod(m_galleryDlUpdater.get(), &GalleryDlUpdater::checkForUpdates, Qt::QueuedConnection);
}

void StartupWorker::onYtDlpUpdateFinished(Updater::UpdateStatus status, const QString &message) {
    if (status == Updater::UpdateStatus::UpdateAvailable || status == Updater::UpdateStatus::UpToDate) {
        qInfo() << message;
    } else { // Error
        qWarning() << QStringLiteral("yt-dlp auto-update failed:") << message;
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
    } else { // Error
        qWarning() << QStringLiteral("gallery-dl auto-update failed:") << message;
    }
    m_galleryDlCheckDone = true;
    this->checkAllFinished();
}

void StartupWorker::onExtractorsReady() {
    qInfo() << QStringLiteral("Extractor list generation finished.");
    m_extractorsCheckDone = true;
    this->checkAllFinished();
}

void StartupWorker::checkAllFinished() {
    if (m_ytDlpCheckDone && m_galleryDlCheckDone && m_extractorsCheckDone) {
        qInfo() << QStringLiteral("Startup checks finished.");
        emit finished();
    }
}
