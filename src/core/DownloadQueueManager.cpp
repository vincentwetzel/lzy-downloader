#include "DownloadQueueManager.h"
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QTimer>
#include <QDir>
#include <QSet>
#include <QRegularExpression>

namespace {
QString normalizeCleanupStem(QString fileName)
{
    if (fileName.isEmpty()) {
        return fileName;
    }

    if (fileName.endsWith(".part", Qt::CaseInsensitive)) {
        fileName.chop(5);
    }
    if (fileName.endsWith(".ytdl", Qt::CaseInsensitive)) {
        fileName.chop(5);
    }
    if (fileName.endsWith(".aria2", Qt::CaseInsensitive)) {
        fileName.chop(6);
    }

    if (fileName.endsWith(".info.json", Qt::CaseInsensitive)) {
        fileName.chop(10);
    } else {
        static const QSet<QString> knownExts = {
            "mp4", "mkv", "webm", "m4a", "mp3", "opus", "ogg", "ts",
            "mov", "avi", "flac", "wav", "jpg", "jpeg", "png", "webp",
            "srt", "vtt", "ass", "lrc"
        };
        const QString ext = QFileInfo(fileName).suffix().toLower();
        if (knownExts.contains(ext)) {
            fileName.chop(ext.length() + 1);
        }
    }

    static const QRegularExpression formatIdRe("\\.f[a-zA-Z0-9_-]+$");
    const QRegularExpressionMatch match = formatIdRe.match(fileName);
    if (match.hasMatch()) {
        fileName.chop(match.capturedLength());
    }

    return fileName;
}

bool hasCleanupStemBoundary(const QString &fileName, const QString &stem)
{
    if (!fileName.startsWith(stem, Qt::CaseInsensitive)) {
        return false;
    }

    if (fileName.size() == stem.size()) {
        return true;
    }

    const QChar next = fileName.at(stem.size());
    return next == '.' || next == '[' || next == ' ' || next == '_' || next == '-';
}

bool shouldDeleteCleanupCandidate(const QFileInfo &entry, const QFileInfo &anchor, const QSet<QString> &stems)
{
    const QString fileName = entry.fileName();
    if (fileName.compare(anchor.fileName(), Qt::CaseInsensitive) == 0) {
        return true;
    }

    if (fileName.endsWith(".part", Qt::CaseInsensitive) ||
        fileName.endsWith(".ytdl", Qt::CaseInsensitive) ||
        fileName.endsWith(".info.json", Qt::CaseInsensitive) ||
        fileName.endsWith(".aria2", Qt::CaseInsensitive) ||
        fileName.contains(".part-Frag", Qt::CaseInsensitive)) {
        for (const QString &stem : stems) {
            if (hasCleanupStemBoundary(fileName, stem)) {
                return true;
            }
        }
    }

    static const QSet<QString> knownExts = {
        "mp4", "mkv", "webm", "m4a", "mp3", "opus", "ogg", "ts",
        "mov", "avi", "flac", "wav", "jpg", "jpeg", "png", "webp",
        "srt", "vtt", "ass", "lrc"
    };
    const QString ext = entry.suffix().toLower();
    if (knownExts.contains(ext)) {
        for (const QString &stem : stems) {
            if (hasCleanupStemBoundary(fileName, stem)) {
                return true;
            }
        }
    }

    return false;
}

void collectCleanupPath(QStringList &paths, const QString &path)
{
    const QString normalizedPath = QDir::fromNativeSeparators(path.trimmed());
    if (normalizedPath.isEmpty()) {
        return;
    }

    if (!paths.contains(normalizedPath, Qt::CaseInsensitive)) {
        paths.append(normalizedPath);
    }
}
}

DownloadQueueManager::DownloadQueueManager(ConfigManager *configManager, ArchiveManager *archiveManager, DownloadQueueState *queueState, QObject *parent)
    : QObject(parent), m_configManager(configManager), m_archiveManager(archiveManager), m_queueState(queueState) {
    connect(m_queueState, &DownloadQueueState::resumeDownloadsRequested, this, &DownloadQueueManager::processResumeDownloadsSelection);
}

