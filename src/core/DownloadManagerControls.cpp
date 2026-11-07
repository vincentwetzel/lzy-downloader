#include "DownloadManager.h"
#include "DownloadQueueManager.h"
#include "GalleryDlWorker.h"
#include "PlaylistExpansionWorker.h"
#include "YtDlpArgsBuilder.h"
#include "YtDlpWorker.h"
#include "core/ProcessUtils.h"
#include <QDebug>
#include <QMetaObject>
#include <QProcess>
#include <QDateTime>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

namespace {
bool looksLikePlaylistUrl(const QString &urlString)
{
    const QUrl url(urlString);
    if (!url.isValid()) {
        return false;
    }

    const QStringList pathParts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : pathParts) {
        if (part.compare(QStringLiteral("playlist"), Qt::CaseInsensitive) == 0
                || part.compare(QStringLiteral("collection"), Qt::CaseInsensitive) == 0
                || part.compare(QStringLiteral("album"), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }

    const QUrlQuery query(url);
    return query.hasQueryItem(QStringLiteral("list"))
        || query.hasQueryItem(QStringLiteral("playlist"))
        || query.hasQueryItem(QStringLiteral("collection"))
        || query.hasQueryItem(QStringLiteral("album"));
}
}

void DownloadManager::cancelDownload(const QString &id) {
    bool cancelled = false;
    
    // Delegate to queue manager first for queued/paused items
    if (m_queueManager->cancelQueuedOrPausedDownload(id)) {
        cancelled = true;
    }

    if (m_queueManager->m_pendingExpansions.contains(id)) {
        m_queueManager->m_pendingExpansions.remove(id);
        
        // Find and terminate the background playlist expansion process
        const QList<PlaylistExpansionWorker*> expanders = findChildren<PlaylistExpansionWorker*>();
        for (PlaylistExpansionWorker *expander : expanders) {
            if (expander->property("queueId").toString() == id) {
                expander->disconnect(this);
                const QList<QProcess*> processes = expander->findChildren<QProcess*>();
                for (QProcess *p : processes) {
                    if (p->state() != QProcess::NotRunning) {
                        p->disconnect(); // Prevent reading buffers from dying process
                        ProcessUtils::terminateProcessTree(p);
                        p->kill();
                    }
                }
                expander->deleteLater();
                break;
            }
        }
        
        if (!cancelled) {
            emit downloadCancelled(id); 
            cancelled = true;
        }
    }

    // Always check active workers to ensure no ghost processes remain
    if (m_activeWorkers.contains(id)) {
        QObject *worker = m_activeWorkers.take(id);
        DownloadItem item = m_activeItems.take(id);

        m_workerSpeeds.remove(id);
        updateTotalSpeed();

        m_lastDownloadFinishTime = QDateTime::currentMSecsSinceEpoch();
        worker->disconnect(this);

        if (auto *ytDlpWorker = qobject_cast<YtDlpWorker*>(worker)) {
            stopWorker(ytDlpWorker);
        } else if (auto *galleryDlWorker = qobject_cast<GalleryDlWorker*>(worker)) {
            stopWorker(galleryDlWorker);
        } else {
            qWarning() << "Cannot stop unknown download worker:" << id;
        }

        adjustActiveDownloadCount(-1);
        
        item.options[QStringLiteral("is_stopped")] = true;
        m_queueManager->m_pausedItems[id] = item;
        
        if (!cancelled) {
            emit downloadCancelled(id);
            cancelled = true;
        }
    } 
    
    if (m_activeEmbedders.contains(id)) {
        // Cancel a download that is currently in the post-processing metadata phase
        QObject *embedder = m_activeEmbedders.take(id);
        DownloadItem item = m_activeItems.take(id);
        
        embedder->disconnect(this);
        
        stopEmbedder(embedder);
        
        item.options[QStringLiteral("is_stopped")] = true;
        m_queueManager->m_pausedItems[id] = item;
        
        adjustActiveDownloadCount(-1);
        if (!cancelled) {
            emit downloadCancelled(id);
            cancelled = true;
        }
    }

    if (cancelled) {
        emitDownloadStats();
        QMetaObject::invokeMethod(this, [this]() {
            m_queueManager->saveQueueStateAsync(m_activeItems);
            startNextDownload();
        }, Qt::QueuedConnection);
    }
}

void DownloadManager::retryDownload(const QVariantMap &itemData) {
    const QString id = itemData.value(QStringLiteral("id")).toString();
    DownloadItem item;
    item.id = id;
    item.url = itemData.value(QStringLiteral("url")).toString();
    item.options = itemData.value(QStringLiteral("options")).toMap();
    item.playlistIndex = itemData.value(QStringLiteral("playlistIndex"), -1).toInt();
    if (item.playlistIndex < 0) {
        item.playlistIndex = item.options.value(QStringLiteral("playlist_index"), -1).toInt();
    }

    const bool failedExpansionPlaceholder = item.options.value(QStringLiteral("playlist_expansion_pending")).toBool()
        || (item.playlistIndex < 0
            && !item.options.value(QStringLiteral("is_playlist"), false).toBool()
            && looksLikePlaylistUrl(item.url));
    if (failedExpansionPlaceholder) {
        const bool hadPausedItem = m_queueManager->m_pausedItems.contains(id);
        const DownloadItem pausedItem = m_queueManager->m_pausedItems.take(id);
        const auto duplicateStatus = m_queueManager->getDuplicateStatus(item, m_activeItems);
        if (duplicateStatus != DownloadQueueManager::NotDuplicate
                && duplicateStatus != DownloadQueueManager::DuplicateCompleted) {
            if (hadPausedItem) {
                m_queueManager->m_pausedItems.insert(id, pausedItem);
            }
            emit duplicateDownloadDetected(item.url, tr("An equivalent download is already queued or active."));
            return;
        }

        item.options.remove(QStringLiteral("is_stopped"));
        item.options.remove(QStringLiteral("is_failed"));
        item.options.insert(QStringLiteral("playlist_expansion_pending"), true);
        m_queueManager->enqueueDownload(item, false);
        emit downloadProgress(id, {{QStringLiteral("status"), tr("Checking for playlist...")},
                                   {QStringLiteral("progress"), 0}});
        launchPlaylistExpansion(item);
        m_queueManager->saveQueueStateAsync(m_activeItems);
        return;
    }

    m_queueManager->retryDownload(itemData, m_activeItems);
}

void DownloadManager::restartDownloadWithOptions(const QVariantMap &itemData) {
    QString id = itemData.value(QStringLiteral("id")).toString();

    if (!m_activeItems.contains(id)) {
        // Fallback for non-active items, just treat as a normal retry
        qWarning() << "restartDownloadWithOptions called for non-active ID:" << id << ". Falling back to retry.";
        retryDownload(itemData);
        return;
    }

    qDebug() << "Restarting active download with new options:" << id;

    if (m_activeWorkers.contains(id)) {
        // 1. Get the active worker and kill it.
        QObject *worker = m_activeWorkers.take(id);
        // Disconnect signals to prevent onWorkerFinished from being called with an error
        worker->disconnect(this);
        
        if (auto *ytDlpWorker = qobject_cast<YtDlpWorker*>(worker)) {
            stopWorker(ytDlpWorker);
        } else if (auto *galleryDlWorker = qobject_cast<GalleryDlWorker*>(worker)) {
            stopWorker(galleryDlWorker);
        }
    }

    // 2. The item is still in m_activeItems. We will reuse it.
    DownloadItem &item = m_activeItems[id];
    item.options = itemData.value(QStringLiteral("options")).toMap(); // Update options

    // 3. Tell the UI to reset its state for the existing item.
    QVariantMap resetData;
    resetData[QStringLiteral("id")] = id;
    resetData[QStringLiteral("status")] = tr("Waiting for video...");
    resetData[QStringLiteral("progress")] = -1; // Indeterminate progress
    emit downloadProgress(id, resetData);

    // 4. Restart using the shared item startup flow so gallery-dl 
    // and active download counts are handled correctly.
    startDownloadItem(item, true);
}

void DownloadManager::resumeDownload(const QVariantMap &itemData) {
    retryDownload(itemData);
}

void DownloadManager::pauseDownload(const QString &id) {
    if (m_queueManager && m_queueManager->m_pendingExpansions.contains(id)) {
        qWarning() << "Cannot pause a download that is currently expanding playlist:" << id;
        emit downloadResumed(id); // Revert UI
        return;
    }

    bool paused = false;
    
    DownloadItem pausedItem;
    if (m_queueManager && m_queueManager->pauseQueuedDownload(id, pausedItem)) {
        paused = true;
    }
    
    if (!paused && m_activeWorkers.contains(id)) {
        QObject *worker = m_activeWorkers.take(id);
        m_queueManager->m_pausedItems[id] = m_activeItems.take(id); // Add to queue manager's paused items
        
        m_workerSpeeds.remove(id);
        updateTotalSpeed();

        m_lastDownloadFinishTime = QDateTime::currentMSecsSinceEpoch();
        worker->disconnect(this);

        if (auto *ytDlpWorker = qobject_cast<YtDlpWorker*>(worker)) {
            stopWorker(ytDlpWorker);
        } else if (auto *galleryDlWorker = qobject_cast<GalleryDlWorker*>(worker)) {
            stopWorker(galleryDlWorker);
        } else {
            qWarning() << "Cannot stop unknown download worker:" << id;
        }
        
        adjustActiveDownloadCount(-1);
        qDebug() << "Paused active download:" << id;
        emit downloadPaused(id);
        paused = true; // Corrected: paused = true
    } else if (!paused && m_activeEmbedders.contains(id)) {
        qWarning() << "Cannot pause a download that is currently embedding metadata:" << id;
        emit downloadResumed(id); // Revert UI
        return;
    } else if (!paused) {
        qWarning() << "Cannot pause download (it may be finalizing or already stopped):" << id;
        emit downloadResumed(id); // Revert UI
        return;
    }

    if (paused) {
        emitDownloadStats();
        QMetaObject::invokeMethod(this, [this]() {
            m_queueManager->saveQueueStateAsync(m_activeItems);
            startNextDownload();
        }, Qt::QueuedConnection);
    }
}

void DownloadManager::unpauseDownload(const QString &id) {
    m_queueManager->unpauseDownload(id);
}

void DownloadManager::finishDownload(const QString &id) {
    if (m_activeWorkers.contains(id)) {
        QObject *worker = m_activeWorkers.value(id);
        YtDlpWorker *ytDlpWorker = qobject_cast<YtDlpWorker*>(worker);
        if (ytDlpWorker) {
            QVariantMap progressData;
            progressData[QStringLiteral("status")] = tr("Finishing stream & finalizing...");
            emit downloadProgress(id, progressData);
            
            QMetaObject::invokeMethod(ytDlpWorker, &YtDlpWorker::finishGracefully, Qt::QueuedConnection);
        }
    }
}

void DownloadManager::moveDownloadUp(const QString &id) {
    m_queueManager->moveDownloadUp(id);
}

void DownloadManager::moveDownloadDown(const QString &id) {
    m_queueManager->moveDownloadDown(id);
}
