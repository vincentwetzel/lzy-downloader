#include "DownloadManager.h"
#include "GalleryDlArgsBuilder.h"
#include "YtDlpArgsBuilder.h"
#include "DownloadQueueManager.h" // Include the new queue manager
#include "DownloadQueueState.h"
#include "ArchiveManager.h"
#include "SortingManager.h"
#include "GalleryDlWorker.h"
#include "YtDlpWorker.h"
#include "PlaylistExpander.h"
#include "DownloadFinalizer.h"
#include "MetadataEmbedder.h"
#include "core/ProcessUtils.h"
#include <QUuid>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStringList>
#include <QThread>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace {
bool shouldNormalizeSectionContainer(const DownloadItem &item)
{
    if (item.options.value("download_sections").toString().isEmpty()) {
        return false;
    }

    const QString suffix = QFileInfo(item.tempFilePath).suffix().toLower();
    return suffix == "mp4" || suffix == "m4v" || suffix == "mov" || suffix == "m4a";
}

bool isMetadataSidecarPath(const QString &path)
{
    return path.endsWith(".info.json", Qt::CaseInsensitive);
}

void appendCleanupCandidate(QVariantMap &options, const QString &path)
{
    const QString normalizedPath = QDir::fromNativeSeparators(path.trimmed());
    if (normalizedPath.isEmpty()) {
        return;
    }

    QStringList cleanupCandidates = options.value("cleanup_candidates").toStringList();
    if (!cleanupCandidates.contains(normalizedPath, Qt::CaseInsensitive)) {
        cleanupCandidates.append(normalizedPath);
        options["cleanup_candidates"] = cleanupCandidates;
    }
}

bool isNonInteractiveRequest(const QVariantMap &options)
{
    return options.value("non_interactive", false).toBool();
}

QString youtubeVideoIdFromUrl(const QString &urlString)
{
    const QUrl url(urlString);
    const QString host = url.host().toLower();
    if (host.contains("youtu.be")) {
        const QString id = url.path().section('/', 1, 1);
        return id.left(11);
    }

    if (host.contains("youtube.com") || host.contains("youtube-nocookie.com")) {
        const QString queryId = QUrlQuery(url).queryItemValue("v");
        if (!queryId.isEmpty()) {
            return queryId.left(11);
        }

        const QStringList parts = url.path().split('/', Qt::SkipEmptyParts);
        for (int i = 0; i + 1 < parts.size(); ++i) {
            const QString marker = parts.at(i).toLower();
            if (marker == "shorts" || marker == "embed" || marker == "live") {
                return parts.at(i + 1).left(11);
            }
        }
    }

    if (!host.isEmpty()) {
        return QString();
    }

    static const QRegularExpression idPattern(R"(([A-Za-z0-9_-]{11}))");
    const QRegularExpressionMatch match = idPattern.match(urlString);
    return match.hasMatch() ? match.captured(1) : QString();
}

QUrl sponsorBlockSegmentsUrl(const QString &videoId)
{
    const QByteArray hash = QCryptographicHash::hash(videoId.toUtf8(), QCryptographicHash::Sha256).toHex();
    QUrl url(QString("https://sponsor.ajay.app/api/skipSegments/%1").arg(QString::fromLatin1(hash.left(4))));
    QUrlQuery query;
    query.addQueryItem("service", "YouTube");
    query.addQueryItem("categories", R"(["preview","intro","selfpromo","interaction","filler","music_offtopic","sponsor","outro","hook"])");
    query.addQueryItem("actionTypes", R"(["skip","poi","chapter"])");
    url.setQuery(query);
    return url;
}

bool sponsorBlockResponseHasSegmentsForVideo(const QByteArray &data, const QString &videoId)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        return false;
    }

    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        if (!object.value("videoID").toString().isEmpty() && object.value("videoID").toString() != videoId) {
            continue;
        }
        if (object.value("segment").isArray()) {
            return true;
        }
    }

    return false;
}
}

