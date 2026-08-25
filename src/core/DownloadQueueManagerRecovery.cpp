#include "DownloadQueueManager.h"

#include <QDebug>

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