DownloadQueueManager::~DownloadQueueManager() {
}

DownloadQueueManager::DuplicateStatus DownloadQueueManager::getDuplicateStatus(const QString &url, const QMap<QString, DownloadItem> &activeItems) const {
    // Check in the pending queue
    for (const DownloadItem &item : m_downloadQueue) {
        if (item.url == url) {
            return DuplicateInQueue;
        }
    }
    
    // Check in active downloads (provided by DownloadManager)
    for (const DownloadItem &item : activeItems) {
        if (item.url == url) {
            return DuplicateActive;
        }
    }
    
    // Check in paused items
    for (const DownloadItem &item : m_pausedItems) {
        if (item.url == url) {
            return DuplicatePaused;
        }
    }
    
    // Check in archive (completed downloads)
    if (m_archiveManager && m_archiveManager->isInArchive(url)) {
        return DuplicateCompleted;
    }
    
    return NotDuplicate;
}

bool DownloadQueueManager::isUrlInQueue(const QString &url, const QMap<QString, DownloadItem> &activeItems) const {
    return getDuplicateStatus(url, activeItems) != NotDuplicate;
}

void DownloadQueueManager::enqueueDownload(const DownloadItem &item, bool isNew) {
    m_downloadQueue.enqueue(item);
    
    QVariantMap uiData;
    uiData["id"] = item.id;
    uiData["url"] = item.url;
    uiData["status"] = "Queued";
    uiData["progress"] = -1;
    uiData["options"] = item.options;
    uiData["playlistIndex"] = item.playlistIndex;
    const QString initialTitle = item.options.value("initial_title").toString().trimmed();
    if (!initialTitle.isEmpty()) {
        uiData["title"] = initialTitle;
    }
    
    if (isNew) {
        emit downloadAddedToQueue(uiData);
    } else {
        // If it's not a new item (e.g., updated after playlist expansion), update existing UI
        emit playlistExpansionPlaceholderUpdated(item.id, uiData);
    }

    emitQueueCountsChanged();
    QMetaObject::invokeMethod(this, [this]() { saveQueueState(QMap<QString, DownloadItem>()); }, Qt::QueuedConnection);
    emit requestStartNextDownload();
}

bool DownloadQueueManager::removePendingExpansionPlaceholder(const QString &id) {
    for (int i = 0; i < m_downloadQueue.size(); ++i) {
        if (m_downloadQueue.at(i).id == id) {
            m_downloadQueue.takeAt(i);
            m_pendingExpansions.remove(id);
            emit playlistExpansionPlaceholderRemoved(id);
            emitQueueCountsChanged();
            QMetaObject::invokeMethod(this, [this]() { saveQueueState(QMap<QString, DownloadItem>()); }, Qt::QueuedConnection);
            emit requestStartNextDownload();
            return true;
        }
    }

    m_pendingExpansions.remove(id);
    return false;
}

