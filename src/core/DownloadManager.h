#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <QObject>
#include <QQueue>
#include <QMap>
#include <QTimer>
#include <QJsonArray>
#include "ConfigManager.h"
#include "DownloadItem.h"

class SortingManager;
class ArchiveManager;
class PlaylistExpander;
class DownloadFinalizer;
class DownloadQueueManager;
class DownloadQueueState;

class DownloadManager : public QObject {
    Q_OBJECT

public:
    explicit DownloadManager(ConfigManager *configManager, QObject *parent = nullptr);
    ~DownloadManager();

    enum DuplicateStatus {
        NotDuplicate,
        DuplicateInQueue,
        DuplicateActive,
        DuplicatePaused,
        DuplicateCompleted
    };
    Q_ENUM(DuplicateStatus)

    void enqueueDownload(const QString &url, const QVariantMap &options);
    void cancelDownload(const QString &id);
    void pauseDownload(const QString &id);
    void unpauseDownload(const QString &id);
    void restartDownloadWithOptions(const QVariantMap &itemData);
    void retryDownload(const QVariantMap &itemData);
    void resumeDownload(const QVariantMap &itemData);
    void finishDownload(const QString &id);
    void moveDownloadUp(const QString &id);
    void moveDownloadDown(const QString &id);
    void onWorkerOutputReceived(const QString &id, const QString &output);
    void processPlaylistSelection(const QString &url, const QString &action, const QVariantMap &options, const QList<QVariantMap> &expandedItems);
    void resumeDownloadWithFormat(const QString &url, const QVariantMap &options, const QString &formatId);
    void shutdown();

public slots:
    void onItemCleared(const QString &id, bool wasSuccessful, bool wasFinished);

signals:
    void downloadAddedToQueue(const QVariantMap &itemData);
    void downloadStarted(const QString &id);
    void downloadPaused(const QString &id);
    void downloadResumed(const QString &id);
    void downloadProgress(const QString &id, const QVariantMap &progressData);
    void playlistActionRequested(const QString &url, int itemCount, const QVariantMap &options, const QList<QVariantMap> &expandedItems);
    void resumeDownloadsRequested(const QJsonArray &arr);
    void downloadCancelled(const QString &id);
    void downloadFinalPathReady(const QString &id, const QString &path);
    void playlistExpansionStarted(const QString &url);
    void playlistExpansionFinished(const QString &url, int count);
    void queueFinished();
    void totalSpeedUpdated(double speed);
    void videoQualityWarning(const QString &url, const QString &message);
    void downloadStatsUpdated(int queued, int active, int completed, int errors);
    void formatSelectionRequested(const QString &url, const QVariantMap &options, const QVariantMap &infoDict);
    void formatSelectionFailed(const QString &url, const QString &message);
    void downloadSectionsRequested(const QString &url, const QVariantMap &options, const QVariantMap &infoJson);
    void ytDlpErrorPopupRequested(const QString &id, const QString &errorType, const QString &userMessage, const QString &rawError, const QVariantMap &itemData);
    void duplicateDownloadDetected(const QString &url, const QString &reason);
    void downloadFinished(const QString &id, bool success, const QString &message);
    void downloadRemovedFromQueue(const QString &id);

private slots:
    void onPlaylistDetected(const QString &url, int itemCount, const QVariantMap &options, const QList<QVariantMap> &expandedItems);
    void onPlaylistExpanded(const QString &originalUrl, const QList<QVariantMap> &expandedItems, const QString &error);
    void onPlaylistExpansionPlaceholderRemoved(const QString &id);
    void onPlaylistExpansionPlaceholderUpdated(const QString &id, const QVariantMap &itemData);
    void onConfigSettingChanged(const QString &section, const QString &key, const QVariant &value);
    void startNextDownload();
    void onSleepTimerTimeout();
    void onWorkerProgress(const QString &id, const QVariantMap &progressData);
    void onWorkerFinished(const QString &id, bool success, const QString &message, const QString &finalFilename, const QString &originalDownloadedFilename, const QVariantMap &metadata);
    void onGalleryDlWorkerFinished(const QString &id, bool success, const QString &message, const QString &finalFilename, const QVariantMap &metadata);
    void onMetadataEmbedded(const QString &id, bool success, const QString &error);
    void onYtDlpErrorDetected(const QString &id, const QString &errorType, const QString &userMessage, const QString &rawError);
    void onFinalizationComplete(const QString &id, bool success, const QString &message);
    void onQueueCountsChanged(int queued, int paused);
    void onRequestStartNextDownload();

private:
    void applyMaxConcurrentSetting(const QString &maxThreadsStr);
    void proceedWithDownload();
    void startDownloadItem(DownloadItem item, bool alreadyCountedActive = false);
    void startDownloadsToCapacity();
    void checkQueueFinished();
    void updateTotalSpeed();
    void emitDownloadStats();
    void fetchInfoForSections(const QString &url, const QVariantMap &options);
    void fetchFormatsForSelection(const QString &url, const QVariantMap &options);
    bool shouldPreflightSponsorBlock(const DownloadItem &item) const;
    void startSponsorBlockPreflight(const DownloadItem &item);
    QString effectivePlaylistTitle(const DownloadItem &item) const;
    void applyAudioPlaylistAlbumMetadata(DownloadItem &item) const;

    ConfigManager *m_configManager;
    SortingManager *m_sortingManager;
    ArchiveManager *m_archiveManager;
    DownloadFinalizer *m_finalizer;
    QMap<QString, QObject*> m_activeWorkers;
    QMap<QString, DownloadItem> m_activeItems;
    QMap<QString, QObject*> m_activeEmbedders;
    QMap<QString, DownloadItem> m_pendingSponsorBlockPreflights;

    int m_maxConcurrentDownloads;
    enum SleepMode { NoSleep, ShortSleep, LongSleep };
    SleepMode m_sleepMode;
    QTimer *m_sleepTimer;

    int m_queuedDownloadsCount;
    int m_pausedDownloadsCount;
    int m_activeDownloadsCount;
    int m_completedDownloadsCount;
    int m_errorDownloadsCount;
    QMap<QString, double> m_workerSpeeds;
    bool m_isShuttingDown;

    DownloadQueueState *m_queueState;
    DownloadQueueManager *m_queueManager;
};

#endif // DOWNLOADMANAGER_H
