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
        static const QSet<QString> knownExts = {
            QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("webm"), QStringLiteral("m4a"), QStringLiteral("mp3"), QStringLiteral("opus"), QStringLiteral("ogg"), QStringLiteral("ts"),
            QStringLiteral("mov"), QStringLiteral("avi"), QStringLiteral("flac"), QStringLiteral("wav"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("webp"),
            QStringLiteral("srt"), QStringLiteral("vtt"), QStringLiteral("ass"), QStringLiteral("lrc")
        };
        const QString ext = QFileInfo(fileName).suffix().toLower();
        if (knownExts.contains(ext)) {
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

void cleanupTempFiles(const DownloadItem &item, const QDir &tempDir, const QString &mediaInfoJsonPath)
{
    if (item.options.value(QStringLiteral("type")).toString() == QStringLiteral("gallery")) {
        return;
    }

    // 1. Clean up the info.json that matches the media file name.
    QFile::remove(mediaInfoJsonPath);
    qDebug() << "Attempted to clean up media info.json:" << mediaInfoJsonPath;

    // 2. Clean up any explicitly tracked temporary files (like _wait_thumbnail.jpg)
    QStringList candidates = item.options.value(QStringLiteral("cleanup_candidates")).toStringList();
    for (const QString &candidate : candidates) {
        QFileInfo candidateInfo(candidate);
        if (candidateInfo.exists() && candidateInfo.isFile()) {
            if (QFile::remove(candidate)) {
                qDebug() << "Cleaned up tracked candidate file:" << candidate;
            }
        }
    }

    // 3. Clean up leftover auxiliary files (like un-embedded .jpg thumbnails) matching the video basename.
    // Avoid QDir wildcard filters here: raw titles can contain '[' and ']', which are special in
    // Qt's wildcard-to-regex conversion and can produce invalid QRegularExpression warnings.
    QFileInfo tempFileInfo(item.tempFilePath);
    const QString baseName = cleanupStem(tempFileInfo.fileName());
    if (!baseName.isEmpty()) {
        const QString tempFilePath = QFileInfo(item.tempFilePath).absoluteFilePath();
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
                QFile::remove(filePath);
            }
        }
    }

    // 4. If it was a playlist download, find and remove the playlist's info.json file.
    QString playlistId;

    // Strategy 1: Get playlist_id from the original playlist URL stored in options. This is the most reliable.
    if (item.options.contains(QStringLiteral("original_playlist_url"))) {
        QUrl url(item.options.value(QStringLiteral("original_playlist_url")).toString());
        if (url.hasQuery()) {
            QUrlQuery query(url);
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
        QStringList potentialFiles = tempDir.entryList(QStringList{QStringLiteral("*.info.json")}, QDir::Files);
        for (const QString &fileName : potentialFiles) {
            // Check if the filename contains the playlist ID, typically formatted as "[<playlist_id>]"
            // by yt-dlp's default playlist output template. This is more reliable than parsing JSON content.
            if (fileName.contains(QStringLiteral("[%1]").arg(playlistId))) {
                QString filePath = tempDir.filePath(fileName);
                if (QFile::remove(filePath)) {
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
    QDir source(sourceDir);
    if (!source.exists()) {
        qWarning() << "copyDirectoryRecursively: source does not exist:" << sourceDir;
        return false;
    }
    QDir dest(destDir);
    if (!dest.exists()) {
        if (!dest.mkpath(QStringLiteral("."))) {
            qWarning() << "copyDirectoryRecursively: failed to create dest dir:" << destDir;
            return false;
        }
    }
    bool success = true;
    QFileInfoList entries = source.entryInfoList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Files);
    for (const QFileInfo &entry : entries) {
        QString srcPath = entry.absoluteFilePath();
        QString dstPath = dest.absoluteFilePath(entry.fileName());
        if (entry.isDir()) {
            success &= copyDirectoryRecursivelyInternal(srcPath, dstPath);
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

    if (item.options.value(QStringLiteral("type")).toString() != QStringLiteral("gallery") && !item.metadata.contains(QStringLiteral("id"))) {
        qWarning() << "Metadata is missing core fields in finalize for id:" << id << ", attempting to read from disk.";
        QFileInfo fi(item.tempFilePath);
        QString jsonPath = fi.absoluteDir().filePath(QStringLiteral("%1.info.json").arg(fi.completeBaseName()));

        QFile jsonFile(jsonPath);
        if (jsonFile.open(QIODevice::ReadOnly)) {
            QByteArray jsonData = jsonFile.readAll();
            jsonFile.close();
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                QVariantMap diskMetadata = doc.object().toVariantMap();
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
    if (item.options.value(QStringLiteral("type")).toString() == QStringLiteral("gallery") && !item.metadata.contains(QStringLiteral("id"))) {
        QDir galleryTempDir(QDir::fromNativeSeparators(item.tempFilePath));
        if (galleryTempDir.exists()) {
            QFileInfoList entries = galleryTempDir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files);
            if (!entries.isEmpty()) {
                QString firstFile = entries.first().fileName();
                // Extract typical gallery-dl ID patterns (e.g., tiktok_7646992089099586847_...)
                static const QRegularExpression idRe(QStringLiteral(R"(_(\d{10,25})_)"));
                QRegularExpressionMatch match = idRe.match(firstFile);
                if (match.hasMatch()) {
                    item.metadata[QStringLiteral("id")] = match.captured(1);
                    qDebug() << "Extracted real gallery ID for sorting:" << match.captured(1);
                }
            }
        }
    }

    QFileInfo fileInfo(item.tempFilePath);
    if (!fileInfo.exists()) {
        emit finalizationComplete(id, false, tr("Downloaded file not found."));
        return;
    }

    if (item.playlistIndex != -1 && !item.metadata.contains(QStringLiteral("playlist_index"))) {
        item.metadata[QStringLiteral("playlist_index")] = item.playlistIndex;
    }

    emit progressUpdated(id, {{QStringLiteral("status"), tr("Verifying download completeness...")}});

    emit progressUpdated(id, {{QStringLiteral("status"), tr("Applying sorting rules...")}});

    QString finalDir = m_sortingManager->getSortedDirectory(item.metadata, item.options);
    QDir().mkpath(finalDir);
    finalDir = QDir(finalDir).absolutePath();
    QString finalName = fileInfo.fileName();

    // Default to true for audio to maintain legacy music sorting, but allow users to toggle it for all types
    bool isAudio = item.options.value(QStringLiteral("type")).toString() == QStringLiteral("audio");
    bool autoNumber = m_configManager->get(QStringLiteral("DownloadOptions"), QStringLiteral("prefix_playlist_indices"), isAudio).toBool();

    if (autoNumber && item.playlistIndex > 0) {
        QString paddedIndex = QString::number(item.playlistIndex).rightJustified(2, QLatin1Char('0'));
        // Prevent double-numbering if the user's yt-dlp output template already includes the playlist index
        if (!finalName.startsWith(QStringLiteral("%1 - ").arg(paddedIndex))) {
            finalName = QStringLiteral("%1 - %2").arg(paddedIndex, finalName);
        }
    }

    QString destPath = QDir(finalDir).filePath(finalName);

    // Capture temp file info BEFORE it's moved/renamed.
    QFileInfo tempFileInfo(item.tempFilePath);
    QDir tempDir = tempFileInfo.absoluteDir();
    QString mediaInfoJsonPath = tempDir.filePath(QStringLiteral("%1.info.json").arg(tempFileInfo.completeBaseName()));

    QPointer<DownloadFinalizer> self(this);
    QThread *thread = QThread::create([self, id, item, finalDir, destPath, tempDir, mediaInfoJsonPath]() {
        auto sendProgress = [self, id](const QString &statusMsg) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [self, id, statusMsg]() {
                if (self) emit self->progressUpdated(id, {{QStringLiteral("status"), statusMsg}});
            }, Qt::QueuedConnection);
        };

        bool success = false;
        QString message;
        QString finalPath = destPath;

        if (item.options.value(QStringLiteral("type")).toString() == QStringLiteral("audio") && item.playlistIndex > 0) {
            // Find the generated folder image. It might not be .jpg if the user selected .png or no conversion
            QStringList filters;
            filters << QStringLiteral("%1_folder.*").arg(id);
            QStringList thumbFiles = tempDir.entryList(filters, QDir::Files);
            if (!thumbFiles.isEmpty()) {
                QString thumbTempPath = tempDir.filePath(thumbFiles.first());
                QFileInfo thumbInfo(thumbTempPath);
                QString thumbDestPath = QDir(finalDir).filePath(QStringLiteral("folder.%1").arg(thumbInfo.suffix()));
                if (!QFile::exists(thumbDestPath)) {
                    QFile::copy(thumbTempPath, thumbDestPath);
                }
                QFile::remove(thumbTempPath);
                qDebug() << "Moved playlist folder artwork to:" << thumbDestPath;
            }
        }

        if (item.options.value(QStringLiteral("type")).toString() == QStringLiteral("gallery")) {
            QString tempPath = QDir::fromNativeSeparators(item.tempFilePath);
            QDir galleryTempDir(tempPath);
            if (!galleryTempDir.exists()) {
                message = DownloadFinalizer::tr("Gallery download failed: temp directory missing.");
            QMetaObject::invokeMethod(QCoreApplication::instance(), [self, id, message]() {
                if (self) emit self->finalizationComplete(id, false, message);
            }, Qt::QueuedConnection);
                return;
            }

            QFileInfoList entries = galleryTempDir.entryInfoList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Files);
            if (entries.size() == 1) {
                QFileInfo entry = entries.first();
                QString newDestPath = QDir(finalDir).filePath(entry.fileName());
                
                if (entry.isFile()) {
                    if (QFile::rename(entry.absoluteFilePath(), newDestPath) || (QFile::copy(entry.absoluteFilePath(), newDestPath) && QFile::remove(entry.absoluteFilePath()))) {
                        finalPath = newDestPath;
                        success = true;
                        message = DownloadFinalizer::tr("Gallery download completed → %1").arg(QDir::toNativeSeparators(finalDir));
                    } else {
                        message = DownloadFinalizer::tr("Gallery download completed, but failed to move file to final destination.");
                    }
                } else {
                    if (QDir().rename(entry.absoluteFilePath(), newDestPath) || (copyDirectoryRecursivelyInternal(entry.absoluteFilePath(), newDestPath) && QDir(entry.absoluteFilePath()).removeRecursively())) {
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
                QDir().mkpath(finalDir);
                for (const QFileInfo &entry : entries) {
                    QString dstPath = QDir(finalDir).filePath(entry.fileName());
                    
                    if (entry.isFile() && QFile::exists(dstPath)) {
                        QFile::remove(dstPath);
                    }
                    
                    if (!QDir().rename(entry.absoluteFilePath(), dstPath)) {
                        if (entry.isDir()) {
                            if (!copyDirectoryRecursivelyInternal(entry.absoluteFilePath(), dstPath) || !QDir(entry.absoluteFilePath()).removeRecursively()) allMoved = false;
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

            if (QFile::exists(destPath) && !QFile::remove(destPath)) {
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
                    QFile::remove(item.tempFilePath);
                }
            }

            if (moved) {
                success = true;
                finalPath = destPath;
                message = DownloadFinalizer::tr("Download completed → %1").arg(QDir::toNativeSeparators(finalDir));
                if (!item.originalDownloadedFilePath.isEmpty() && item.originalDownloadedFilePath != item.tempFilePath) {
                    QFile::remove(item.originalDownloadedFilePath);
                }
            } else {
                message = DownloadFinalizer::tr("Download completed, but failed to move file.");
            }
        }

        // Cleanup must happen after all signals are emitted and operations are complete.
        cleanupTempFiles(item, tempDir, mediaInfoJsonPath);

        // Because yt-dlp downloads are now isolated into their own UUID subfolders
        // to prevent naming collisions, we must delete the UUID folder afterward.
        if (item.options.value(QStringLiteral("type")).toString() != QStringLiteral("gallery")) {
            if (tempDir.dirName() == id) {
                QDir(tempDir).removeRecursively();
            }
        } else {
            QDir galleryTempDir(item.tempFilePath);
            if (galleryTempDir.exists() && galleryTempDir.dirName() == id) {
                galleryTempDir.removeRecursively();
            }
        }

        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, id, item, success, message, finalPath]() {
            if (!self) return;
            if (success) {
                if (self->m_archiveManager) {
                    self->m_archiveManager->addToArchive(item.url);
                }
                emit self->finalPathReady(id, finalPath);
                emit self->finalizationComplete(id, true, message);
            } else {
                emit self->finalizationComplete(id, false, message);
            }
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
