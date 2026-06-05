#pragma once

#include <QObject>
#include <QStringList>
#include <memory>
#include "ConfigManager.h"
#include "YtDlpUpdater.h"
#include "GalleryDlUpdater.h"
#include "utils/ExtractorJsonParser.h"

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
    void ytDlpVersionFetched(const QString &version);
    void galleryDlVersionFetched(const QString &version);

private slots:
    void onYtDlpUpdateFinished(Updater::UpdateStatus status, const QString &message);
    void onGalleryDlUpdateFinished(Updater::UpdateStatus status, const QString &message);
    void onExtractorsReady();

private:
    void logBinaryPaths();
    void checkBinaries();
    void checkYtDlpUpdate();
    void checkGalleryDlUpdate();
    void checkAllFinished();

    ConfigManager *m_configManager;
    ExtractorJsonParser *m_extractorJsonParser;
    std::unique_ptr<YtDlpUpdater> m_ytDlpUpdater;
    std::unique_ptr<GalleryDlUpdater> m_galleryDlUpdater;
    bool m_ytDlpCheckDone;
    bool m_galleryDlCheckDone;
    bool m_extractorsCheckDone;
};

