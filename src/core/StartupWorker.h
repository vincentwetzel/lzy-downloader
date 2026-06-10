#pragma once

#include <QObject>
#include <QStringList>
#include <memory>
#include "ConfigManager.h"
#include "UpdateStatus.h"
#include "utils/ExtractorJsonParser.h"

class BaseBinaryUpdater;

class StartupWorker : public QObject {
    Q_OBJECT

public:
    explicit StartupWorker(ConfigManager *configManager, ExtractorJsonParser *extractorJsonParser,
                           QObject *parent = nullptr);
    ~StartupWorker() override;

public slots:
    void start();

signals:
    void finished();
    void binariesChecked(const QStringList &missingBinaries);
    void binaryUpdateRequired(const QString &binaryName, const QString &details);
    void ytDlpVersionFetched(const QString &version);
    void galleryDlVersionFetched(const QString &version);

private slots:
    void onYtDlpUpdateFinished(Updater::UpdateStatus status, const QString &message);
    void onGalleryDlUpdateFinished(Updater::UpdateStatus status, const QString &message);
    void onFfmpegUpdateFinished(Updater::UpdateStatus status, const QString &message);
    void onDenoUpdateFinished(Updater::UpdateStatus status, const QString &message);
    void onExtractorsReady();

private:
    void logBinaryPaths();
    void checkBinaries();
    void checkYtDlpUpdate();
    void checkFfmpegAndDenoVersions();
    void checkGalleryDlUpdate();
    void checkAllFinished();

    ConfigManager *m_configManager;
    ExtractorJsonParser *m_extractorJsonParser;
    std::unique_ptr<BaseBinaryUpdater> m_ytDlpUpdater;
    std::unique_ptr<BaseBinaryUpdater> m_galleryDlUpdater;
    std::unique_ptr<BaseBinaryUpdater> m_ffmpegUpdater;
    std::unique_ptr<BaseBinaryUpdater> m_denoUpdater;
    bool m_ytDlpCheckDone;
    bool m_galleryDlCheckDone;
    bool m_ffmpegCheckDone;
    bool m_denoCheckDone;
    bool m_extractorsCheckDone;
};
