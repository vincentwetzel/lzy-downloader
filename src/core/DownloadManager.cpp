#include "DownloadManager.h"
#include "DownloadQueueManager.h"
#include "DownloadQueueState.h"
#include "ArchiveManager.h"
#include "SortingManager.h"
#include "DownloadFinalizer.h"
#include "GalleryDlWorker.h"
#include "MetadataEmbedder.h"
#include "core/ProcessUtils.h"
#include "YtDlpWorker.h"
#include "PowerInhibitor.h"
#include <QDebug>
#include <QMetaObject>
#include <QProcess>
#include <QThread>
#include <QTimer>

DownloadManager::DownloadManager(ConfigManager *configManager, QObject *parent) : QObject(parent),
    m_configManager(configManager), m_archiveManager(nullptr), m_sleepMode(NoSleep),
    m_queuedDownloadsCount(0), m_activeDownloadsCount(0), m_completedDownloadsCount(0), m_errorDownloadsCount(0),
    m_isShuttingDown(false)
{

    m_queueState = new DownloadQueueState(this);
    m_sortingManager = new SortingManager(m_configManager, this);
    m_archiveManager = new ArchiveManager(m_configManager, this);

    m_sleepTimer = new QTimer(this);
    m_sleepTimer->setSingleShot(true);
    connect(m_sleepTimer, &QTimer::timeout, this, &DownloadManager::onSleepTimerTimeout);
    m_globalCapacityTimer = new QTimer(this);
    m_globalCapacityTimer->setSingleShot(true);
    m_globalCapacityTimer->setInterval(1000);
    connect(m_globalCapacityTimer, &QTimer::timeout, this, &DownloadManager::onGlobalCapacityRetry);
    connect(m_configManager, &ConfigManager::settingChanged, this, &DownloadManager::onConfigSettingChanged);

    applyMaxConcurrentSetting(m_configManager->get(QStringLiteral("General"), QStringLiteral("max_threads"), QStringLiteral("4")).toString());

    m_finalizer = new DownloadFinalizer(m_configManager, m_sortingManager, m_archiveManager, this);
    connect(m_finalizer, &DownloadFinalizer::progressUpdated, this, [this](const QString &id, const QVariantMap &data) {
        emit downloadProgress(id, data);
    });
    connect(m_finalizer, &DownloadFinalizer::finalPathReady, this, &DownloadManager::downloadFinalPathReady);
    connect(m_finalizer, &DownloadFinalizer::finalizationComplete, this, &DownloadManager::onFinalizationComplete);

    m_queueManager = new DownloadQueueManager(m_configManager, m_archiveManager, m_queueState, this); // m_queueState is passed to queueManager
    connect(m_queueManager, &DownloadQueueManager::downloadAddedToQueue, this, &DownloadManager::downloadAddedToQueue);
    connect(m_queueManager, &DownloadQueueManager::downloadCancelled, this, &DownloadManager::downloadCancelled);
    connect(m_queueManager, &DownloadQueueManager::downloadPaused, this, &DownloadManager::downloadPaused);
    connect(m_queueManager, &DownloadQueueManager::downloadResumed, this, &DownloadManager::downloadResumed);
    connect(m_queueManager, &DownloadQueueManager::duplicateDownloadDetected, this, &DownloadManager::duplicateDownloadDetected);
    connect(m_queueManager, &DownloadQueueManager::requestStartNextDownload, this, &DownloadManager::onRequestStartNextDownload, Qt::QueuedConnection);
    connect(m_queueManager, &DownloadQueueManager::queueCountsChanged, this, &DownloadManager::onQueueCountsChanged);
    connect(m_queueManager, &DownloadQueueManager::playlistExpansionPlaceholderRemoved, this, &DownloadManager::onPlaylistExpansionPlaceholderRemoved);
    connect(m_queueManager, &DownloadQueueManager::playlistExpansionPlaceholderUpdated, this, &DownloadManager::onPlaylistExpansionPlaceholderUpdated);
    QTimer::singleShot(0, this, [this]() {
        m_queueState->load();
        // Queue restoration is synchronous within load(); reconcile only after
        // restored stopped/paused items have been registered as protected IDs.
        m_queueManager->cleanupOrphanedTempDirectories();
    });

    emitDownloadStats();
}

DownloadManager::~DownloadManager() {
    shutdown();
}

void DownloadManager::stopWorker(QObject *worker)
{
    if (!worker) {
        return;
    }

    QMetaObject::invokeMethod(worker, [worker]() {
        if (auto *ytDlpWorker = qobject_cast<YtDlpWorker *>(worker)) {
            ytDlpWorker->killProcess();
        } else if (auto *galleryDlWorker = qobject_cast<GalleryDlWorker *>(worker)) {
            galleryDlWorker->killProcess();
        }
        QThread::currentThread()->quit();
    }, Qt::QueuedConnection);
}

void DownloadManager::stopEmbedder(QObject *embedder)
{
    if (!embedder) {
        return;
    }

    QMetaObject::invokeMethod(embedder, [embedder]() {
        if (auto *metadataEmbedder = qobject_cast<MetadataEmbedder *>(embedder)) {
            metadataEmbedder->cancel();
        }
        QThread::currentThread()->quit();
    }, Qt::QueuedConnection);
}

