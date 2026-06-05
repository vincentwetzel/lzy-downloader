#include "DownloadManager.h"
#include "DownloadQueueManager.h"
#include "PlaylistExpansionWorker.h"
#include <QUuid>
#include <QDebug>
#include <QMetaObject>

namespace {
bool isNonInteractiveRequest(const QVariantMap &options)
{
    return options.value(QStringLiteral("non_interactive"), false).toBool();
}
}

void DownloadManager::onPlaylistDetected(const QString &url, int itemCount, const QVariantMap &options, const QList<QVariantMap> &expandedItems) {
    PlaylistExpansionWorker *expander = qobject_cast<PlaylistExpansionWorker*>(sender());
    QVariantMap storedOptions = options;
    if (expander) {
        storedOptions = expander->property("options").toMap();
        const QString queueId = expander->property("queueId").toString();
        if (!queueId.isEmpty()) {
            storedOptions.insert(QStringLiteral("playlist_placeholder_id"), queueId);
        }
        expander->deleteLater();
    }

    if (itemCount == 1 || isNonInteractiveRequest(storedOptions)) {
        if (itemCount == 1) {
            qInfo() << "Playlist contains only 1 item; queueing without prompting:" << url;
        } else {
            qInfo() << "Non-interactive playlist detected; queueing all items without prompting:" << url << "count:" << itemCount;
        }
        QMetaObject::invokeMethod(this, [this, url, storedOptions, expandedItems]() {
            processPlaylistSelection(url, QStringLiteral("Download All"), storedOptions, expandedItems);
        }, Qt::QueuedConnection);
        return;
    }

    // Delegate UI presentation to the View layer
    QMetaObject::invokeMethod(this, [this, url, itemCount, storedOptions, expandedItems]() {
        emit playlistActionRequested(url, itemCount, storedOptions, expandedItems);
    }, Qt::QueuedConnection);
}

void DownloadManager::processPlaylistSelection(const QString &url, const QString &action, const QVariantMap &options, const QList<QVariantMap> &expandedItems) {
    const QString queueId = options.value(QStringLiteral("playlist_placeholder_id")).toString();
    QVariantMap queueOptions = options;
    queueOptions.remove(QStringLiteral("playlist_placeholder_id"));
    queueOptions.insert(QStringLiteral("playlist_logic"), QStringLiteral("Download Single (ignore playlist)"));
    QList<QVariantMap> finalItems;

    if (action == QStringLiteral("Download All") || action == QStringLiteral("Download Part")) {
        finalItems = expandedItems;
        bool containsPlaylistItems = false;
        for (const QVariantMap &itemData : finalItems) {
            if (itemData.value(QStringLiteral("is_playlist"), false).toBool()) {
                containsPlaylistItems = true;
                break;
            }
        }
        queueOptions.insert(QStringLiteral("is_playlist"), containsPlaylistItems);
    } else if (action == QStringLiteral("Download Single Item") && !expandedItems.isEmpty()) {
        finalItems.append(expandedItems.first());
        queueOptions.insert(QStringLiteral("is_playlist"), expandedItems.first().value(QStringLiteral("is_playlist"), false).toBool());
    } else {
        if (!queueId.isEmpty()) {
            m_queueManager->removePendingExpansionPlaceholder(queueId);
        }
        emit playlistExpansionFinished(url, 0);
        return;
    }

    emit playlistExpansionFinished(url, finalItems.count());

    if (finalItems.size() == 1 && !queueId.isEmpty()) {
        const QVariantMap itemData = finalItems.first();
        bool found = false;
        for (DownloadItem &item : m_queueManager->m_downloadQueue) {
            if (item.id == queueId) {
                item.url = itemData.value(QStringLiteral("url")).toString();
                item.playlistIndex = itemData.value(QStringLiteral("playlist_index"), -1).toInt();
                item.options = queueOptions;
                item.options.insert(QStringLiteral("original_playlist_url"), url);
                item.options.insert(QStringLiteral("is_playlist"), itemData.value(QStringLiteral("is_playlist"), queueOptions.value(QStringLiteral("is_playlist"), false)).toBool());
                if (itemData.contains(QStringLiteral("is_live"))) {
                    item.options.insert(QStringLiteral("is_live"), itemData.value(QStringLiteral("is_live")).toBool());
                }
                found = true;
                break;
            }
        }

        if (found) {
            m_queueManager->m_pendingExpansions.remove(queueId);
            QVariantMap progressData;
            progressData.insert(QStringLiteral("progress"), 0);
            progressData.insert(QStringLiteral("url"), itemData.value(QStringLiteral("url")).toString());
            progressData.insert(QStringLiteral("playlistIndex"), itemData.value(QStringLiteral("playlist_index"), -1).toInt());
            progressData.insert(QStringLiteral("options"), queueOptions);
            const QString title = itemData.value(QStringLiteral("title")).toString().trimmed();
            if (!title.isEmpty()) {
                progressData.insert(QStringLiteral("title"), title);
            }
            if (itemData.contains(QStringLiteral("thumbnail_url"))) {
                progressData.insert(QStringLiteral("thumbnail_path"), itemData.value(QStringLiteral("thumbnail_url")));
            }
            emit downloadProgress(queueId, progressData);
            
            onQueueCountsChanged(m_queueManager->m_downloadQueue.size(), m_queueManager->m_pausedItems.size());

            startNextDownload();
            return;
        }
    }

    if (!queueId.isEmpty()) {
        m_queueManager->removePendingExpansionPlaceholder(queueId);
    }

    for (const QVariantMap &itemData : finalItems) {
        DownloadItem item;
        item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        item.url = itemData.value(QStringLiteral("url")).toString();
        QVariantMap itemOptions = queueOptions;
        itemOptions.insert(QStringLiteral("is_playlist"), itemData.value(QStringLiteral("is_playlist"), queueOptions.value(QStringLiteral("is_playlist"), false)).toBool());
        if (itemData.contains(QStringLiteral("is_live"))) {
            itemOptions.insert(QStringLiteral("is_live"), itemData.value(QStringLiteral("is_live")).toBool());
        }
        const QString title = itemData.value(QStringLiteral("title")).toString().trimmed();
        if (!title.isEmpty()) {
            itemOptions.insert(QStringLiteral("initial_title"), title);
        }
        itemOptions.insert(QStringLiteral("original_playlist_url"), url);
        if (itemData.contains(QStringLiteral("playlist_title"))) {
            itemOptions.insert(QStringLiteral("playlist_title"), itemData.value(QStringLiteral("playlist_title")));
        }
        if (itemData.contains(QStringLiteral("thumbnail_url"))) {
            itemOptions.insert(QStringLiteral("thumbnail_url"), itemData.value(QStringLiteral("thumbnail_url")));
        }
        item.options = itemOptions;
        item.playlistIndex = itemData.value(QStringLiteral("playlist_index"), -1).toInt();
        m_queueManager->enqueueDownload(item);
    }
}

