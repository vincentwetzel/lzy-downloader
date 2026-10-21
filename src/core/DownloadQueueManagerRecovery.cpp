#include "DownloadQueueManager.h"

#include <QDebug>
#include <QJsonObject>
#include <QTimer>
#include <utility>

DownloadItem DownloadQueueManager::takeNextQueuedDownload()
{
    if (m_queueRestoreInProgress) return DownloadItem();
    for (qsizetype i = 0; i < m_downloadQueue.size(); ++i) {
        if (!m_pendingExpansions.contains(m_downloadQueue.at(i).id)) {
            DownloadItem item = m_downloadQueue.takeAt(i);
            emitQueueCountsChanged();
            return item;
        }
    }
    return DownloadItem();
}

bool DownloadQueueManager::hasQueuedDownloads() const
{
    if (m_tempCleanupInProgress || m_queueRestoreInProgress) return false;
    for (const DownloadItem &item : std::as_const(m_downloadQueue)) {
        if (!m_pendingExpansions.contains(item.id)) return true;
    }
    return false;
}

void DownloadQueueManager::emitQueueCountsChanged()
{
    emit queueCountsChanged(m_downloadQueue.size(), m_pausedItems.size());
}

void DownloadQueueManager::processResumeDownloadsSelection(const QJsonArray &arr)
{
    m_pendingResumeItems = arr;
    m_nextResumeItemIndex = 0;
    m_queueRestoreInProgress = true;
    processResumeDownloadsBatch();
}

void DownloadQueueManager::processResumeDownloadsBatch()
{
    constexpr int batchSize = 8;
    const int endIndex = qMin(m_nextResumeItemIndex + batchSize, m_pendingResumeItems.size());
    for (; m_nextResumeItemIndex < endIndex; ++m_nextResumeItemIndex) {
        const QJsonObject obj = m_pendingResumeItems.at(m_nextResumeItemIndex).toObject();
        DownloadItem item;
        item.id = obj.value(QStringLiteral("id")).toString();
        item.url = obj.value(QStringLiteral("url")).toString();
        item.options = obj.value(QStringLiteral("options")).toObject().toVariantMap();
        item.metadata = obj.value(QStringLiteral("metadata")).toObject().toVariantMap();
        item.playlistIndex = obj.value(QStringLiteral("playlistIndex")).toInt(-1);
        item.tempFilePath = obj.value(QStringLiteral("tempFilePath")).toString();
        item.originalDownloadedFilePath = obj.value(QStringLiteral("originalDownloadedFilePath")).toString();

        const QString status = obj.value(QStringLiteral("status")).toString(QStringLiteral("queued"));
        QVariantMap uiData{
            {QStringLiteral("id"), item.id},
            {QStringLiteral("url"), item.url},
            {QStringLiteral("status"), status == QStringLiteral("paused") ? tr("Paused") : tr("Queued")},
            {QStringLiteral("progress"), 0},
            {QStringLiteral("options"), item.options},
            {QStringLiteral("playlistIndex"), item.playlistIndex}
        };

        const QString initialTitle = item.options.value(QStringLiteral("initial_title")).toString().trimmed();
        if (!initialTitle.isEmpty()) {
            uiData.insert(QStringLiteral("title"), initialTitle);
        } else if (item.metadata.contains(QStringLiteral("title"))) {
            uiData.insert(QStringLiteral("title"), item.metadata.value(QStringLiteral("title")));
        }
        const QString thumbPath = item.metadata.value(QStringLiteral("thumbnail_path")).toString().trimmed();
        if (!thumbPath.isEmpty()) {
            uiData.insert(QStringLiteral("thumbnail_path"), thumbPath);
        } else if (item.options.contains(QStringLiteral("thumbnail_url"))) {
            uiData.insert(QStringLiteral("thumbnail_path"), item.options.value(QStringLiteral("thumbnail_url")).toString().trimmed());
        }

        if (status == QStringLiteral("paused")) {
            m_pausedItems.insert(item.id, item);
            emit downloadAddedToQueue(uiData);
            emit downloadPaused(item.id);
        } else if (status == QStringLiteral("stopped")) {
            uiData.insert(QStringLiteral("status"), tr("Stopped"));
            item.options.insert(QStringLiteral("is_stopped"), true);
            m_pausedItems.insert(item.id, item);
            emit downloadAddedToQueue(uiData);
            emit downloadCancelled(item.id);
        } else {
            m_downloadQueue.enqueue(item);
            emit downloadAddedToQueue(uiData);
        }
    }

    if (m_nextResumeItemIndex < m_pendingResumeItems.size()) {
        QTimer::singleShot(1, this, &DownloadQueueManager::processResumeDownloadsBatch);
        return;
    }

    m_pendingResumeItems = QJsonArray();
    m_nextResumeItemIndex = 0;
    m_queueRestoreInProgress = false;
    emitQueueCountsChanged();
    emit resumeItemsRestored();
    emit requestStartNextDownload();
}

bool DownloadQueueManager::removeTerminalPausedDuplicate(const DownloadItem &candidate, QString *removedId)
{
    const auto sameMedia = [this, &candidate](const DownloadItem &item) {
        if (!m_archiveManager) {
            return item.url == candidate.url;
        }
        return m_archiveManager->sameMediaIdentity(item.url, candidate.url);
    };

    for (auto it = m_pausedItems.begin(); it != m_pausedItems.end(); ++it) {
        const DownloadItem &item = it.value();
        const bool terminal = item.options.value(QStringLiteral("is_stopped")).toBool()
            || item.options.value(QStringLiteral("is_failed")).toBool();
        if (!terminal || !sameMedia(item)) {
            continue;
        }

        const QString id = it.key();
        m_pausedItems.erase(it);
        if (removedId) {
            *removedId = id;
        }
        qInfo() << "DownloadQueueManager: Removed terminal stopped/failed duplicate for explicit re-download:" << id;
        emitQueueCountsChanged();
        return true;
    }

    return false;
}