DownloadManager::DownloadManager(ConfigManager *configManager, QObject *parent) : QObject(parent),
    m_configManager(configManager), m_archiveManager(nullptr), m_sleepMode(NoSleep),
    m_queuedDownloadsCount(0), m_activeDownloadsCount(0), m_completedDownloadsCount(0), m_errorDownloadsCount(0),
    m_isShuttingDown(false)
{

    m_queueState = new DownloadQueueState(this);
    m_sortingManager = new SortingManager(m_configManager, this);
    m_archiveManager = new ArchiveManager(m_configManager, this);

    applyMaxConcurrentSetting(m_configManager->get("General", "max_threads", "4").toString());

    m_sleepTimer = new QTimer(this);
    m_sleepTimer->setSingleShot(true);
    connect(m_sleepTimer, &QTimer::timeout, this, &DownloadManager::onSleepTimerTimeout);
    connect(m_configManager, &ConfigManager::settingChanged, this, &DownloadManager::onConfigSettingChanged);

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
        delete worker;
    }
    m_activeWorkers.clear();

    const QStringList embedderIds = m_activeEmbedders.keys();
    for (const QString &id : embedderIds) {
        QObject *embedder = m_activeEmbedders.take(id);
        if (!embedder) {
            continue;
        }
        embedder->disconnect(this);
        delete embedder;
    }
    m_activeEmbedders.clear();
    m_pendingSponsorBlockPreflights.clear();

    m_workerSpeeds.clear();
}

void DownloadManager::onQueueCountsChanged(int queued, int paused) {
    m_queuedDownloadsCount = queued;
    // m_pausedDownloadsCount is not directly stored in DownloadManager, but can be derived if needed.
    emitDownloadStats();
}

