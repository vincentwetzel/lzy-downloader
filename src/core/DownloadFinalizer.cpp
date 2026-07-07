#include "DownloadFinalizer.h"
#include "ConfigManager.h"
#include "SortingManager.h"
#include "ArchiveManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QCoreApplication>
#include <QUrlQuery>
#include <QDebug>
#include <QSet>
#include <QRegularExpression>
#include <QPointer>
#include <array>
#include <algorithm>

namespace { // Anonymous namespace to limit scope to this file

QString cleanupStem(QString fileName)
{
    if (fileName.endsWith(QStringLiteral(".part"), Qt::CaseInsensitive)) {
        fileName.chop(5);
    }
    if (fileName.endsWith(QStringLiteral(".ytdl"), Qt::CaseInsensitive)) {
        fileName.chop(5);
    }
    if (fileName.endsWith(QStringLiteral(".aria2"), Qt::CaseInsensitive)) {
        fileName.chop(6);
    }

    if (fileName.endsWith(QStringLiteral(".info.json"), Qt::CaseInsensitive)) {
        fileName.chop(10);
    } else {
        static constexpr std::array<QStringView, 20> knownExts = {
            u"mp4", u"mkv", u"webm", u"m4a", u"mp3", u"opus", u"ogg", u"ts",
            u"mov", u"avi", u"flac", u"wav", u"jpg", u"jpeg", u"png", u"webp",
            u"srt", u"vtt", u"ass", u"lrc"
        };
        const QString ext = QFileInfo(fileName).suffix();
        auto it = std::ranges::find_if(knownExts, [&](QStringView knownExt) {
            return ext.compare(knownExt, Qt::CaseInsensitive) == 0;
        });
        if (it != knownExts.end()) {
            fileName.chop(ext.length() + 1);
        }
    }

    static const QRegularExpression formatIdRe(QStringLiteral("\\.f[a-zA-Z0-9_-]+$"));
    const QRegularExpressionMatch match = formatIdRe.match(fileName);
    if (match.hasMatch()) {
        fileName.chop(match.capturedLength());
    }

    return fileName;
}

bool safeRemoveWithRetry(const QString &filePath, int retries = 5, int delayMs = 100)
{
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        return true;
    }
    for (int i = 0; i < retries; ++i) {
        if (QFile::remove(filePath)) {
            return true;
        }
        QThread::msleep(delayMs);
    }
    return false;
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
    return next == QLatin1Char('.') || next == QLatin1Char('[') || next == QLatin1Char(' ') || next == QLatin1Char('_') || next == QLatin1Char('-');
}

