#include "DownloadManager.h"
#include "DownloadQueueManager.h"
#include "PlaylistExpander.h"
#include <QUuid>
#include <QDebug>
#include <QMetaObject>

namespace {
bool isNonInteractiveRequest(const QVariantMap &options)
{
    return options.value("non_interactive", false).toBool();
}
}

void DownloadManager::onPlaylistDetected(const QString &url, int itemCount, const QVariantMap &options, const QList<QVariantMap> &expandedItems) {
    PlaylistExpander *expander = qobject_cast<PlaylistExpander*>(sender());
    QVariantMap storedOptions = options;
    if (expander) {
        storedOptions = expander->property("options").toMap();
        const QString queueId = expander->property("queueId").toString();
        if (!queueId.isEmpty()) {
            storedOptions["playlist_placeholder_id"] = queueId;
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
            processPlaylistSelection(url, "Download All", storedOptions, expandedItems);
        }, Qt::QueuedConnection);
        return;
    }

    // Delegate UI presentation to the View layer
    QMetaObject::invokeMethod(this, [this, url, itemCount, storedOptions, expandedItems]() {
        emit playlistActionRequested(url, itemCount, storedOptions, expandedItems);
    }, Qt::QueuedConnection);
}

void DownloadManager::processPlaylistSelection(const QString &url, const QString &action, const QVariantMap &options, const QList<QVariantMap> &expandedItems) {
    const QString queueId = options.value("playlist_placeholder_id").toString();
    QVariantMap queueOptions = options;
    queueOptions.remove("playlist_placeholder_id");
    queueOptions["playlist_logic"] = "Download Single (ignore playlist)";
    QList<QVariantMap> finalItems;

    if (action == "Download All") {
        finalItems = expandedItems;
        bool containsPlaylistItems = false;
        for (const QVariantMap &itemData : finalItems) {
            if (itemData.value("is_playlist", false).toBool()) {
                containsPlaylistItems = true;
                break;
            }
        }
        queueOptions["is_playlist"] = containsPlaylistItems;
    } else if (action == "Download Single Item" && !expandedItems.isEmpty()) {
        finalItems.append(expandedItems.first());
        queueOptions["is_playlist"] = expandedItems.first().value("is_playlist", false).toBool();
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
                item.url = itemData.value("url").toString();
                item.playlistIndex = itemData.value("playlist_index", -1).toInt();
                item.options = queueOptions;
                item.options["original_playlist_url"] = url;
                item.options["is_playlist"] = itemData.value("is_playlist", queueOptions.value("is_playlist", false)).toBool();
                if (itemData.contains("is_live")) {
                    item.options["is_live"] = itemData.value("is_live").toBool();
                }
                found = true;
                break;
            }
        }

        if (found) {
            m_queueManager->m_pendingExpansions.remove(queueId);
            QVariantMap progressData;
            progressData["status"] = "Queued";
            progressData["progress"] = 0;
            progressData["url"] = itemData.value("url").toString();
            progressData["playlistIndex"] = itemData.value("playlist_index", -1).toInt();
            progressData["options"] = queueOptions;
            const QString title = itemData.value("title").toString().trimmed();
            if (!title.isEmpty()) {
                progressData["title"] = title;
            }
            emit downloadProgress(queueId, progressData);
            QMetaObject::invokeMethod(m_queueManager, [this]() { m_queueManager->saveQueueState(m_activeItems); }, Qt::QueuedConnection);
            
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
        item.url = itemData["url"].toString();
        QVariantMap itemOptions = queueOptions;
        itemOptions["is_playlist"] = itemData.value("is_playlist", queueOptions.value("is_playlist", false)).toBool();
        if (itemData.contains("is_live")) { // Use 'options' from parameter
            itemOptions["is_live"] = itemData.value("is_live").toBool();
        }
        const QString title = itemData.value("title").toString().trimmed();
        if (!title.isEmpty()) {
            itemOptions["initial_title"] = title;
        }
        itemOptions["original_playlist_url"] = url;
        item.options = itemOptions;
        item.playlistIndex = itemData.value("playlist_index", -1).toInt();
        m_queueManager->enqueueDownload(item);
    }
}