void DownloadManager::onConfigSettingChanged(const QString &section, const QString &key, const QVariant &value) {
    if (section != "General" || key != "max_threads") {
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

void DownloadManager::enqueueDownload(const QString &url, const QVariantMap &options) {
    QVariantMap effectiveOptions = options;
    if (isNonInteractiveRequest(effectiveOptions)) {
        effectiveOptions["override_archive"] = true;
        effectiveOptions["playlist_logic"] = "Download All (no prompt)";
        effectiveOptions["runtime_format_selected"] = true;
        effectiveOptions["download_sections_set"] = true;
    }

    // Check if URL is already in any state (prevents duplicate enqueuing)
    bool overrideArchive = effectiveOptions.value("override_archive", false).toBool();
    DownloadQueueManager::DuplicateStatus status = m_queueManager->getDuplicateStatus(url, m_activeItems);
    
    if (status != DownloadQueueManager::NotDuplicate) {
        // If it's only in completed and override is enabled, allow it
        if (status == DownloadQueueManager::DuplicateCompleted && overrideArchive) {
            qDebug() << "DownloadManager: Allowing re-download of completed URL (override enabled):" << url;
        } else {
            QString reason;
            switch (status) {
                case DownloadQueueManager::DuplicateInQueue:
                    reason = "This URL is already waiting in the download queue.";
                    break;
                case DownloadQueueManager::DuplicateActive:
                    reason = "This URL is currently being downloaded.";
                    break;
                case DownloadQueueManager::DuplicatePaused:
                    reason = "This download is paused.";
                    break;
                case DownloadQueueManager::DuplicateCompleted:
                    reason = "This URL has already been downloaded (use 'Override duplicate check' to re-download).";
                    break;
                default:
                    reason = "This URL is already in the system.";
                    break;
            }
            qDebug() << "DownloadManager: Skipping duplicate URL:" << url << "- Reason:" << reason;
            emit duplicateDownloadDetected(url, reason);
            return;
        }
    }

    // Intercept for download sections before anything else
    bool useSections = m_configManager->get("DownloadOptions", "download_sections_enabled", false).toBool();
    QString downloadTypeCheck = effectiveOptions.value("type", "video").toString();
    // The "download_sections_set" flag prevents an infinite loop.
    if (useSections && !isNonInteractiveRequest(effectiveOptions) && !effectiveOptions.contains("download_sections_set") && (downloadTypeCheck == "video" || downloadTypeCheck == "audio")) {
        qDebug() << "Download sections enabled, fetching metadata for" << url;
        fetchInfoForSections(url, effectiveOptions);
        return;
    }

    QString downloadType = effectiveOptions.value("type", "video").toString();

    // Intercept for runtime format selection before doing anything else
    bool needsRuntimeSelection = false;
    if (downloadType == "video") {
        if (m_configManager->get("Video", "video_quality").toString() == "Select at Runtime") {
            needsRuntimeSelection = true;
        }
    } else if (downloadType == "audio") {
        if (m_configManager->get("Audio", "audio_quality").toString() == "Select at Runtime") {
            needsRuntimeSelection = true;
        }
    }

    if (needsRuntimeSelection && !isNonInteractiveRequest(effectiveOptions) && !effectiveOptions.value("runtime_format_selected", false).toBool()) {
        fetchFormatsForSelection(url, effectiveOptions);
        return;
    }

    if (downloadType == "gallery") {
        DownloadItem item;
        item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        item.url = url;
        item.options = effectiveOptions;
        item.playlistIndex = -1; // Not part of a playlist initially

        m_queueManager->enqueueDownload(item);
    } else {
        // IMMEDIATE UI FEEDBACK: Create the download item in UI before playlist expansion
        DownloadItem item;
        item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        item.url = url;
        item.options = effectiveOptions;
        item.playlistIndex = -1;

        QVariantMap uiData;
        uiData["id"] = item.id;
        uiData["url"] = url;
        uiData["status"] = "Checking for playlist...";
        uiData["options"] = effectiveOptions;
        emit downloadAddedToQueue(uiData);

        PlaylistExpander *expander = new PlaylistExpander(url, m_configManager, this);
        expander->setProperty("options", effectiveOptions);
        expander->setProperty("queueId", item.id); // Store queue ID for later

        // Mark the placeholder as pending before it enters the queue so the
        // queue cannot start downloading the original playlist URL first.
        m_queueManager->m_pendingExpansions[item.id] = url;
        m_queueManager->enqueueDownload(item, false); // Enqueue as placeholder, not a "new" item for UI
        connect(expander, &PlaylistExpander::expansionFinished, this, &DownloadManager::onPlaylistExpanded);
        connect(expander, &PlaylistExpander::playlistDetected, this, &DownloadManager::onPlaylistDetected);

        QString playlistLogic = effectiveOptions.value("playlist_logic", "Ask").toString();
        expander->startExpansion(playlistLogic);
        emit playlistExpansionStarted(url);
    }
}

void DownloadManager::fetchInfoForSections(const QString &url, const QVariantMap &options)
{
    QProcess *process = new QProcess(this);
    QString ytDlpPath = ProcessUtils::findBinary("yt-dlp", m_configManager).path;

    QStringList args;
    args << "--dump-json" << "--no-playlist" << url;

    QString cookiesBrowser = m_configManager->get("General", "cookies_from_browser", "None").toString();
    if (cookiesBrowser != "None") {
        args << "--cookies-from-browser" << cookiesBrowser.toLower();
    }

    connect(process, &QProcess::finished, this, [this, process, url, options](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            QByteArray output = process->readAllStandardOutput();
            QJsonDocument doc = QJsonDocument::fromJson(output);
            if (doc.isObject()) {
                QVariantMap infoJson = doc.object().toVariantMap();
                QMetaObject::invokeMethod(this, [this, url, options, infoJson]() {
                    emit downloadSectionsRequested(url, options, infoJson);
                }, Qt::QueuedConnection);
            } else {
                qWarning() << "Failed to parse JSON for sections, enqueuing without them.";
                QVariantMap newOptions = options;
                newOptions["download_sections_set"] = true; // Prevent re-triggering
                enqueueDownload(url, newOptions);
            }
        } else {
            qWarning() << "Failed to fetch info for sections, enqueuing without them. Error:" << process->readAllStandardError();
            QVariantMap newOptions = options;
            newOptions["download_sections_set"] = true; // Prevent re-triggering
            enqueueDownload(url, newOptions);
        }
        process->deleteLater();
    });

    process->start(ytDlpPath, args);
}

void DownloadManager::fetchFormatsForSelection(const QString &url, const QVariantMap &options) {
    QProcess *process = new QProcess(this);
    QString ytDlpPath = ProcessUtils::findBinary("yt-dlp", m_configManager).path;
    
    QStringList args;
    args << "--dump-json" << "--no-playlist" << url;
    
    QString cookiesBrowser = m_configManager->get("General", "cookies_from_browser", "None").toString();
    if (cookiesBrowser != "None") {
        args << "--cookies-from-browser" << cookiesBrowser.toLower();
    }
    
    connect(process, &QProcess::finished, this, [this, process, url, options](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            QByteArray output = process->readAllStandardOutput();
            QJsonDocument doc = QJsonDocument::fromJson(output);
            if (doc.isObject()) {
                QVariantMap metadata = doc.object().toVariantMap();
                QVariantMap newOptions = options;
                if (metadata.value("is_live", false).toBool()) {
                    newOptions["is_live"] = true;
                }
                QMetaObject::invokeMethod(this, [this, url, newOptions, metadata]() {
                    emit formatSelectionRequested(url, newOptions, metadata);
                }, Qt::QueuedConnection);
            } else {
                const QString message = "yt-dlp returned invalid format metadata.";
                qWarning() << "DownloadManager: Invalid JSON returned from yt-dlp -J";
                QMetaObject::invokeMethod(this, [this, url, message]() {
                    emit formatSelectionFailed(url, message);
                }, Qt::QueuedConnection);
            }
        } else {
            const QString errorText = QString::fromUtf8(process->readAllStandardError()).trimmed();
            qWarning() << "DownloadManager: yt-dlp -J failed:" << errorText;
            QString message = errorText.isEmpty() ? "Failed to retrieve available formats." : errorText;
            QMetaObject::invokeMethod(this, [this, url, message]() {
                emit formatSelectionFailed(url, message);
            }, Qt::QueuedConnection);
        }
        process->deleteLater();
    });
    
    process->start(ytDlpPath, args);
}