void cleanupTempFiles(const DownloadItem &item, const QDir &tempDir, const QString &mediaInfoJsonPath)
{
    if (item.options.value(QStringLiteral("type")).toString() == QStringLiteral("gallery")) {
        return;
    }

    // 1. Clean up the info.json that matches the media file name.
    if (QFile::exists(mediaInfoJsonPath)) {
        if (safeRemoveWithRetry(mediaInfoJsonPath)) {
            qDebug() << "Cleaned up media info.json:" << mediaInfoJsonPath;
        } else {
            qWarning() << "Failed to clean up media info.json:" << mediaInfoJsonPath;
        }
    }

    // 2. Clean up any explicitly tracked temporary files (like _wait_thumbnail.jpg)
    const QStringList candidates = item.options.value(QStringLiteral("cleanup_candidates")).toStringList();
    for (const QString &candidate : candidates) {
        const QFileInfo candidateInfo(candidate);
        if (candidateInfo.exists() && candidateInfo.isFile()) {
            if (safeRemoveWithRetry(candidate)) {
                qDebug() << "Cleaned up tracked candidate file:" << candidate;
            } else {
                qWarning() << "Failed to clean up tracked candidate file:" << candidate;
            }
        }
    }

    // 3. Clean up leftover auxiliary files (like un-embedded .jpg thumbnails) matching the video basename.
    // Avoid QDir wildcard filters here: raw titles can contain '[' and ']', which are special in
    // Qt's wildcard-to-regex conversion and can produce invalid QRegularExpression warnings.
    QFileInfo tempFileInfo(item.tempFilePath);
    const QString baseName = cleanupStem(tempFileInfo.fileName());
    if (!baseName.isEmpty()) {
        const QString tempFilePath = tempFileInfo.absoluteFilePath();
        const QString originalFilePath = item.originalDownloadedFilePath.isEmpty()
            ? QString()
            : QFileInfo(item.originalDownloadedFilePath).absoluteFilePath();
        const QFileInfoList leftoverFiles = tempDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);

        for (const QFileInfo &leftoverInfo : leftoverFiles) {
            const QString filePath = leftoverInfo.absoluteFilePath();
            if (filePath == tempFilePath || (!originalFilePath.isEmpty() && filePath == originalFilePath)) {
                continue;
            }

            if (hasCleanupStemBoundary(leftoverInfo.fileName(), baseName)) {
                if (!safeRemoveWithRetry(filePath)) {
                    qWarning() << "Failed to remove leftover file:" << filePath;
                }
            }
        }
    }

    // 4. If it was a playlist download, find and remove the playlist's info.json file.
    QString playlistId;

    // Strategy 1: Get playlist_id from the original playlist URL stored in options. This is the most reliable.
    if (item.options.contains(QStringLiteral("original_playlist_url"))) {
        const QUrl url(item.options.value(QStringLiteral("original_playlist_url")).toString());
        if (url.hasQuery()) {
            const QUrlQuery query(url);
            if (query.hasQueryItem(QStringLiteral("list"))) {
                playlistId = query.queryItemValue(QStringLiteral("list"));
            }
        }
    }

    // Strategy 2: Fallback to metadata if the original URL wasn't available for some reason.
    if (playlistId.isEmpty() && item.metadata.contains(QStringLiteral("playlist_id"))) {
        playlistId = item.metadata.value(QStringLiteral("playlist_id")).toString();
        qDebug() << "Using playlist_id from metadata as fallback for cleanup:" << playlistId;
    }

    if (!playlistId.isEmpty()) {
        const QString matchStr = QStringLiteral("[%1]").arg(playlistId);
        const QStringList potentialFiles = tempDir.entryList({QStringLiteral("*.info.json")}, QDir::Files);
        for (const QString &fileName : potentialFiles) {
            // Check if the filename contains the playlist ID, typically formatted as "[<playlist_id>]"
            // by yt-dlp's default playlist output template. This is more reliable than parsing JSON content.
            if (fileName.contains(matchStr)) {
                const QString filePath = tempDir.filePath(fileName);
                if (safeRemoveWithRetry(filePath)) {
                    qDebug() << "Cleaned up playlist info.json by filename match:" << filePath;
                } else {
                    qWarning() << "Failed to remove playlist info.json by filename match:" << filePath;
                }
                break; // Assume only one such file exists per playlist.
            }
        }
    } else {
        qDebug() << "No playlist_id found for cleanup for item:" << item.id;
    }
}

bool copyDirectoryRecursivelyInternal(const QString &sourceDir, const QString &destDir) {
    const QDir source(sourceDir);
    if (!source.exists()) {
        qWarning() << "copyDirectoryRecursively: source does not exist:" << sourceDir;
        return false;
    }
    const QDir dest(destDir);
    if (!dest.exists()) {
        if (!dest.mkpath(QStringLiteral("."))) {
            qWarning() << "copyDirectoryRecursively: failed to create dest dir:" << destDir;
            return false;
        }
    }
    bool success = true;
    const QFileInfoList entries = source.entryInfoList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Files);
    for (const QFileInfo &entry : entries) {
        const QString srcPath = entry.absoluteFilePath();
        const QString dstPath = dest.absoluteFilePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyDirectoryRecursivelyInternal(srcPath, dstPath)) {
                success = false;
            }
        } else {
            if (QFile::exists(dstPath)) {
                if (!QFile::remove(dstPath)) {
                    qWarning() << "copyDirectoryRecursively: failed to remove existing file:" << dstPath;
                }
            }
            if (!QFile::copy(srcPath, dstPath)) {
                qWarning() << "copyDirectoryRecursively: failed to copy" << srcPath << "to" << dstPath;
                success = false;
            }
        }
    }
    return success;
}

} // namespace

DownloadFinalizer::DownloadFinalizer(ConfigManager *configManager, SortingManager *sortingManager, ArchiveManager *archiveManager, QObject *parent)
    : QObject(parent), m_configManager(configManager), m_sortingManager(sortingManager), m_archiveManager(archiveManager) {
}

bool DownloadFinalizer::copyDirectoryRecursively(const QString &sourceDir, const QString &destDir) {
    return copyDirectoryRecursivelyInternal(sourceDir, destDir);
}

