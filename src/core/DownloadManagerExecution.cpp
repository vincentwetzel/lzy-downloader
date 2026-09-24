#include "DownloadManager.h"
#include "DownloadQueueManager.h"
#include "GalleryDlArgsBuilder.h"
#include "GalleryDlWorker.h"
#include "YtDlpArgsBuilder.h"
#include "YtDlpWorker.h"
#include <QDebug>
#include <QThread>
#include <QTimer>
#include <QDateTime>
#include <chrono>

void DownloadManager::proceedWithDownload() {
    if (!m_queueManager->hasQueuedDownloads()) {
        checkQueueFinished();
        return;
    }

    // The GUI and --server mode can be separate processes. Reserve a slot in
    // the shared limiter before removing an item from this process's queue.
    if (!m_globalDownloadLimiter.tryAcquire(m_maxConcurrentDownloads)) {
        if (!m_globalCapacityTimer->isActive()) {
            m_globalCapacityTimer->start();
        }
        return;
    }

    DownloadItem item = m_queueManager->takeNextQueuedDownload();
    adjustActiveDownloadCount(1);
    startDownloadItem(item, true);
}

void DownloadManager::startDownloadItem(DownloadItem item, bool alreadyCountedActive) {
    if (!alreadyCountedActive) {
        adjustActiveDownloadCount(1);
    }

    const QString downloadType = item.options.value(QStringLiteral("type"), QStringLiteral("video")).toString();

    if (downloadType == QStringLiteral("gallery")) {
        item.options.insert(QStringLiteral("id"), item.id);
        item.options.insert(QStringLiteral("playlist_index"), item.playlistIndex);
        GalleryDlArgsBuilder argsBuilder(m_configManager);
        const QStringList args = argsBuilder.build(item.url, item.options);

        QThread *workerThread = new QThread(this);
        workerThread->setObjectName(QStringLiteral("gallery-download-%1").arg(item.id));
        GalleryDlWorker *worker = new GalleryDlWorker(item.id, args, m_configManager, nullptr);
        worker->moveToThread(workerThread);
        connect(workerThread, &QThread::started, worker, [this, worker]() {
            if (m_isShuttingDown) {
                QThread::currentThread()->quit();
                return;
            }
            worker->start();
        }, Qt::QueuedConnection);
        connect(worker, &GalleryDlWorker::finished, workerThread, &QThread::quit, Qt::DirectConnection);
        connect(workerThread, &QThread::finished, worker, &QObject::deleteLater);
        connect(workerThread, &QThread::finished, workerThread, &QObject::deleteLater);
        m_activeWorkers.insert(item.id, worker);
        m_activeItems.insert(item.id, item);

        connect(worker, &GalleryDlWorker::progressUpdated, this, &DownloadManager::onWorkerProgress);
        connect(worker, &GalleryDlWorker::finished, this, &DownloadManager::onGalleryDlWorkerFinished);
        connect(worker, &GalleryDlWorker::outputReceived, this, &DownloadManager::onWorkerOutputReceived);

        emit downloadStarted(item.id);
        workerThread->start();
    } else {
        item.options.insert(QStringLiteral("id"), item.id);
        item.options.insert(QStringLiteral("playlist_index"), item.playlistIndex);
        YtDlpArgsBuilder argsBuilder;
        const QStringList args = argsBuilder.build(m_configManager, item.url, item.options);

        QThread *workerThread = new QThread(this);
        workerThread->setObjectName(QStringLiteral("yt-dlp-download-%1").arg(item.id));
        YtDlpWorker *worker = new YtDlpWorker(item.id, args, m_configManager, nullptr);
        worker->moveToThread(workerThread);
        connect(workerThread, &QThread::started, worker, [this, worker]() {
            if (m_isShuttingDown) {
                QThread::currentThread()->quit();
                return;
            }
            worker->start();
        }, Qt::QueuedConnection);
        connect(worker, &YtDlpWorker::finished, workerThread, &QThread::quit, Qt::DirectConnection);
        connect(workerThread, &QThread::finished, worker, &QObject::deleteLater);
        connect(workerThread, &QThread::finished, workerThread, &QObject::deleteLater);
        m_activeWorkers.insert(item.id, worker);
        m_activeItems.insert(item.id, item);

        connect(worker, &YtDlpWorker::progressUpdated, this, &DownloadManager::onWorkerProgress);
        connect(worker, &YtDlpWorker::finished, this, &DownloadManager::onWorkerFinished);
        connect(worker, &YtDlpWorker::outputReceived, this, &DownloadManager::onWorkerOutputReceived);
        connect(worker, &YtDlpWorker::ytDlpErrorDetected, this, &DownloadManager::onYtDlpErrorDetected);

        emit downloadStarted(item.id);
        workerThread->start();
    }
    emitDownloadStats();
}

void DownloadManager::applyMaxConcurrentSetting(const QString &maxThreadsStr) {
    const SleepMode oldSleepMode = m_sleepMode;

    if (maxThreadsStr == QStringLiteral("1 (short sleep)")) {
        m_maxConcurrentDownloads = 1;
        m_sleepMode = ShortSleep;
    } else if (maxThreadsStr == QStringLiteral("1 (long sleep)")) {
        m_maxConcurrentDownloads = 1;
        m_sleepMode = LongSleep;
    } else {
        m_maxConcurrentDownloads = qMax(1, maxThreadsStr.toInt());
        m_sleepMode = NoSleep;
    }

    if (oldSleepMode != m_sleepMode && m_sleepTimer && m_sleepTimer->isActive()) {
        m_sleepTimer->stop();
    }
}

void DownloadManager::startDownloadsToCapacity() {
    if (m_isShuttingDown) {
        return;
    }

    // QSettings is shared by GUI and server-mode processes, but settingChanged
    // is an in-process signal. Sync before admission so a setting changed in
    // the other surface becomes the global limit here as well.
    m_configManager->save();
    applyMaxConcurrentSetting(m_configManager->get(QStringLiteral("General"), QStringLiteral("max_threads"), QStringLiteral("4")).toString());

    if (m_sleepTimer->isActive()) {
        if (!m_queueManager->hasQueuedDownloads()) {
            m_sleepTimer->stop();
            checkQueueFinished();
        }
        return;
    }

    if (m_sleepMode != NoSleep && m_maxConcurrentDownloads == 1 &&
        m_queueManager->hasQueuedDownloads() && m_activeWorkers.count() < m_maxConcurrentDownloads) {
        
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 sleepDuration = (m_sleepMode == ShortSleep) ? 5000 : 30000;
        const qint64 timeSinceLastFinish = now - m_lastDownloadFinishTime;

        if (m_lastDownloadFinishTime > 0 && timeSinceLastFinish < sleepDuration) {
            const int remainingSleep = static_cast<int>(sleepDuration - qMax(Q_INT64_C(0), timeSinceLastFinish));
            qDebug() << "Starting sleep timer for remaining" << remainingSleep << "ms.";
            m_sleepTimer->start(std::chrono::milliseconds(remainingSleep));
            return;
        }
    }

    while (m_activeWorkers.count() < m_maxConcurrentDownloads && m_queueManager->hasQueuedDownloads()) {
        proceedWithDownload();
    }

    checkQueueFinished();
}

void DownloadManager::startNextDownload() {
    startDownloadsToCapacity();
}

void DownloadManager::onSleepTimerTimeout() {
    m_sleepTimer->stop();
    qDebug() << "Sleep timer timed out. Attempting to start next download.";
    startNextDownload();
}

void DownloadManager::onGlobalCapacityRetry()
{
    startDownloadsToCapacity();
}