bool DownloadQueueManager::cancelQueuedOrPausedDownload(const QString &id) {
    for (int i = 0; i < m_downloadQueue.size(); ++i) {
        if (m_downloadQueue.at(i).id == id) {
            DownloadItem item = m_downloadQueue.takeAt(i);
            item.options["is_stopped"] = true;
            m_pausedItems[id] = item;
            qDebug() << "Stopped queued download:" << id;
            emit downloadCancelled(id);
            emitQueueCountsChanged();
            QMetaObject::invokeMethod(this, [this]() { saveQueueState(QMap<QString, DownloadItem>()); }, Qt::QueuedConnection);
            emit requestStartNextDownload();
            return true;
        }
    }

    if (m_pausedItems.contains(id)) {
        DownloadItem item = m_pausedItems.value(id);
        if (item.options.value("is_stopped").toBool() || item.options.value("is_failed").toBool()) {
            // Item was already stopped/failed. A second cancel means the user cleared it from the UI!
            m_pausedItems.remove(id);
            QStringList cleanupPaths;
            collectCleanupPath(cleanupPaths, item.tempFilePath);
            collectCleanupPath(cleanupPaths, item.originalDownloadedFilePath);
            for (const QString &candidate : item.options.value("cleanup_candidates").toStringList()) {
                collectCleanupPath(cleanupPaths, candidate);
            }

            if (!cleanupPaths.isEmpty()) {
                QSet<QString> visitedDirs;
                for (const QString &cleanupPath : cleanupPaths) {
                    const QFileInfo anchor(cleanupPath);
                    if (!anchor.absoluteDir().exists()) {
                        continue;
                    }

                    QFile::remove(anchor.absoluteFilePath());

                    const QString dirPath = anchor.absolutePath();
                    if (visitedDirs.contains(dirPath)) {
                        continue;
                    }
                    visitedDirs.insert(dirPath);

                    QDir tempDir(dirPath);
                    QSet<QString> cleanupStems;
                    for (const QString &path : cleanupPaths) {
                        const QFileInfo candidateInfo(path);
                        if (candidateInfo.absolutePath().compare(dirPath, Qt::CaseInsensitive) != 0) {
                            continue;
                        }
                        cleanupStems.insert(normalizeCleanupStem(candidateInfo.fileName()));
                    }
                    cleanupStems.remove(QString());
                    if (cleanupStems.isEmpty()) {
                        continue;
                    }

                    QFileInfoList entries = tempDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
                    for (const QFileInfo &entry : entries) {
                        if (shouldDeleteCleanupCandidate(entry, anchor, cleanupStems)) {
                            QFile::remove(entry.absoluteFilePath());
                        }
                    }
                    
                    tempDir.refresh();
                    if (tempDir.dirName() == id && tempDir.isEmpty()) {
                        tempDir.removeRecursively();
                    }
                }
                qDebug() << "Cleaned up temporary files for cleared download:" << id;
            }
            emitQueueCountsChanged();
            QMetaObject::invokeMethod(this, [this]() { saveQueueState(QMap<QString, DownloadItem>()); }, Qt::QueuedConnection);
            return true;
        } else {
            // Item was paused, now it's stopped
            m_pausedItems[id].options["is_stopped"] = true;
            qDebug() << "Stopped paused download:" << id;
            emit downloadCancelled(id);
            emitQueueCountsChanged();
            QMetaObject::invokeMethod(this, [this]() { saveQueueState(QMap<QString, DownloadItem>()); }, Qt::QueuedConnection);
            return true;
        }
    }
    return false;
}

bool DownloadQueueManager::pauseQueuedDownload(const QString &id, DownloadItem &pausedItem) {
    for (int i = 0; i < m_downloadQueue.size(); ++i) {
        if (m_downloadQueue.at(i).id == id) {
            pausedItem = m_downloadQueue.takeAt(i);
            m_pausedItems[id] = pausedItem;
            qDebug() << "Paused queued download:" << id;
            emit downloadPaused(id);
            emitQueueCountsChanged();
            QMetaObject::invokeMethod(this, [this]() { saveQueueState(QMap<QString, DownloadItem>()); }, Qt::QueuedConnection);
            emit requestStartNextDownload();
            return true;
        }
    }
    return false;
}

bool DownloadQueueManager::unpauseDownload(const QString &id) {
    if (m_pausedItems.contains(id)) {
        DownloadItem item = m_pausedItems.take(id);
        m_downloadQueue.prepend(item); // Insert at front to resume immediately
        qDebug() << "Unpaused download:" << id;
        emit downloadResumed(id);
        emitQueueCountsChanged();
        QMetaObject::invokeMethod(this, [this]() { saveQueueState(QMap<QString, DownloadItem>()); }, Qt::QueuedConnection);
        emit requestStartNextDownload();
        return true;
    }
    return false;
}