void DownloadFinalizer::finalize(const QString &id, DownloadItem item) {
    qDebug() << "Starting finalize for id:" << id;

    QPointer<DownloadFinalizer> self(this);
    QPointer<SortingManager> sortingManager(m_sortingManager);
    QPointer<ArchiveManager> archiveManager(m_archiveManager);

    // Resolve config values on the main thread safely to prevent cross-thread QSettings data races
    const QString downloadType = item.options.value(QStringLiteral("type")).toString();
    const bool isAudio = (downloadType == QStringLiteral("audio"));
    bool prefixPlaylistIndices = isAudio;
    if (m_configManager) {
        prefixPlaylistIndices = m_configManager->get(QStringLiteral("DownloadOptions"), QStringLiteral("prefix_playlist_indices"), isAudio).toBool();
    }

    QThread *thread = QThread::create([self, id, item, sortingManager, archiveManager, prefixPlaylistIndices, downloadType]() mutable {

        if (downloadType != QStringLiteral("gallery") && !item.metadata.contains(QStringLiteral("id"))) {
            qWarning() << "Metadata is missing core fields in finalize for id:" << id << ", attempting to read from disk.";
            const QFileInfo fi(item.tempFilePath);
            const QString baseName = cleanupStem(fi.fileName());
            const QString jsonPath = fi.absoluteDir().filePath(QStringLiteral("%1.info.json").arg(baseName));

            QFile jsonFile(jsonPath);
            if (jsonFile.open(QIODevice::ReadOnly)) {
                const QByteArray jsonData = jsonFile.readAll();
                jsonFile.close();
                QJsonParseError parseError;
                const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
                if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                    const QVariantMap diskMetadata = doc.object().toVariantMap();
                    for (auto it = diskMetadata.constBegin(); it != diskMetadata.constEnd(); ++it) {
                        item.metadata.insert(it.key(), it.value());
                    }
                    qDebug() << "Successfully loaded metadata from fallback for id:" << id;
                } else {
                    qWarning() << "Invalid info.json in fallback:" << jsonPath << "Parse error:" << parseError.errorString();
                }
            } else {
                qWarning() << "Could not open info.json for fallback:" << jsonPath;
            }

            if (item.metadata.isEmpty()) {
                qWarning() << "Continuing finalization without metadata for id:" << id
                           << "- sorting rules may fall back to the default directory.";
            }
        }

        // Extract the real gallery ID from the filenames if missing, so {id} sorting tokens work
        if (downloadType == QStringLiteral("gallery") && !item.metadata.contains(QStringLiteral("id"))) {
            const QDir galleryTempDir(QDir::fromNativeSeparators(item.tempFilePath));
            if (galleryTempDir.exists()) {
                const QFileInfoList entries = galleryTempDir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files);
                if (!entries.isEmpty()) {
                    const QString firstFile = entries.first().fileName();
                    // Extract typical gallery-dl ID patterns (e.g., tiktok_7646992089099586847_...)
                    static const QRegularExpression idRe(QStringLiteral(R"(_(\d{10,25})_)"));
                    const QRegularExpressionMatch match = idRe.match(firstFile);
                    if (match.hasMatch()) {
                        item.metadata.insert(QStringLiteral("id"), match.captured(1));
                        qDebug() << "Extracted real gallery ID for sorting:" << match.captured(1);
                    }
                }
            }
        }

        const QFileInfo fileInfo(item.tempFilePath);
        if (!fileInfo.exists()) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [self, id]() {
                if (self) emit self->finalizationComplete(id, false, DownloadFinalizer::tr("Downloaded file not found."));
            }, Qt::QueuedConnection);
            return;
        }

        if (item.playlistIndex != -1 && !item.metadata.contains(QStringLiteral("playlist_index"))) {
            item.metadata.insert(QStringLiteral("playlist_index"), item.playlistIndex);
        }

        auto sendProgress = [self, id](const QString &statusMsg) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [self, id, statusMsg]() {
                if (self) emit self->progressUpdated(id, {{QStringLiteral("status"), statusMsg}});
            }, Qt::QueuedConnection);
        };

        sendProgress(DownloadFinalizer::tr("Verifying download completeness..."));
        sendProgress(DownloadFinalizer::tr("Applying sorting rules..."));

        QString finalDir;
        bool sortingOk = false;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [&finalDir, &sortingOk, sortingManager, meta = item.metadata, opts = item.options]() {
            if (sortingManager) {
                finalDir = QDir(sortingManager->getSortedDirectory(meta, opts)).absolutePath();
                sortingOk = true;
            }
        }, Qt::BlockingQueuedConnection);
        if (!sortingOk || finalDir.isEmpty()) {
            finalDir = fileInfo.absolutePath(); // fallback if sorting manager is dead
        }

        QString finalName = fileInfo.fileName();

        if (prefixPlaylistIndices && item.playlistIndex > 0) {
            const QString paddedIndex = QString::number(item.playlistIndex).rightJustified(2, QLatin1Char('0'));
            // Prevent double-numbering if the user's yt-dlp output template already includes the playlist index
            if (!finalName.startsWith(QStringLiteral("%1 - ").arg(paddedIndex))) {
                finalName = QStringLiteral("%1 - %2").arg(paddedIndex, finalName);
            }
        }

        const QString destPath = QDir(finalDir).filePath(finalName);

        // Capture temp file info BEFORE it's moved/renamed.
        const QFileInfo tempFileInfo(item.tempFilePath);
        const QDir tempDir = tempFileInfo.absoluteDir();
        const QString cleanBase = cleanupStem(tempFileInfo.fileName());
        const QString mediaInfoJsonPath = tempDir.filePath(QStringLiteral("%1.info.json").arg(cleanBase));

        QDir().mkpath(finalDir); // Ensure destination directory exists without blocking main thread

        bool success = false;
        QString message;
        QString finalPath = destPath;

        bool isFullPlaylistDownload = item.options.value(QStringLiteral("is_full_playlist_download"), false).toBool();
        if (downloadType == QStringLiteral("audio") && isFullPlaylistDownload) {
            // Find the generated folder image. It might not be .jpg if the user selected .png or no conversion
            const QStringList thumbFiles = tempDir.entryList({QStringLiteral("%1_folder.*").arg(id)}, QDir::Files);
            if (!thumbFiles.isEmpty()) {
                const QString thumbTempPath = tempDir.filePath(thumbFiles.first());
                const QFileInfo thumbInfo(thumbTempPath);
                const QString thumbDestPath = QDir(finalDir).filePath(QStringLiteral("folder.%1").arg(thumbInfo.suffix()));
                if (!QFile::exists(thumbDestPath)) {
                    if (!QFile::copy(thumbTempPath, thumbDestPath)) {
                        qWarning() << "Failed to copy playlist folder artwork to:" << thumbDestPath;
                    }
                }
                if (!QFile::remove(thumbTempPath)) {
                    qWarning() << "Failed to remove temp playlist folder artwork:" << thumbTempPath;
                }
                qDebug() << "Moved playlist folder artwork to:" << thumbDestPath;
            }
        }

        if (downloadType == QStringLiteral("gallery")) {
            const QString tempPath = QDir::fromNativeSeparators(item.tempFilePath);
            const QDir galleryTempDir(tempPath);
            if (!galleryTempDir.exists()) {
                message = DownloadFinalizer::tr("Gallery download failed: temp directory missing.");
                QMetaObject::invokeMethod(QCoreApplication::instance(), [self, id, message]() {
                    if (self) emit self->finalizationComplete(id, false, message);
                }, Qt::QueuedConnection);
                return;
            }

            const QFileInfoList entries = galleryTempDir.entryInfoList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Files);
            if (entries.size() == 1) {
                const QFileInfo &entry = entries.first();
                const QString newDestPath = QDir(finalDir).filePath(entry.fileName());
                
                if (entry.isFile()) {
                    if (QFile::exists(newDestPath)) {
                        if (!QFile::remove(newDestPath)) {
                            qWarning() << "Failed to remove existing file before gallery move:" << newDestPath;
                        }
                    }
                    if (QFile::rename(entry.absoluteFilePath(), newDestPath) || (QFile::copy(entry.absoluteFilePath(), newDestPath) && QFile::remove(entry.absoluteFilePath()))) {
                        finalPath = newDestPath;
                        success = true;
                        message = DownloadFinalizer::tr("Gallery download completed → %1").arg(QDir::toNativeSeparators(finalDir));
                    } else {
                        message = DownloadFinalizer::tr("Gallery download completed, but failed to move file to final destination.");
                    }
                } else {
                    QDir srcDir(entry.absoluteFilePath());
                    if (QDir().rename(entry.absoluteFilePath(), newDestPath) || (copyDirectoryRecursivelyInternal(entry.absoluteFilePath(), newDestPath) && srcDir.removeRecursively())) {
                        finalPath = newDestPath;
                        success = true;
                        message = DownloadFinalizer::tr("Gallery download completed → %1").arg(QDir::toNativeSeparators(finalDir));
                    } else {
                        message = DownloadFinalizer::tr("Gallery download completed, but failed to move directory.");
                    }
                }
            } else {
                // The temp directory contains multiple files or directories at its root.
                // Move its contents directly into finalDir rather than keeping the UUID folder.
                bool allMoved = true;
                for (const QFileInfo &entry : entries) {
                    const QString dstPath = QDir(finalDir).filePath(entry.fileName());
                    
                    if (entry.isFile() && QFile::exists(dstPath)) {
                        if (!QFile::remove(dstPath)) {
                            qWarning() << "Failed to remove existing file during gallery move:" << dstPath;
                        }
                    }
                    
                    if (!QDir().rename(entry.absoluteFilePath(), dstPath)) {
                        if (entry.isDir()) {
                            QDir tempDir(entry.absoluteFilePath());
                            if (!copyDirectoryRecursivelyInternal(entry.absoluteFilePath(), dstPath) || !tempDir.removeRecursively()) allMoved = false;
                        } else {
                            if (!QFile::copy(entry.absoluteFilePath(), dstPath) || !QFile::remove(entry.absoluteFilePath())) allMoved = false;
                        }
                    }
                }

                if (allMoved) {
                    finalPath = finalDir;
                    success = true;
                    message = DownloadFinalizer::tr("Gallery download completed → %1").arg(QDir::toNativeSeparators(finalDir));
                } else {
                    message = DownloadFinalizer::tr("Gallery download completed, but failed to move directory contents.");
                }
            }
        } else {
            sendProgress(DownloadFinalizer::tr("Moving to final destination..."));

            if (QFile::exists(destPath) && !safeRemoveWithRetry(destPath)) {
                message = DownloadFinalizer::tr("Download completed, but failed to replace existing file.");
                QMetaObject::invokeMethod(QCoreApplication::instance(), [self, id, message]() {
                    if (self) emit self->finalizationComplete(id, false, message);
                }, Qt::QueuedConnection);
                return;
            }

            bool moved = QFile::rename(item.tempFilePath, destPath);
            if (!moved) {
                sendProgress(DownloadFinalizer::tr("Copying file to destination..."));
                if ((moved = QFile::copy(item.tempFilePath, destPath))) {
                    if (!safeRemoveWithRetry(item.tempFilePath)) {
                        qWarning() << "Failed to remove temp file after copy:" << item.tempFilePath;
                    }
                }
            }

            if (moved) {
                success = true;
                finalPath = destPath;
                message = DownloadFinalizer::tr("Download completed → %1").arg(QDir::toNativeSeparators(finalDir));
                if (!item.originalDownloadedFilePath.isEmpty() && item.originalDownloadedFilePath != item.tempFilePath) {
                    if (!safeRemoveWithRetry(item.originalDownloadedFilePath)) {
                        qWarning() << "Failed to remove original downloaded file:" << item.originalDownloadedFilePath;
                    }
                }
            } else {
                message = DownloadFinalizer::tr("Download completed, but failed to move file.");
            }
        }

        // Cleanup must happen after all signals are emitted and operations are complete.
        cleanupTempFiles(item, tempDir, mediaInfoJsonPath);

        // Because yt-dlp downloads are now isolated into their own UUID subfolders
        // to prevent naming collisions, we must delete the UUID folder afterward.
        if (downloadType != QStringLiteral("gallery")) {
            if (tempDir.dirName() == id) {
                QDir dirToRemove(tempDir);
                dirToRemove.removeRecursively();
            }
        } else {
            QDir galleryTempDir(item.tempFilePath);
            if (galleryTempDir.exists() && galleryTempDir.dirName() == id) {
                galleryTempDir.removeRecursively();
            }
        }

        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, id, success, message, finalPath, archiveManager, url = item.url]() {
            // Execute archive insertion on the main thread to prevent leaking thread-local SQLite connections
            // from one-off QThread::create workers.
            if (success && archiveManager) {
                archiveManager->addToArchive(url);
            }

            if (!self) return;
            if (success) {
                emit self->finalPathReady(id, finalPath);
                emit self->finalizationComplete(id, true, message);
            } else {
                emit self->finalizationComplete(id, false, message);
            }
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->setObjectName(QStringLiteral("FinalizerThread"));
    thread->start();
}
