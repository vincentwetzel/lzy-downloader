#include "DownloadManager.h"
#include "DownloadFinalizer.h"
#include "DownloadQueueManager.h"
#include "MetadataEmbedder.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>

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
        m_queueManager->saveQueueState(m_activeItems);
        QMetaObject::invokeMethod(this, [this]() {
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
        m_queueManager->saveQueueState(m_activeItems);
        QMetaObject::invokeMethod(this, [this]() {
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
        m_queueManager->saveQueueState(m_activeItems);
        QMetaObject::invokeMethod(this, [this]() {
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
        
        // CRITICAL: Synchronously flush state immediately so headless exits do not terminate
        // with a pending state save sitting in the event loop queue.
        m_queueManager->saveQueueState(m_activeItems);
        
    QMetaObject::invokeMethod(this, [this]() {
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