void DownloadManager::onPlaylistExpanded(const QString &originalUrl, const QList<QVariantMap> &expandedItems, const QString &error) {
    PlaylistExpander *expander = qobject_cast<PlaylistExpander*>(sender());
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

            if (error.contains("Premieres in", Qt::CaseInsensitive) ||
                error.contains("Premiering in", Qt::CaseInsensitive) ||
                error.contains("Premiere will begin", Qt::CaseInsensitive) ||
                error.contains("live event will begin", Qt::CaseInsensitive) ||
                error.contains("is upcoming", Qt::CaseInsensitive) ||
                error.contains("Offline (expected)", Qt::CaseInsensitive) ||
                error.contains("Offline expected", Qt::CaseInsensitive) ||
                error.contains("waiting for premiere", Qt::CaseInsensitive) ||
                error.contains("waiting for livestream", Qt::CaseInsensitive) ||
                error.contains("Live in ", Qt::CaseInsensitive) ||
                error.contains("Starting in ", Qt::CaseInsensitive)) {
                isKnownVideoError = true;
            } else if (error.contains("private", Qt::CaseInsensitive)) {
                isKnownVideoError = true;
            } else if (error.contains("unavailable", Qt::CaseInsensitive) ||
                     error.contains("no longer available", Qt::CaseInsensitive) ||
                     error.contains("does not exist", Qt::CaseInsensitive) ||
                     error.contains("removed", Qt::CaseInsensitive)) {
                isKnownVideoError = true;
            } else if (error.contains("members", Qt::CaseInsensitive) && error.contains("only", Qt::CaseInsensitive)) {
                isKnownVideoError = true;
            } else if (error.contains("geo", Qt::CaseInsensitive) || error.contains("country", Qt::CaseInsensitive)) {
                isKnownVideoError = true;
            } else if (error.contains("age", Qt::CaseInsensitive)) {
                isKnownVideoError = true;
            }

            if (isKnownVideoError) {
                qDebug() << "Playlist expansion hit a known video-level error. Bypassing to let YtDlpWorker handle it. Error:" << error;
                QVariantMap singleItem;
                singleItem["url"] = originalUrl;
                singleItem["playlist_index"] = -1;
                itemsToProcess.append(singleItem);
            } else {
                qDebug() << "Playlist expansion failed:" << error;
                // Update the UI item to show error
                if (!queueId.isEmpty()) {
                    emit downloadProgress(queueId, {{"status", "Failed to check playlist"}});
                    emit downloadFinished(queueId, false, "Playlist expansion failed: " + error);
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
                item.url = itemData.value("url").toString();
                item.playlistIndex = itemData.value("playlist_index", -1).toInt();
                item.options = options;
                item.options["original_playlist_url"] = originalUrl;
                item.options["playlist_logic"] = "Download Single (ignore playlist)";
                    item.options["is_playlist"] = itemData.value("is_playlist", false).toBool();
                    if (itemData.contains("playlist_title")) {
                        item.options["playlist_title"] = itemData.value("playlist_title");
                    }
                const QString title = itemData.value("title").toString().trimmed();
                if (!title.isEmpty()) {
                    item.options["initial_title"] = title;
                }
                if (itemData.contains("is_live")) {
                    item.options["is_live"] = itemData.value("is_live").toBool();
                }
                found = true;
                break;
            }
        }

        if (found) {
            m_queueManager->m_pendingExpansions.remove(queueId); // Assumes m_pendingExpansions is accessible
            QVariantMap progressData;
            progressData["status"] = "Queued";
            progressData["progress"] = 0;
            const QString title = itemData.value("title").toString().trimmed();
            if (!title.isEmpty()) {
                progressData["title"] = title;
            }
            emit downloadProgress(queueId, progressData);
            // Manually save the queue state since we modified an item in-place
            QMetaObject::invokeMethod(m_queueManager, [this]() { m_queueManager->saveQueueState(m_activeItems); }, Qt::QueuedConnection);
            
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
            item.url = itemData["url"].toString();
            item.options = options; // Use the options from the original enqueue
            item.options["original_playlist_url"] = originalUrl;
            item.options["playlist_logic"] = "Download Single (ignore playlist)";
                item.options["is_playlist"] = true;
                if (itemData.contains("playlist_title")) {
                    item.options["playlist_title"] = itemData.value("playlist_title");
                }
            const QString title = itemData.value("title").toString().trimmed();
            if (!title.isEmpty()) {
                item.options["initial_title"] = title;
            }
            if (itemData.contains("is_live")) item.options["is_live"] = itemData.value("is_live").toBool();
            if (itemData.contains("playlist_title")) {
                item.options["playlist_title"] = itemData.value("playlist_title");
            }
            item.playlistIndex = itemData.value("playlist_index", -1).toInt();
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


