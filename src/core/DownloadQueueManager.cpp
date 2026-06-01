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
#include <QStandardPaths>
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
    uiData[QStringLiteral("id")] = item.id;
    uiData[QStringLiteral("url")] = item.url;
    uiData[QStringLiteral("status")] = tr("Queued");
    uiData[QStringLiteral("progress")] = -1;
    uiData[QStringLiteral("options")] = item.options;
    uiData[QStringLiteral("playlistIndex")] = item.playlistIndex;
    const QString initialTitle = item.options.value("initial_title").toString().trimmed();
    if (!initialTitle.isEmpty()) {
        uiData[QStringLiteral("title")] = initialTitle;
    }
    
    if (isNew) {
        emit downloadAddedToQueue(uiData);
    } else {
        // If it's not a new item (e.g., updated after playlist expansion), update existing UI
        emit playlistExpansionPlaceholderUpdated(item.id, uiData);
    }

    emitQueueCountsChanged();
    emit requestStartNextDownload();
}

bool DownloadQueueManager::removePendingExpansionPlaceholder(const QString &id) {
    for (int i = 0; i < m_downloadQueue.size(); ++i) {
        if (m_downloadQueue.at(i).id == id) {
            m_downloadQueue.takeAt(i);
            m_pendingExpansions.remove(id);
            emit playlistExpansionPlaceholderRemoved(id);
            emitQueueCountsChanged();
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
            item.options[QStringLiteral("is_stopped")] = true;
            m_pausedItems[id] = item;
            qDebug() << "Stopped queued download:" << id;
            emit downloadCancelled(id);
            emitQueueCountsChanged();
            emit requestStartNextDownload();
            return true;
        }
    }

    if (m_pausedItems.contains(id)) {
        DownloadItem item = m_pausedItems.value(id);
        if (item.options.value(QStringLiteral("is_stopped")).toBool() || item.options.value(QStringLiteral("is_failed")).toBool()) {
            // Item was already stopped/failed. A second cancel means the user cleared it from the UI!
            m_pausedItems.remove(id);
            QStringList cleanupPaths;
            collectCleanupPath(cleanupPaths, item.tempFilePath);
            collectCleanupPath(cleanupPaths, item.originalDownloadedFilePath);
            for (const QString &candidate : item.options.value(QStringLiteral("cleanup_candidates")).toStringList()) {
                collectCleanupPath(cleanupPaths, candidate);
            }

            if (!cleanupPaths.isEmpty()) {
                QSet<QString> visitedDirs;
                for (const QString &cleanupPath : cleanupPaths) {
                    const QFileInfo anchor(cleanupPath);
                    if (!anchor.absoluteDir().exists()) {
                        continue;
                    }
                    if (anchor.isDir() && anchor.fileName() == id) {
                        QDir(anchor.absoluteFilePath()).removeRecursively();
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
                    if (tempDir.dirName() == id) {
                        tempDir.removeRecursively();
                    }
                }
                qDebug() << "Cleaned up temporary files for cleared download:" << id;
            }
            emitQueueCountsChanged();
            return true;
        } else {
            // Item was paused, now it's stopped
            m_pausedItems[id].options[QStringLiteral("is_stopped")] = true;
            qDebug() << "Stopped paused download:" << id;
            emit downloadCancelled(id);
            emitQueueCountsChanged();
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
        emit requestStartNextDownload();
        return true;
    }
    return false;
}

void DownloadQueueManager::moveDownloadUp(const QString &id) {
    for (int i = 1; i < m_downloadQueue.size(); ++i) { // Can't move 0 up
        if (m_downloadQueue.at(i).id == id) {
            m_downloadQueue.swapItemsAt(i, i - 1);
            emitQueueCountsChanged();
            break;
        }
    }
}

void DownloadQueueManager::moveDownloadDown(const QString &id) {
    for (int i = 0; i < m_downloadQueue.size() - 1; ++i) { // Can't move last down
        if (m_downloadQueue.at(i).id == id) {
            m_downloadQueue.swapItemsAt(i, i + 1);
            emitQueueCountsChanged();
            break;
        }
    }
}

void DownloadQueueManager::retryDownload(const QVariantMap &itemData) {
    DownloadItem item;
    item.id = itemData.value(QStringLiteral("id")).toString();
    item.url = itemData.value(QStringLiteral("url")).toString();
    item.options = itemData.value(QStringLiteral("options")).toMap();
    item.playlistIndex = itemData.value(QStringLiteral("playlistIndex"), -1).toInt();

    m_pausedItems.remove(item.id);
    item.options.remove(QStringLiteral("is_stopped"));
    item.options.remove(QStringLiteral("is_failed"));

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
        item.id = obj.value(QStringLiteral("id")).toString();
        item.url = obj.value(QStringLiteral("url")).toString();
        item.options = obj.value(QStringLiteral("options")).toObject().toVariantMap();
        item.metadata = obj.value(QStringLiteral("metadata")).toObject().toVariantMap();
        item.playlistIndex = obj.value(QStringLiteral("playlistIndex")).toInt(-1);
        item.tempFilePath = obj.value(QStringLiteral("tempFilePath")).toString();
        item.originalDownloadedFilePath = obj.value(QStringLiteral("originalDownloadedFilePath")).toString();
        
        QString status = obj.value(QStringLiteral("status")).toString(QStringLiteral("queued"));

        QVariantMap uiData;
        uiData[QStringLiteral("id")] = item.id;
        uiData[QStringLiteral("url")] = item.url;
        uiData[QStringLiteral("status")] = (status == "paused") ? tr("Paused") : tr("Queued");
        uiData[QStringLiteral("progress")] = 0;
        uiData[QStringLiteral("options")] = item.options;

        if (status == "paused") {
            m_pausedItems[item.id] = item;
            emit downloadAddedToQueue(uiData);
            emit downloadPaused(item.id);
        } else if (status == "stopped") {
            uiData[QStringLiteral("status")] = tr("Stopped");
            uiData[QStringLiteral("progress")] = 0;
        item.options[QStringLiteral("is_stopped")] = true;
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
