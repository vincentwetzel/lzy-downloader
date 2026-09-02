#pragma once

#include <QObject>
#include <QQueue>
#include <QMap>
#include <QUuid>
#include <QFutureWatcher>
#include "DownloadItem.h"
#include "ConfigManager.h"
#include "ArchiveManager.h"
#include "DownloadQueueState.h"

class DownloadQueueManager : public QObject {
    Q_OBJECT
public:
    explicit DownloadQueueManager(ConfigManager *configManager, ArchiveManager *archiveManager, DownloadQueueState *queueState, QObject *parent = nullptr);
    ~DownloadQueueManager();

    enum DuplicateStatus {
        NotDuplicate,
        DuplicateInQueue,
        DuplicateActive, // Managed by DownloadManager, but checked here
        DuplicatePaused,
        DuplicateCompleted
    };
    Q_ENUM(DuplicateStatus)

    DuplicateStatus getDuplicateStatus(const QString &url, const QMap<QString, DownloadItem> &activeItems) const;
    DuplicateStatus getDuplicateStatus(const DownloadItem &candidate, const QMap<QString, DownloadItem> &activeItems) const;
    bool isUrlInQueue(const QString &url, const QMap<QString, DownloadItem> &activeItems) const;
    /** Removes one matching restored stopped/failed item for an explicit re-download. */
    bool removeTerminalPausedDuplicate(const DownloadItem &candidate, QString *removedId = nullptr);

    void enqueueDownload(const DownloadItem &item, bool isNew = true);
    bool removePendingExpansionPlaceholder(const QString &id);
    bool cancelQueuedOrPausedDownload(const QString &id);
    bool pauseQueuedDownload(const QString &id, DownloadItem &pausedItem);
    bool unpauseDownload(const QString &id);
    void moveDownloadUp(const QString &id);
    void moveDownloadDown(const QString &id);
    void retryDownload(const QVariantMap &itemData, const QMap<QString, DownloadItem> &activeItems = {});
    void resumeDownload(const QVariantMap &itemData, const QMap<QString, DownloadItem> &activeItems = {});
    void processResumeDownloadsSelection(const QJsonArray &arr);

    void saveQueueState(const QMap<QString, DownloadItem> &activeItems);
    void saveQueueStateAsync(const QMap<QString, DownloadItem> &activeItems);
    void cleanupOrphanedTempDirectories();
    DownloadItem takeNextQueuedDownload();
    bool hasQueuedDownloads() const;
    int queuedDownloadsCount() const { return m_downloadQueue.size(); }
    int pausedDownloadsCount() const { return m_pausedItems.size(); }

signals:
    void downloadAddedToQueue(const QVariantMap &uiData);
    void downloadCancelled(const QString &id);
    void downloadPaused(const QString &id);
    void downloadResumed(const QString &id);
    void duplicateDownloadDetected(const QString &url, const QString &reason);
    void requestStartNextDownload();
    void queueCountsChanged(int queued, int paused);
    void playlistExpansionPlaceholderRemoved(const QString &id);
    void playlistExpansionPlaceholderUpdated(const QString &id, const QVariantMap &itemData);

public: // Public for DownloadManager to access directly during playlist expansion
    friend class DownloadManager;

    QMap<QString, QString> m_pendingExpansions; // Maps queueId to original URL

private:
    ConfigManager *m_configManager;
    ArchiveManager *m_archiveManager;
    DownloadQueueState *m_queueState;

    QQueue<DownloadItem> m_downloadQueue;
    QMap<QString, DownloadItem> m_pausedItems;
    bool m_tempCleanupInProgress = false;
    QFutureWatcher<void> *m_queueSaveWatcher = nullptr;
    bool m_hasPendingQueueSave = false;
    QString m_pendingQueueSavePath;
    QList<DownloadItem> m_pendingActiveItems;
    QMap<QString, DownloadItem> m_pendingPausedItems;
    QQueue<DownloadItem> m_pendingDownloadQueue;

    void emitQueueCountsChanged();
    void startQueueStateSave(const QString &backupPath, const QList<DownloadItem> &activeItems,
                             const QMap<QString, DownloadItem> &pausedItems,
                             const QQueue<DownloadItem> &downloadQueue);
};
