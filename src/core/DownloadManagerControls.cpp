#include "DownloadManager.h"
#include "DownloadQueueManager.h"
#include "GalleryDlWorker.h"
#include "PlaylistExpander.h"
#include "YtDlpArgsBuilder.h"
#include "YtDlpWorker.h"
#include "core/ProcessUtils.h"
#include <QDebug>
#include <QMetaObject>
#include <QProcess>

void DownloadManager::cancelDownload(const QString &id) {
    bool cancelled = false;
    
    // Delegate to queue manager first for queued/paused items
    if (m_queueManager->cancelQueuedOrPausedDownload(id)) {
        cancelled = true;
    }

    if (m_queueManager->m_pendingExpansions.contains(id)) {
        m_queueManager->m_pendingExpansions.remove(id);
        
        // Find and terminate the background PlaylistExpander process
        const QList<PlaylistExpander*> expanders = findChildren<PlaylistExpander*>();
        for (PlaylistExpander *expander : expanders) {
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

    if (m_pendingSponsorBlockPreflights.contains(id)) {
        DownloadItem item = m_pendingSponsorBlockPreflights.take(id);
        item.options["is_stopped"] = true;
        m_queueManager->m_pausedItems[id] = item;
        m_activeDownloadsCount = qMax(0, m_activeDownloadsCount - 1);
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

        YtDlpWorker *ytDlpWorker = qobject_cast<YtDlpWorker*>(worker);
        if (ytDlpWorker) {
            ytDlpWorker->killProcess();
        } else {
            GalleryDlWorker *galleryDlWorker = qobject_cast<GalleryDlWorker*>(worker);
            if (galleryDlWorker) {
                galleryDlWorker->killProcess();
            }
        }

        worker->disconnect(this);
        
        // Ensure all child processes belonging to this worker are forcefully killed
        const QList<QProcess*> processes = worker->findChildren<QProcess*>();
        for (QProcess *p : processes) {
            if (p->state() != QProcess::NotRunning) {
                p->disconnect();
                ProcessUtils::terminateProcessTree(p);
                p->kill();
            }
        }

        worker->deleteLater();
        m_activeDownloadsCount--;
        
        item.options["is_stopped"] = true;
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
        
        const QList<QProcess*> processes = embedder->findChildren<QProcess*>();
        for (QProcess *p : processes) {
            if (p->state() != QProcess::NotRunning) {
                p->disconnect();
                ProcessUtils::terminateProcessTree(p);
                p->kill();
            }
        }

        // Deleting the embedder will kill any active QProcess internally
        embedder->deleteLater();
        
        item.options["is_stopped"] = true;
        m_queueManager->m_pausedItems[id] = item;
        
        m_activeDownloadsCount--;
        if (!cancelled) {
            emit downloadCancelled(id);
            cancelled = true;
        }
    }

    if (cancelled) {
        emitDownloadStats();
        QMetaObject::invokeMethod(this, [this]() {
            m_queueManager->saveQueueState(m_activeItems);
            startNextDownload();
        }, Qt::QueuedConnection);
    }
}

void DownloadManager::retryDownload(const QVariantMap &itemData) {
    m_queueManager->retryDownload(itemData);
}

void DownloadManager::restartDownloadWithOptions(const QVariantMap &itemData) {
    QString id = itemData.value("id").toString();

    if (!m_activeItems.contains(id)) {
        // Fallback for non-active items, just treat as a normal retry
        qWarning() << "restartDownloadWithOptions called for non-active ID:" << id << ". Falling back to retry.";
        retryDownload(itemData);
        return;
    }

    qDebug() << "Restarting active download with new options:" << id;

    // 1. Get the active worker and kill it.
    if (m_activeWorkers.contains(id)) {
        QObject *worker = m_activeWorkers.take(id);
        // Disconnect signals to prevent onWorkerFinished from being called with an error
        worker->disconnect(this);
        
        YtDlpWorker *ytDlpWorker = qobject_cast<YtDlpWorker*>(worker);
        if (ytDlpWorker) {
            ytDlpWorker->killProcess();
        }
        
        const QList<QProcess*> processes = worker->findChildren<QProcess*>();
        for (QProcess *p : processes) {
            if (p->state() != QProcess::NotRunning) {
                p->disconnect();
                ProcessUtils::terminateProcessTree(p);
                p->kill();
            }
        }

        worker->deleteLater();
    }

    // 2. The item is still in m_activeItems. We will reuse it.
    DownloadItem &item = m_activeItems[id];
    item.options = itemData.value("options").toMap(); // Update options

    // 3. Tell the UI to reset its state for the existing item.
    QVariantMap resetData;
    resetData["id"] = id;
    resetData["status"] = "Waiting for video...";
    resetData["progress"] = -1; // Indeterminate progress
    emit downloadProgress(id, resetData);

    // 4. Create and start a new worker with the same ID and new options.
    YtDlpArgsBuilder argsBuilder;
    QStringList args = argsBuilder.build(m_configManager, item.url, item.options);
    YtDlpWorker *newWorker = new YtDlpWorker(item.id, args, m_configManager, this);
    m_activeWorkers[item.id] = newWorker;

    connect(newWorker, &YtDlpWorker::progressUpdated, this, &DownloadManager::onWorkerProgress);
    connect(newWorker, &YtDlpWorker::finished, this, &DownloadManager::onWorkerFinished);
    connect(newWorker, &YtDlpWorker::outputReceived, this, &DownloadManager::onWorkerOutputReceived);
    connect(newWorker, &YtDlpWorker::ytDlpErrorDetected, this, &DownloadManager::onYtDlpErrorDetected);

    newWorker->start();
}

void DownloadManager::resumeDownload(const QVariantMap &itemData) {
    retryDownload(itemData);
}

void DownloadManager::pauseDownload(const QString &id) {
    bool paused = false;
    
    DownloadItem pausedItem; // To capture the item if it's from the queue

    if (!paused && m_activeWorkers.contains(id)) {
        QObject *worker = m_activeWorkers.take(id);
        m_queueManager->m_pausedItems[id] = m_activeItems.take(id); // Add to queue manager's paused items
        
        m_workerSpeeds.remove(id);
        updateTotalSpeed();

        YtDlpWorker *ytDlpWorker = qobject_cast<YtDlpWorker*>(worker);
        if (ytDlpWorker) {
            ytDlpWorker->killProcess();
        } else {
            GalleryDlWorker *galleryDlWorker = qobject_cast<GalleryDlWorker*>(worker);
            if (galleryDlWorker) {
                galleryDlWorker->killProcess();
            }
        }
        
        worker->disconnect(this);
        
        const QList<QProcess*> processes = worker->findChildren<QProcess*>();
        for (QProcess *p : processes) {
            if (p->state() != QProcess::NotRunning) {
                p->disconnect();
                ProcessUtils::terminateProcessTree(p);
                p->kill();
            }
        }

        worker->deleteLater();
        m_activeDownloadsCount--;
        qDebug() << "Paused active download:" << id;
        emit downloadPaused(id);
        paused = true; // Corrected: paused = true
    } else if (!paused && m_pendingSponsorBlockPreflights.contains(id)) {
        DownloadItem item = m_pendingSponsorBlockPreflights.take(id);
        item.options["is_stopped"] = true;
        m_queueManager->m_pausedItems[id] = item;
        m_activeDownloadsCount = qMax(0, m_activeDownloadsCount - 1);
        qDebug() << "Paused SponsorBlock preflight download:" << id;
        emit downloadPaused(id);
        paused = true;
    } else if (!paused && m_activeEmbedders.contains(id)) {
        qWarning() << "Cannot pause a download that is currently embedding metadata:" << id;
        emit downloadResumed(id); // Revert UI
        paused = true;
    }

    if (paused) {
        emitDownloadStats();
        QMetaObject::invokeMethod(this, [this]() {
            m_queueManager->saveQueueState(m_activeItems);
            startNextDownload();
        }, Qt::QueuedConnection);
    }
}

void DownloadManager::unpauseDownload(const QString &id) {
    m_queueManager->unpauseDownload(id);
}

void DownloadManager::moveDownloadUp(const QString &id) {
    m_queueManager->moveDownloadUp(id);
}

void DownloadManager::moveDownloadDown(const QString &id) {
    m_queueManager->moveDownloadDown(id);
}