void DownloadManager::resumeDownloadWithFormat(const QString &url, const QVariantMap &options, const QString &formatId) {
    QVariantMap newOptions = options;
    newOptions["runtime_format_selected"] = true;
    if (options.value("type", "video").toString() == "audio") {
        newOptions["runtime_audio_format"] = formatId;
    } else {
        newOptions["runtime_video_format"] = formatId;
    }
    enqueueDownload(url, newOptions);
}

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

void DownloadManager::proceedWithDownload() {
    if (!m_queueManager->hasQueuedDownloads()) {
        checkQueueFinished();
        return;
    }

    DownloadItem item = m_queueManager->takeNextQueuedDownload();
    m_activeDownloadsCount++;

    if (shouldPreflightSponsorBlock(item)) {
        startSponsorBlockPreflight(item);
        emitDownloadStats();
        return;
    }

    startDownloadItem(item, true);
}

void DownloadManager::startDownloadItem(DownloadItem item, bool alreadyCountedActive) {
    if (!alreadyCountedActive) {
        m_activeDownloadsCount++;
    }

    QString downloadType = item.options.value("type", "video").toString();

    if (downloadType == "gallery") {
        item.options["id"] = item.id;
        item.options["playlist_index"] = item.playlistIndex;
        GalleryDlArgsBuilder argsBuilder(m_configManager);
        QStringList args = argsBuilder.build(item.url, item.options);

        GalleryDlWorker *worker = new GalleryDlWorker(item.id, args, m_configManager, this);
        m_activeWorkers[item.id] = worker;
        m_activeItems[item.id] = item;

        connect(worker, &GalleryDlWorker::progressUpdated, this, &DownloadManager::onWorkerProgress);
        connect(worker, &GalleryDlWorker::finished, this, &DownloadManager::onGalleryDlWorkerFinished);
        connect(worker, &GalleryDlWorker::outputReceived, this, &DownloadManager::onWorkerOutputReceived);

        emit downloadStarted(item.id);
        worker->start();
    } else {
        item.options["id"] = item.id;
        item.options["playlist_index"] = item.playlistIndex;
        YtDlpArgsBuilder argsBuilder;
        QStringList args = argsBuilder.build(m_configManager, item.url, item.options);

        YtDlpWorker *worker = new YtDlpWorker(item.id, args, m_configManager, this);
        m_activeWorkers[item.id] = worker;
        m_activeItems[item.id] = item;

        connect(worker, &YtDlpWorker::progressUpdated, this, &DownloadManager::onWorkerProgress);
        connect(worker, &YtDlpWorker::finished, this, &DownloadManager::onWorkerFinished);
        connect(worker, &YtDlpWorker::outputReceived, this, &DownloadManager::onWorkerOutputReceived);
        connect(worker, &YtDlpWorker::ytDlpErrorDetected, this, &DownloadManager::onYtDlpErrorDetected);

        emit downloadStarted(item.id);
        worker->start();
    }
    emitDownloadStats();
}

bool DownloadManager::shouldPreflightSponsorBlock(const DownloadItem &item) const {
    if (!m_configManager->get("General", "sponsorblock", false).toBool()) {
        return false;
    }
    if (item.options.value("sponsorblock_segments_checked", false).toBool()) {
        return false;
    }

    const QString downloadType = item.options.value("type", "video").toString();
    const bool isLivestream = item.options.value("is_live", false).toBool()
        || item.options.value("wait_for_video", false).toBool();
    if (downloadType != "video" && !isLivestream) {
        return false;
    }

    return !youtubeVideoIdFromUrl(item.url).isEmpty();
}