void DownloadManager::onPlaylistExpanded(const QString &originalUrl, const QList<QVariantMap> &expandedItems, const QString &error) {
    PlaylistExpansionWorker *expander = qobject_cast<PlaylistExpansionWorker*>(sender());
    QVariantMap options;
    QString queueId;
    if (expander) {
        options = expander->property("options").toMap();
        queueId = expander->property("queueId").toString();
        expander->deleteLater();
    }

    QList<QVariantMap> itemsToProcess = expandedItems;

    if (!error.isEmpty()) {
        bool isKnownVideoError = false;

        if (error.contains(QStringLiteral("Premieres in"), Qt::CaseInsensitive) ||
                error.contains(QStringLiteral("Premiering in"), Qt::CaseInsensitive) ||
                error.contains(QStringLiteral("Premiere will begin"), Qt::CaseInsensitive) ||
                error.contains(QStringLiteral("live event will begin"), Qt::CaseInsensitive) ||
                error.contains(QStringLiteral("is upcoming"), Qt::CaseInsensitive) ||
                error.contains(QStringLiteral("Offline (expected)"), Qt::CaseInsensitive) ||
                error.contains(QStringLiteral("Offline expected"), Qt::CaseInsensitive) ||
                error.contains(QStringLiteral("waiting for premiere"), Qt::CaseInsensitive) ||
                error.contains(QStringLiteral("waiting for livestream"), Qt::CaseInsensitive) ||
                error.contains(QStringLiteral("Live in "), Qt::CaseInsensitive) ||
                error.contains(QStringLiteral("Starting in "), Qt::CaseInsensitive)) {
            isKnownVideoError = true;
        } else if (error.contains(QStringLiteral("private"), Qt::CaseInsensitive)) {
            isKnownVideoError = true;
        } else if (error.contains(QStringLiteral("unavailable"), Qt::CaseInsensitive) ||
                     error.contains(QStringLiteral("no longer available"), Qt::CaseInsensitive) ||
                     error.contains(QStringLiteral("does not exist"), Qt::CaseInsensitive) ||
                     error.contains(QStringLiteral("removed"), Qt::CaseInsensitive)) {
            isKnownVideoError = true;
        } else if (error.contains(QStringLiteral("members"), Qt::CaseInsensitive) && error.contains(QStringLiteral("only"), Qt::CaseInsensitive)) {
            isKnownVideoError = true;
        } else if (error.contains(QStringLiteral("geo"), Qt::CaseInsensitive) || error.contains(QStringLiteral("country"), Qt::CaseInsensitive)) {
            isKnownVideoError = true;
        } else if (error.contains(QStringLiteral("age"), Qt::CaseInsensitive)) {
            isKnownVideoError = true;
        }

        if (isKnownVideoError) {
            qDebug() << "Playlist expansion hit a known video-level error. Bypassing to let YtDlpWorker handle it. Error:" << error;
            QVariantMap singleItem;
            singleItem.insert(QStringLiteral("url"), originalUrl);
            singleItem.insert(QStringLiteral("playlist_index"), -1);
            itemsToProcess.append(singleItem);
        } else {
            qDebug() << "Playlist expansion failed:" << error;
            // Update the UI item to show error
            if (!queueId.isEmpty()) {
                emit downloadProgress(queueId, {{QStringLiteral("status"), tr("Failed to check playlist")}});
                emit downloadFinished(queueId, false, tr("Playlist expansion failed: %1").arg(error));
                m_queueManager->m_pendingExpansions.remove(queueId);
                m_queueManager->cancelQueuedOrPausedDownload(queueId); // Remove placeholder from queue
            }
            return;
        }
    }

    emit playlistExpansionFinished(originalUrl, itemsToProcess.count());

    // If this was a single video (no expansion needed), update the existing UI item
    if (itemsToProcess.size() == 1 && !queueId.isEmpty()) {
        QVariantMap itemData = itemsToProcess.first();

        // Find the placeholder item in the queue and update it in-place.
        // This avoids re-enqueueing and causing a duplicate download.
        bool found = false;
        for (DownloadItem &item : m_queueManager->m_downloadQueue) { // Assumes m_downloadQueue is accessible
            if (item.id == queueId) {
                item.url = itemData.value(QStringLiteral("url")).toString();
                item.playlistIndex = itemData.value(QStringLiteral("playlist_index"), -1).toInt();
                item.options = options;
            item.options.insert(QStringLiteral("original_playlist_url"), originalUrl);
            item.options.insert(QStringLiteral("playlist_logic"), QStringLiteral("Download Single (ignore playlist)"));
            item.options.insert(QStringLiteral("is_playlist"), itemData.value(QStringLiteral("is_playlist"), false).toBool());
                if (itemData.contains(QStringLiteral("playlist_title"))) {
                item.options.insert(QStringLiteral("playlist_title"), itemData.value(QStringLiteral("playlist_title")));
                }
                const QString title = itemData.value(QStringLiteral("title")).toString().trimmed();
                if (!title.isEmpty()) {
                item.options.insert(QStringLiteral("initial_title"), title);
                }
                if (itemData.contains(QStringLiteral("is_live"))) {
                item.options.insert(QStringLiteral("is_live"), itemData.value(QStringLiteral("is_live")).toBool());
                }
                found = true;
                break;
            }
        }

        if (found) {
            m_queueManager->m_pendingExpansions.remove(queueId); // Assumes m_pendingExpansions is accessible
            QVariantMap progressData;
            progressData.insert(QStringLiteral("progress"), 0);
            const QString title = itemData.value(QStringLiteral("title")).toString().trimmed();
            if (!title.isEmpty()) {
                progressData.insert(QStringLiteral("title"), title);
            }
            if (itemData.contains(QStringLiteral("thumbnail_url"))) {
                progressData.insert(QStringLiteral("thumbnail_path"), itemData.value(QStringLiteral("thumbnail_url")));
            }
            emit downloadProgress(queueId, progressData);
            
            onQueueCountsChanged(m_queueManager->m_downloadQueue.size(), m_queueManager->m_pausedItems.size());

            startNextDownload();
        }
    } else if (itemsToProcess.size() > 1) {
        // This is an actual playlist - remove the placeholder from queue
        if (!queueId.isEmpty()) {
            m_queueManager->removePendingExpansionPlaceholder(queueId);
        }
        
        for (const QVariantMap &itemData : itemsToProcess) {
            DownloadItem item;
            item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            item.url = itemData.value(QStringLiteral("url")).toString();
            item.options = options; // Use the options from the original enqueue
            item.options.insert(QStringLiteral("original_playlist_url"), originalUrl);
            item.options.insert(QStringLiteral("playlist_logic"), QStringLiteral("Download Single (ignore playlist)"));
            item.options.insert(QStringLiteral("is_playlist"), true);
            
            if (itemData.contains(QStringLiteral("playlist_title"))) {
                item.options.insert(QStringLiteral("playlist_title"), itemData.value(QStringLiteral("playlist_title")));
            }
            
            const QString title = itemData.value(QStringLiteral("title")).toString().trimmed();
            if (!title.isEmpty()) {
                item.options.insert(QStringLiteral("initial_title"), title);
            }
            
            if (itemData.contains(QStringLiteral("is_live"))) {
                item.options.insert(QStringLiteral("is_live"), itemData.value(QStringLiteral("is_live")).toBool());
            }
            
            item.playlistIndex = itemData.value(QStringLiteral("playlist_index"), -1).toInt();
            m_queueManager->enqueueDownload(item);
        }
    }
    // No need to call emitDownloadStats() or startNextDownload() here,
    // as enqueueDownload() already triggers these via signals.
}

void DownloadManager::onPlaylistExpansionPlaceholderRemoved(const QString &id) {
    emit downloadRemovedFromQueue(id);
}

void DownloadManager::onPlaylistExpansionPlaceholderUpdated(const QString &id, const QVariantMap &itemData) {
    // Handle UI update if necessary, e.g., update the placeholder item's status to "Queued"
    emit downloadProgress(id, itemData);
}