void DownloadQueueManager::moveDownloadUp(const QString &id) {
    for (int i = 1; i < m_downloadQueue.size(); ++i) { // Can't move 0 up
        if (m_downloadQueue.at(i).id == id) {
            m_downloadQueue.swapItemsAt(i, i - 1);
            QMetaObject::invokeMethod(this, [this]() { saveQueueState(QMap<QString, DownloadItem>()); }, Qt::QueuedConnection);
            break;
        }
    }
}

void DownloadQueueManager::moveDownloadDown(const QString &id) {
    for (int i = 0; i < m_downloadQueue.size() - 1; ++i) { // Can't move last down
        if (m_downloadQueue.at(i).id == id) {
            m_downloadQueue.swapItemsAt(i, i + 1);
            QMetaObject::invokeMethod(this, [this]() { saveQueueState(QMap<QString, DownloadItem>()); }, Qt::QueuedConnection);
            break;
        }
    }
}

void DownloadQueueManager::retryDownload(const QVariantMap &itemData) {
    DownloadItem item;
    item.id = itemData["id"].toString();
    item.url = itemData["url"].toString();
    item.options = itemData["options"].toMap();
    item.playlistIndex = itemData.value("playlistIndex", -1).toInt();

    m_pausedItems.remove(item.id);
    item.options.remove("is_stopped");
    item.options.remove("is_failed");

    enqueueDownload(item, false); // false prevents spawning a new UI progress bar
}

void DownloadQueueManager::resumeDownload(const QVariantMap &itemData) {
    retryDownload(itemData);
}

void DownloadQueueManager::saveQueueState(const QMap<QString, DownloadItem> &activeItems) {
    m_queueState->save(activeItems.values(), m_pausedItems, m_downloadQueue);
}

void DownloadQueueManager::processResumeDownloadsSelection(const QJsonArray &arr) {
    for (const QJsonValue& val : arr) {
        QJsonObject obj = val.toObject();
        DownloadItem item;
        item.id = obj["id"].toString();
        item.url = obj["url"].toString();
        item.options = obj["options"].toObject().toVariantMap();
        item.metadata = obj["metadata"].toObject().toVariantMap();
        item.playlistIndex = obj["playlistIndex"].toInt(-1);
        item.tempFilePath = obj["tempFilePath"].toString();
        item.originalDownloadedFilePath = obj["originalDownloadedFilePath"].toString();
        
        QString status = obj["status"].toString("queued");

        QVariantMap uiData;
        uiData["id"] = item.id;
        uiData["url"] = item.url;
        uiData["status"] = (status == "paused") ? "Paused" : "Queued";
        uiData["progress"] = 0;
        uiData["options"] = item.options;

        if (status == "paused") {
            m_pausedItems[item.id] = item;
            emit downloadAddedToQueue(uiData);
            emit downloadPaused(item.id);
        } else if (status == "stopped") {
            uiData["status"] = "Stopped";
            uiData["progress"] = 0;
            item.options["is_stopped"] = true;
            m_pausedItems[item.id] = item;
            emit downloadAddedToQueue(uiData);
            emit downloadCancelled(item.id); // Triggers "Stopped" visuals in the UI
        } else {
            m_downloadQueue.enqueue(item);
            emit downloadAddedToQueue(uiData);
        }
    }
    emitQueueCountsChanged();
    emit requestStartNextDownload();
}

DownloadItem DownloadQueueManager::takeNextQueuedDownload() {
    for (int i = 0; i < m_downloadQueue.size(); ++i) {
        if (!m_pendingExpansions.contains(m_downloadQueue.at(i).id)) {
            DownloadItem item = m_downloadQueue.takeAt(i);
            emitQueueCountsChanged();
            return item;
        }
    }
    return DownloadItem(); // Return an invalid item if queue is empty
}

bool DownloadQueueManager::hasQueuedDownloads() const {
    for (const DownloadItem &item : m_downloadQueue) {
        if (!m_pendingExpansions.contains(item.id)) {
            return true;
        }
    }
    return false;
}

void DownloadQueueManager::emitQueueCountsChanged() {
    emit queueCountsChanged(m_downloadQueue.size(), m_pausedItems.size());
}