void DownloadManager::startSponsorBlockPreflight(const DownloadItem &item) {
    const QString videoId = youtubeVideoIdFromUrl(item.url);
    if (videoId.isEmpty()) {
        DownloadItem fallbackItem = item;
        fallbackItem.options["sponsorblock_segments_checked"] = false;
        startDownloadItem(fallbackItem, true);
        return;
    }

    m_pendingSponsorBlockPreflights[item.id] = item;

    QVariantMap progressData;
    progressData["status"] = "Checking SponsorBlock segments...";
    progressData["progress"] = -1;
    emit downloadProgress(item.id, progressData);

    QNetworkAccessManager *networkManager = new QNetworkAccessManager(this);
    QNetworkReply *reply = networkManager->get(QNetworkRequest(sponsorBlockSegmentsUrl(videoId)));
    QTimer::singleShot(8000, reply, [reply]() {
        if (reply->isRunning()) {
            reply->abort();
        }
    });

    const QString itemId = item.id;
    connect(reply, &QNetworkReply::finished, this, [this, reply, networkManager, videoId, itemId]() {
        if (m_isShuttingDown || !m_pendingSponsorBlockPreflights.contains(itemId)) {
            reply->deleteLater();
            networkManager->deleteLater();
            return;
        }

        DownloadItem checkedItem = m_pendingSponsorBlockPreflights.take(itemId);
        bool checked = false;
        bool hasSegments = false;

        const QVariant statusAttr = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const int httpStatus = statusAttr.isValid() ? statusAttr.toInt() : 0;
        const QNetworkReply::NetworkError error = reply->error();

        if (error == QNetworkReply::NoError) {
            checked = true;
            hasSegments = sponsorBlockResponseHasSegmentsForVideo(reply->readAll(), videoId);
        } else if (httpStatus == 404) {
            checked = true;
            hasSegments = false;
        } else {
            qWarning() << "SponsorBlock preflight failed for" << videoId << ":" << reply->errorString()
                       << "- falling back to accurate cut arguments.";
        }

        checkedItem.options["sponsorblock_segments_checked"] = checked;
        checkedItem.options["sponsorblock_has_segments"] = hasSegments;

        qInfo() << "SponsorBlock preflight for" << videoId
                << "checked=" << checked
                << "hasSegments=" << hasSegments;

        startDownloadItem(checkedItem, true);
        reply->deleteLater();
        networkManager->deleteLater();
    });
}

void DownloadManager::applyMaxConcurrentSetting(const QString &maxThreadsStr) {
    if (maxThreadsStr == "1 (short sleep)") {
        m_maxConcurrentDownloads = 1;
        m_sleepMode = ShortSleep;
    } else if (maxThreadsStr == "1 (long sleep)") {
        m_maxConcurrentDownloads = 1;
        m_sleepMode = LongSleep;
    } else {
        m_maxConcurrentDownloads = qMax(1, maxThreadsStr.toInt());
        m_sleepMode = NoSleep;
    }
}

void DownloadManager::startDownloadsToCapacity() {
    while ((m_activeWorkers.count() + m_pendingSponsorBlockPreflights.count()) < m_maxConcurrentDownloads && m_queueManager->hasQueuedDownloads()) {
        if (m_sleepMode != NoSleep && m_maxConcurrentDownloads == 1) {
            if (!m_sleepTimer->isActive()) {
                int sleepDuration = (m_sleepMode == ShortSleep) ? 5000 : 30000;
                qDebug() << "Starting sleep timer for" << sleepDuration << "ms.";
                m_sleepTimer->start(sleepDuration);
            }
            return;
        }

        proceedWithDownload();
    }

    checkQueueFinished();
}

void DownloadManager::startNextDownload() {
    startDownloadsToCapacity();
}

void DownloadManager::onSleepTimerTimeout() {
    qDebug() << "Sleep timer timed out. Attempting to start next download.";
    startNextDownload();
}

