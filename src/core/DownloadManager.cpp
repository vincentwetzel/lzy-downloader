#include "DownloadManager.h"
#include "DownloadQueueManager.h"
#include "DownloadQueueState.h"
#include "ArchiveManager.h"
#include "SortingManager.h"
#include "DownloadFinalizer.h"
#include "core/ProcessUtils.h"
#include <QDebug>
#include <QMetaObject>
#include <QProcess>
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
    QTimer::singleShot(0, this, [this]() { m_queueState->load(); }); // Load queue state after connections are established

    emitDownloadStats();
}

DownloadManager::~DownloadManager() {
    shutdown();
}

void DownloadManager::shutdown() {
    if (m_isShuttingDown) {
        return;
    }
    m_isShuttingDown = true;

    qInfo() << "[DownloadManager] Shutdown requested. Terminating active downloads and helper processes.";

    if (m_queueManager) {
        m_queueManager->saveQueueState(m_activeItems);
    }

    if (m_sleepTimer && m_sleepTimer->isActive()) {
        m_sleepTimer->stop();
    }

    const QList<QProcess*> descendantProcesses = findChildren<QProcess*>();
    for (QProcess *process : descendantProcesses) {
        process->disconnect(); // Prevent reading buffers from dying process
        ProcessUtils::terminateProcessTree(process);
    }

    const QStringList activeIds = m_activeWorkers.keys();
    for (const QString &id : activeIds) {
        QObject *worker = m_activeWorkers.take(id);
        if (!worker) {
            continue;
        }
        worker->disconnect(this);
        worker->deleteLater();
    }
    m_activeWorkers.clear();

    const QStringList embedderIds = m_activeEmbedders.keys();
    for (const QString &id : embedderIds) {
        QObject *embedder = m_activeEmbedders.take(id);
        if (!embedder) {
            continue;
        }
        embedder->disconnect(this);
        embedder->deleteLater();
    }
    m_activeEmbedders.clear();
    m_pendingSponsorBlockPreflights.clear();

    m_workerSpeeds.clear();
}

void DownloadManager::onQueueCountsChanged(int queued, int paused) {
    m_queuedDownloadsCount = queued;
    // m_pausedDownloadsCount is not directly stored in DownloadManager, but can be derived if needed.
    emitDownloadStats();

    // Save queue state to disk, preserving active items
    QMetaObject::invokeMethod(this, [this]() {
        if (m_queueManager) {
            m_queueManager->saveQueueState(m_activeItems);
        }
    }, Qt::QueuedConnection);

    // Update queue position statuses
    if (m_queueManager) {
        int position = 1;
        for (const DownloadItem &item : m_queueManager->m_downloadQueue) {
            if (!m_queueManager->m_pendingExpansions.contains(item.id)) {
                QVariantMap progressData;
                progressData[QStringLiteral("status")] = tr("Queued (Position %1)").arg(position);
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
