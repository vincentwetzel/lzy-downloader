#include "DownloadQueueManager.h"

#include "DownloadTempCleanup.h"

#include <QDebug>
#include <QThread>

#include <utility>

void DownloadQueueManager::cleanupOrphanedTempDirectories()
{
    if (m_tempCleanupInProgress) {
        return;
    }
    m_tempCleanupInProgress = true;

    QSet<QString> preservedIds;
    for (const DownloadItem &item : std::as_const(m_downloadQueue)) {
        preservedIds.insert(item.id);
    }
    for (auto it = m_pausedItems.cbegin(); it != m_pausedItems.cend(); ++it) {
        preservedIds.insert(it.key());
    }
    for (auto it = m_pendingExpansions.cbegin(); it != m_pendingExpansions.cend(); ++it) {
        preservedIds.insert(it.key());
    }

    const QString tempRoot = DownloadTempCleanup::resolveRoot(m_configManager);
    QThread *cleanupThread = QThread::create([tempRoot, preservedIds]() {
        const int removedCount = DownloadTempCleanup::removeOrphanedUuidDirectories(tempRoot, preservedIds);
        if (removedCount > 0) {
            qInfo() << "Temporary-directory startup sweep removed" << removedCount << "orphaned download folder(s).";
        }
    });
    connect(cleanupThread, &QThread::finished, cleanupThread, &QObject::deleteLater);
    connect(cleanupThread, &QThread::finished, this, [this]() {
        m_tempCleanupInProgress = false;
        emit requestStartNextDownload();
    });
    cleanupThread->setObjectName(QStringLiteral("DownloadTempCleanupThread"));
    cleanupThread->start();
}