void DownloadManager::onWorkerProgress(const QString &id, const QVariantMap &progressData) {
    if (m_activeItems.contains(id)) {
        DownloadItem &item = m_activeItems[id];
        const QString currentFile = progressData.value("current_file").toString().trimmed();
        if (!currentFile.isEmpty()) {
            const QString normalizedCurrentFile = QDir::fromNativeSeparators(currentFile);
            item.tempFilePath = normalizedCurrentFile;
            appendCleanupCandidate(item.options, normalizedCurrentFile);
            if (!isMetadataSidecarPath(normalizedCurrentFile)) {
                item.originalDownloadedFilePath = normalizedCurrentFile;
            }
        }

        const QString thumbnailPath = progressData.value("thumbnail_path").toString().trimmed();
        if (!thumbnailPath.isEmpty()) {
            appendCleanupCandidate(item.options, thumbnailPath);
        }
    }

    m_workerSpeeds[id] = progressData.value("speed_bytes", 0.0).toDouble();
    updateTotalSpeed();
    emit downloadProgress(id, progressData);
}

void DownloadManager::onWorkerOutputReceived(const QString &id, const QString &output) {
    Q_UNUSED(id);
    Q_UNUSED(output);
    // Raw console output from workers can be processed or logged here if needed
}

QString DownloadManager::effectivePlaylistTitle(const DownloadItem &item) const {
    auto isUsable = [](const QString &value) {
        return !value.isEmpty() && value.compare("unknown", Qt::CaseInsensitive) != 0;
    };

    const QStringList playlistKeys = {"playlist_title", "playlist"};
    for (const QString &key : playlistKeys) {
        const QString value = item.metadata.value(key).toString().trimmed();
        if (isUsable(value)) {
            return value;
        }
    }
    for (const QString &key : playlistKeys) {
        const QString value = item.options.value(key).toString().trimmed();
        if (isUsable(value)) {
            return value;
        }
    }

    const QString metadataAlbum = item.metadata.value("album").toString().trimmed();
    if (isUsable(metadataAlbum)) {
        return metadataAlbum;
    }
    const QString optionsAlbum = item.options.value("album").toString().trimmed();
    if (isUsable(optionsAlbum)) {
        return optionsAlbum;
    }

    return QString();
}

void DownloadManager::applyAudioPlaylistAlbumMetadata(DownloadItem &item) const {
    if (item.options.value("type").toString() != "audio" || item.playlistIndex <= 0) {
        return;
    }

    const QString playlistTitle = effectivePlaylistTitle(item);
    if (playlistTitle.isEmpty()) {
        return;
    }

    if (item.metadata.value("playlist_title").toString().trimmed().isEmpty()) {
        item.metadata["playlist_title"] = playlistTitle;
    }
    if (item.options.value("playlist_title").toString().trimmed().isEmpty()) {
        item.options["playlist_title"] = playlistTitle;
    }

    if (m_configManager->get("Metadata", "force_playlist_as_album", false).toBool()) {
        item.metadata["album"] = playlistTitle;
        item.metadata["album_artist"] = "Various Artists";
    }
}