void DownloadManager::shutdown() {
    if (m_isShuttingDown) {
        return;
    }
    m_isShuttingDown = true;
    m_powerInhibitor.release();

    qInfo() << "[DownloadManager] Shutdown requested. Terminating active downloads and helper processes.";

    if (m_queueManager) {
        m_queueManager->saveQueueState(m_activeItems);
    }

    if (m_sleepTimer && m_sleepTimer->isActive()) {
        m_sleepTimer->stop();
    }
    if (m_globalCapacityTimer && m_globalCapacityTimer->isActive()) {
        m_globalCapacityTimer->stop();
    }

    const QList<QProcess*> descendantProcesses = findChildren<QProcess*>();
    for (QProcess *process : std::as_const(descendantProcesses)) {
        process->disconnect(); // Prevent reading buffers from dying process
        ProcessUtils::terminateProcessTree(process);
    }

    for (QObject *worker : std::as_const(m_activeWorkers)) {
        if (worker) {
            worker->disconnect(this);
            if (QThread *workerThread = worker->thread()) {
                // A queued QThread::started handler can otherwise launch the
                // process after shutdown has begun.
                QObject::disconnect(workerThread, &QThread::started, worker, nullptr);
            }
            if (auto *ytDlpWorker = qobject_cast<YtDlpWorker*>(worker)) {
                stopWorker(ytDlpWorker);
            } else if (auto *galleryDlWorker = qobject_cast<GalleryDlWorker*>(worker)) {
                stopWorker(galleryDlWorker);
            }
        }
    }
    m_activeWorkers.clear();

    for (QObject *embedder : std::as_const(m_activeEmbedders)) {
        if (embedder) {
            embedder->disconnect(this);
            stopEmbedder(embedder);
        }
    }
    m_activeEmbedders.clear();
    m_pendingSponsorBlockPreflights.clear();

    // Worker and metadata threads are children of this manager. Ensure their
    // event loops have exited before QObject destruction; otherwise a timed
    // test or headless shutdown can destroy a live QThread and abort.
    const QList<QThread *> workerThreads = findChildren<QThread *>();
    for (QThread *thread : workerThreads) {
        if (thread && thread != QThread::currentThread() && thread->isRunning()) {
            if (!thread->wait(5000)) {
                qWarning() << "DownloadManager shutdown: forcing worker thread exit:" << thread->objectName();
                thread->quit();
                thread->wait(1000);
            }
        }
    }

    m_workerSpeeds.clear();
}

void DownloadManager::adjustActiveDownloadCount(int delta)
{
    const int oldActiveDownloadsCount = m_activeDownloadsCount;
    m_activeDownloadsCount = qMax(0, m_activeDownloadsCount + delta);

    if (delta < 0 && oldActiveDownloadsCount > 0) {
        m_globalDownloadLimiter.release();
    }

    if (m_activeDownloadsCount > 0) {
        if (!m_powerInhibitor.isActive()) {
            if (m_powerInhibitor.acquire()) {
                qInfo() << "System idle-sleep inhibition enabled for active downloads.";
            } else {
                qWarning() << "Download activity is not protected from system sleep on this platform.";
            }
        }
    } else {
        const bool wasActive = m_powerInhibitor.isActive();
        m_powerInhibitor.release();
        if (wasActive) {
            qInfo() << "System idle-sleep inhibition released after download activity ended.";
        }
    }
}

void DownloadManager::onQueueCountsChanged(int queued, int paused) {
    m_queuedDownloadsCount = queued;
    // m_pausedDownloadsCount is not directly stored in DownloadManager, but can be derived if needed.
    emitDownloadStats();

    // Save queue state to disk, preserving active items
    QMetaObject::invokeMethod(this, [this]() {
        if (m_queueManager) {
            m_queueManager->saveQueueStateAsync(m_activeItems);
        }
    }, Qt::QueuedConnection);

    // Update queue position statuses
    if (m_queueManager) {
        int position = 1;
        for (const DownloadItem &item : std::as_const(m_queueManager->m_downloadQueue)) {
            if (!m_queueManager->m_pendingExpansions.contains(item.id)) {
                QVariantMap progressData;
                progressData.insert(QStringLiteral("status"), tr("Queued (Position %1)").arg(position));
                emit downloadProgress(item.id, progressData);
                position++;
            }
        }
    }
}

void DownloadManager::onConfigSettingChanged(const QString &section, const QString &key, const QVariant &value) {
    if (section != QStringLiteral("General") || key != QStringLiteral("max_threads")) {
        return;
    }

    applyMaxConcurrentSetting(value.toString());
    
    // Only attempt to start downloads if there are actually items in the queue.
    // This prevents a spurious queueFinished signal when the user clicks Download 
    // and the UI saves the max_threads setting before the new item is enqueued.
    if (m_queueManager && m_queueManager->hasQueuedDownloads()) {
        startDownloadsToCapacity();
    }
}

void DownloadManager::onRequestStartNextDownload() {
    startDownloadsToCapacity();
}