void DownloadManager::onWorkerFinished(const QString &id, bool success, const QString &message, const QString &finalFilename, const QString &originalDownloadedFilename, const QVariantMap &metadata) {
    if (!m_activeWorkers.contains(id)) return;

    QObject *workerObj = m_activeWorkers.take(id);
    DownloadItem &item = m_activeItems[id];
    m_workerSpeeds.remove(id);
    updateTotalSpeed();
    workerObj->deleteLater();

    m_activeDownloadsCount--;

    if (!success) {
        DownloadItem item = m_activeItems.take(id);
        item.options["is_failed"] = true;
        m_queueManager->m_pausedItems[id] = item;
        
        m_errorDownloadsCount++;
        emit downloadFinished(id, false, message); // This will trigger emitDownloadStats()
        emitDownloadStats();
        QMetaObject::invokeMethod(this, [this]() {
            m_queueManager->saveQueueState(m_activeItems);
            startNextDownload();
        }, Qt::QueuedConnection);
        return;
    }

    if (metadata.contains("height") && metadata["height"].toInt() < 480) {
        QString url = item.url;
        QMetaObject::invokeMethod(this, [this, url]() {
            emit videoQualityWarning(url, "Downloaded video quality is below 480p.");
        }, Qt::QueuedConnection);
    }

    QString normalizedFinal = QDir::fromNativeSeparators(finalFilename);
    QString normalizedOriginal = QDir::fromNativeSeparators(originalDownloadedFilename);

    item.tempFilePath = normalizedFinal.isEmpty() ? normalizedOriginal : normalizedFinal;
    item.originalDownloadedFilePath = normalizedOriginal;
    item.metadata = metadata;
    if (metadata.contains("postprocessor_warning")) {
        item.options["completion_warning"] = metadata.value("postprocessor_warning").toString();
        emit downloadProgress(id, {{"status", "Completed with post-processing warning"}});
    }

    // Inject playlist_index into metadata for sorting manager
    if (item.playlistIndex != -1) {
        item.metadata["playlist_index"] = item.playlistIndex;
        qDebug() << "Injected playlist_index" << item.playlistIndex << "into metadata for sorting.";
    }
    if (item.options.value("is_playlist").toBool()) {
        item.metadata["is_playlist"] = true;
    }
    if (item.options.contains("playlist_title") && !item.metadata.contains("playlist_title")) {
        item.metadata["playlist_title"] = item.options.value("playlist_title");
    }
    applyAudioPlaylistAlbumMetadata(item);

    const bool needsTrackEmbedding = (item.options.value("type").toString() == "audio" && item.playlistIndex > 0);
    const bool needsSectionNormalization = shouldNormalizeSectionContainer(item);
    const QString thumbnailPath = item.metadata.value("thumbnail_path").toString();
    const bool wantsEmbed = m_configManager->get("Metadata", "embed_thumbnail", true).toBool();
    const bool hasAbandonedThumb = wantsEmbed && !thumbnailPath.isEmpty() && QFile::exists(thumbnailPath);

    if (needsTrackEmbedding || needsSectionNormalization || hasAbandonedThumb) {
        QVariantMap progressData;
        progressData["status"] = needsSectionNormalization
            ? "Normalizing clip container metadata..."
            : (hasAbandonedThumb && !needsTrackEmbedding ? "Embedding thumbnail..." : "Embedding metadata...");
        emit downloadProgress(id, progressData);

        MetadataEmbedder *embedder = new MetadataEmbedder(m_configManager, this);
        m_activeEmbedders[id] = embedder;
        connect(embedder, &MetadataEmbedder::finished, this, [this, id](bool s, const QString &e){
            onMetadataEmbedded(id, s, e);
        });
        if (hasAbandonedThumb) {
            embedder->setProperty("thumbnail_path", thumbnailPath);
        }
        QVariantMap extraMetadata;
        if (item.options.value("type").toString() == "audio" && item.playlistIndex > 0
            && m_configManager->get("Metadata", "force_playlist_as_album", false).toBool()) {
            const QString playlistTitle = effectivePlaylistTitle(item);
            if (!playlistTitle.isEmpty()) {
                extraMetadata["album"] = playlistTitle;
                extraMetadata["album_artist"] = "Various Artists";
            }
        }
        if (!extraMetadata.isEmpty()) {
            embedder->setExtraMetadata(extraMetadata);
        }
        embedder->processFile(item.tempFilePath, needsTrackEmbedding ? item.playlistIndex : 0, needsSectionNormalization);
    } else {
        m_finalizer->finalize(id, item);
    }
    emitDownloadStats(); // Update stats after worker finishes, before finalizer starts
}

void DownloadManager::onGalleryDlWorkerFinished(const QString &id, bool success, const QString &message, const QString &finalFilename, const QVariantMap &metadata) {
    if (!m_activeWorkers.contains(id)) return;

    QObject *workerObj = m_activeWorkers.take(id);
    DownloadItem &item = m_activeItems[id];
    m_workerSpeeds.remove(id);
    updateTotalSpeed();
    workerObj->deleteLater();

    m_activeDownloadsCount--;

    if (!success) {
        DownloadItem item = m_activeItems.take(id);
        item.options["is_failed"] = true;
        m_queueManager->m_pausedItems[id] = item;
        
        m_errorDownloadsCount++;
        emit downloadFinished(id, false, message); // This will trigger emitDownloadStats()
        emitDownloadStats();
        QMetaObject::invokeMethod(this, [this]() {
            m_queueManager->saveQueueState(m_activeItems);
            startNextDownload();
        }, Qt::QueuedConnection);
        return;
    }

    item.tempFilePath = finalFilename;
    item.originalDownloadedFilePath = "";
    item.metadata = metadata;

    m_finalizer->finalize(id, item);
    emitDownloadStats();
}

void DownloadManager::onYtDlpErrorDetected(const QString &id, const QString &errorType, const QString &userMessage, const QString &rawError) {
    if (!m_activeItems.contains(id)) {
        qWarning() << "onYtDlpErrorDetected called for inactive/unknown ID:" << id;
        return;
    }

    // Pass the item data to the UI to allow for actions like retrying or opening the URL.
    QVariantMap itemData;
    const DownloadItem& item = m_activeItems.value(id);
    itemData["id"] = item.id;
    itemData["url"] = item.url;
    itemData["options"] = item.options;
    itemData["playlistIndex"] = item.playlistIndex;

    // Forward to UI for popup display
    QMetaObject::invokeMethod(this, [this, id, errorType, userMessage, rawError, itemData]() {
        emit ytDlpErrorPopupRequested(id, errorType, userMessage, rawError, itemData);
    }, Qt::QueuedConnection);
}

void DownloadManager::onMetadataEmbedded(const QString &id, bool success, const QString &error) {
    if (!m_activeEmbedders.contains(id)) return;

    MetadataEmbedder *embedder = qobject_cast<MetadataEmbedder*>(m_activeEmbedders.take(id));
    embedder->deleteLater();

    DownloadItem &item = m_activeItems[id];

    if (success) {
        m_finalizer->finalize(id, item);
    } else {
        DownloadItem item = m_activeItems.take(id);
        item.options["is_failed"] = true;
        m_queueManager->m_pausedItems[id] = item;
        
        m_errorDownloadsCount++;
        emit downloadFinished(id, false, "Metadata embedding failed: " + error); // This will trigger emitDownloadStats()
        emitDownloadStats();
        QMetaObject::invokeMethod(this, [this]() {
            m_queueManager->saveQueueState(m_activeItems);
            startNextDownload();
        }, Qt::QueuedConnection);
    }
}

void DownloadManager::onFinalizationComplete(const QString &id, bool success, const QString &message) {
    QString finalMessage = message;
    if (success && m_activeItems.contains(id)) {
        const QString warning = m_activeItems.value(id).options.value("completion_warning").toString();
        if (!warning.isEmpty()) {
            finalMessage += "\n" + warning;
        }
    }

    if (success) {
        m_completedDownloadsCount++;
    } else {
        m_errorDownloadsCount++;
    }
    
    emit downloadFinished(id, success, finalMessage);
    m_activeItems.remove(id);

    emitDownloadStats();
    QMetaObject::invokeMethod(this, [this]() {
        m_queueManager->saveQueueState(m_activeItems);
        startNextDownload();
    }, Qt::QueuedConnection);
}

/*
FIXME: The implementation for onItemCleared was causing build errors because it is not declared in DownloadManager.h.
To fix this, declare it as a public slot in DownloadManager.h:
    public slots:
        void onItemCleared(const QString &id, bool wasSuccessful, bool wasFinished);
Then, uncomment the function body below and the corresponding connect() call in MainWindow.cpp.
*/

void DownloadManager::checkQueueFinished() {
    const bool hasPendingPlaylistExpansions = m_queueManager && !m_queueManager->m_pendingExpansions.isEmpty();

    bool hasActivelyPausedItems = false;
    if (m_queueManager) {
        for (const DownloadItem &item : m_queueManager->m_pausedItems) {
            if (!item.options.value("is_stopped").toBool() && !item.options.value("is_failed").toBool()) {
                hasActivelyPausedItems = true;
                break;
            }
        }
    }

    bool isQueueEmptyAndIdle = m_activeWorkers.isEmpty()
        && m_pendingSponsorBlockPreflights.isEmpty()
        && !m_queueManager->hasQueuedDownloads()
        && m_activeItems.isEmpty()
        && !hasPendingPlaylistExpansions
        && !hasActivelyPausedItems;

    if (isQueueEmptyAndIdle) {
        if (property("queueWasActive").toBool()) {
            setProperty("queueWasActive", false);
            emit queueFinished();
        }
    } else {
        setProperty("queueWasActive", true);
    }
}

void DownloadManager::updateTotalSpeed() {
    double totalSpeed = 0.0;
    for (double speed : m_workerSpeeds.values()) {
        totalSpeed += speed;
    }
    emit totalSpeedUpdated(totalSpeed);
}

void DownloadManager::emitDownloadStats() {
    emit downloadStatsUpdated(m_queuedDownloadsCount, m_activeDownloadsCount, m_completedDownloadsCount, m_errorDownloadsCount);
}
