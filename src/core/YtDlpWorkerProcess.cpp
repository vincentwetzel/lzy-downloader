#include "YtDlpWorker.h"

#include "ConfigManager.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>
#include <QVariantList>
#include <chrono>

void YtDlpWorker::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (m_finishEmitted) {
        return;
    }

    // Process any remaining output. This is crucial for capturing the final file path.
    parseStandardOutput(m_process->readAllStandardOutput());
    // Also process any remaining stderr output
    parseStandardError(m_process->readAllStandardError());

    if (!m_outputBuffer.isEmpty()) {
        handleOutputLine(QString::fromUtf8(m_outputBuffer).trimmed()); // Trim any remaining buffer content
        m_outputBuffer.clear();
    }
    if (!m_errorBuffer.isEmpty()) {
        handleOutputLine(QString::fromUtf8(m_errorBuffer).trimmed());
        m_errorBuffer.clear();
    }


    m_finishEmitted = true;
    const bool normalExit = (exitStatus == QProcess::NormalExit);
    const bool capturedFinalFileExists = !m_finalFilename.isEmpty() && QFileInfo::exists(m_finalFilename);
    const bool recoveredFromPostProcessorFailure = normalExit && exitCode != 0 && capturedFinalFileExists;
    bool success = (normalExit && exitCode == 0 && !m_finalFilename.isEmpty()) || recoveredFromPostProcessorFailure;
    QString message = success ? tr("Download completed successfully.") : tr("Download failed.");
    QString postprocessorWarning;

    // Check if we are waiting for a user prompt (scheduled livestream)
    if (!success && m_errorEmitted && !property("promptDelayActive").toBool()) {
        QString errorStr = m_errorLines.join(QStringLiteral(" "));
        if (errorStr.contains(QStringLiteral("Premieres in"), Qt::CaseInsensitive) ||
            errorStr.contains(QStringLiteral("Premiering in"), Qt::CaseInsensitive) ||
            errorStr.contains(QStringLiteral("Premiere will begin"), Qt::CaseInsensitive) ||
            errorStr.contains(QStringLiteral("live event will begin"), Qt::CaseInsensitive) ||
            errorStr.contains(QStringLiteral("is upcoming"), Qt::CaseInsensitive) ||
            errorStr.contains(QStringLiteral("Offline (expected)"), Qt::CaseInsensitive) ||
            errorStr.contains(QStringLiteral("Offline expected"), Qt::CaseInsensitive) ||
            errorStr.contains(QStringLiteral("waiting for premiere"), Qt::CaseInsensitive) ||
            errorStr.contains(QStringLiteral("waiting for livestream"), Qt::CaseInsensitive) ||
            errorStr.contains(QStringLiteral("Live in "), Qt::CaseInsensitive) ||
            errorStr.contains(QStringLiteral("Starting in "), Qt::CaseInsensitive)) {
            
            setProperty("promptDelayActive", true);
            qDebug() << "[YtDlpWorker] Delaying finished signal to wait for user prompt response.";
            
            QVariantMap progressData;
            progressData[QStringLiteral("status")] = tr("Waiting for user response...");
            progressData[QStringLiteral("progress")] = -1;
            if (!m_videoTitle.isEmpty()) {
                progressData[QStringLiteral("title")] = m_videoTitle;
            }
            if (!m_thumbnailPath.isEmpty()) {
                progressData[QStringLiteral("thumbnail_path")] = m_thumbnailPath;
            }
            emit progressUpdated(m_id, progressData);

            QTimer::singleShot(std::chrono::minutes(5), this, [this, exitCode, exitStatus]() {
                qDebug() << "[YtDlpWorker] User prompt timeout reached. Emitting finished signal.";
                m_finishEmitted = false; // Reset so onProcessFinished actually executes
                onProcessFinished(exitCode, exitStatus);
            });
            return;
        }
    }

    if (recoveredFromPostProcessorFailure) {
        message = tr("Download completed, but thumbnail/post-processing reported a warning.");
        if (!m_errorLines.isEmpty()) {
            message = QStringLiteral("%1\n%2").arg(message, m_errorLines.join(QStringLiteral("\n")).left(200));
        }
        postprocessorWarning = message;
        qWarning() << "yt-dlp exited with code" << exitCode
                   << "after producing final media. Continuing finalization for"
                   << m_id << "at" << m_finalFilename;
    }

    if (!m_finalFilename.isEmpty()) {
        qDebug() << "Final filename captured:" << m_finalFilename;
    } else {
        qWarning() << "Could not determine final filename. Download may have failed or produced no output.";
        if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
            message = QStringLiteral("%1\n%2").arg(message, tr("Could not determine final filename."));
        }
    }

    QVariantMap metadata;
    if (success) {
        // Move wait thumbnail inside UUID folder so DownloadFinalizer cleans it up automatically
        if (!m_thumbnailPath.isEmpty() && QFileInfo(m_thumbnailPath).fileName().startsWith(QStringLiteral("%1_wait_thumbnail").arg(m_id)) && m_configManager) {
            QString tempDir = m_configManager->get(QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory")).toString();
            if (tempDir.isEmpty()) {
                QString completedDir = m_configManager->get(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory")).toString();
                if (!completedDir.isEmpty()) {
                    tempDir = QDir(completedDir).filePath(QStringLiteral("temp_downloads"));
                }
            }
            if (!tempDir.isEmpty()) {
                QString uuidDirPath = QDir(tempDir).filePath(m_id);
                QString newThumbPath = QDir(uuidDirPath).filePath(QFileInfo(m_thumbnailPath).fileName());
                
                QDir().mkpath(uuidDirPath);
                if (QFile::rename(m_thumbnailPath, newThumbPath) || (QFile::copy(m_thumbnailPath, newThumbPath) && QFile::remove(m_thumbnailPath))) {
                    m_thumbnailPath = newThumbPath;
                    qDebug() << "Moved wait thumbnail into UUID directory for automatic cleanup:" << m_thumbnailPath;
                } else {
                    qWarning() << "Failed to move wait thumbnail to UUID directory:" << m_thumbnailPath;
                }
            }
        }

        // Ensure metadata is loaded if it hasn't been asynchronously parsed yet
        if (m_fullMetadata.isEmpty() && !m_infoJsonPath.isEmpty() && QFile::exists(m_infoJsonPath)) {
            QFile jsonFile(m_infoJsonPath);
            if (jsonFile.open(QIODevice::ReadOnly)) {
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(jsonFile.readAll(), &parseError);
                if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                    m_fullMetadata = doc.object().toVariantMap();
                } else {
                    qWarning() << "Failed to parse info.json in onProcessFinished:" << parseError.errorString();
                }
            }
        }

        // Clean up info.json now that metadata is loaded, so the UUID temp folder can be removed
        if (!m_infoJsonPath.isEmpty() && QFile::exists(m_infoJsonPath)) {
            QFile::remove(m_infoJsonPath);
            qDebug() << "Cleaned up info.json file:" << m_infoJsonPath;
        }

        // Use the full metadata that was already parsed from info.json during readInfoJsonWithRetry
        if (!m_fullMetadata.isEmpty()) {
            metadata = m_fullMetadata;
            qDebug() << "onProcessFinished: Using cached metadata with" << metadata.size() << "keys. Keys:" << metadata.keys();
            // Ensure m_videoTitle is consistent with what's in metadata
            if (metadata.contains(QStringLiteral("title")) && m_videoTitle.isEmpty()) {
                m_videoTitle = metadata[QStringLiteral("title")].toString();
            }
            if (metadata.contains(QStringLiteral("uploader"))) {
                qDebug() << "onProcessFinished: uploader from metadata:" << metadata[QStringLiteral("uploader")].toString();
            } else {
                qWarning() << "onProcessFinished: uploader NOT found in cached metadata!";
            }
        } else {
            qWarning() << "onProcessFinished: No cached metadata available. Sorting rules may not work.";
        }

        // CRITICAL FIX: Emit 100% progress update before finished signal to ensure
        // the UI progress bar reaches 100% and turns green, not stuck at <100%
        QVariantMap finalProgressData;
        finalProgressData[QStringLiteral("progress")] = 100;
        finalProgressData[QStringLiteral("status")] = tr("Complete");
        if (!m_videoTitle.isEmpty()) {
            finalProgressData[QStringLiteral("title")] = m_videoTitle;
        }
        if (!m_thumbnailPath.isEmpty()) {
            finalProgressData[QStringLiteral("thumbnail_path")] = m_thumbnailPath;
        }
        emit progressUpdated(m_id, finalProgressData);
    }

    if (!success) {
        if (!m_errorLines.isEmpty()) {
            message = QStringLiteral("%1\n%2").arg(message, m_errorLines.join(QStringLiteral("\n")).left(200));
        } else {
            // Fallback: Use the last few lines of general output if no ERROR: lines were captured
            if (!m_allOutputLines.isEmpty()) {
                message = QStringLiteral("%1\n%2").arg(message, m_allOutputLines.mid(qMax(0, m_allOutputLines.size() - 5)).join(QStringLiteral("\n")).left(200));
            }
        }
        
        // Clean up orphaned wait thumbnail on failure
        if (!m_thumbnailPath.isEmpty() && QFileInfo(m_thumbnailPath).fileName().startsWith(QStringLiteral("%1_wait_thumbnail").arg(m_id))) {
            QFile::remove(m_thumbnailPath);
            m_thumbnailPath.clear();
        }
    }

    // Try to clean up empty UUID directory if yt-dlp failed before writing anything,
    // or if the process completed but left the directory empty (e.g. skipped downloads).
    if (m_configManager) {
        QString tempDir = m_configManager->get(QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory")).toString();
        if (tempDir.isEmpty()) {
            QString completedDir = m_configManager->get(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory")).toString();
            if (!completedDir.isEmpty()) {
                tempDir = QDir(completedDir).filePath(QStringLiteral("temp_downloads"));
            }
        }
        if (!tempDir.isEmpty()) {
            QString uuidDirPath = QDir(tempDir).filePath(m_id);
            QDir().rmdir(uuidDirPath); // Only succeeds if the directory is empty
        }
    }

    // Ensure m_videoTitle is included in the metadata for the finished signal
    if (!m_videoTitle.isEmpty() && !metadata.contains(QStringLiteral("title"))) {
        metadata[QStringLiteral("title")] = m_videoTitle;
    }
    if (!m_thumbnailPath.isEmpty() && !metadata.contains(QStringLiteral("thumbnail_path"))) {
        metadata[QStringLiteral("thumbnail_path")] = m_thumbnailPath;
    }
    if (!postprocessorWarning.isEmpty()) {
        metadata[QStringLiteral("postprocessor_warning")] = postprocessorWarning;
    }

    emit finished(m_id, success, message, m_finalFilename, m_originalDownloadedFilename, metadata);
}


void YtDlpWorker::onProcessError(QProcess::ProcessError error) {
    if (m_finishEmitted) {
        return;
    }

    if (error == QProcess::FailedToStart || error == QProcess::Crashed ||
        error == QProcess::ReadError || error == QProcess::WriteError) {
        m_finishEmitted = true;
        const QString message = tr("Download failed.\nFailed to start yt-dlp process (%1): %2")
                                    .arg(static_cast<int>(error))
                                    .arg(m_process->errorString());
        qWarning() << message;
        emit finished(m_id, false, message, QString(), QString(), QVariantMap());
    }
}

void YtDlpWorker::onReadyReadStandardOutput() {
    QByteArray data = m_process->readAllStandardOutput();
    qDebug() << "[STDOUT] Received" << data.size() << "bytes:" << QString::fromUtf8(data).left(300);
    parseStandardOutput(data);
}

void YtDlpWorker::onReadyReadStandardError() {
    QByteArray data = m_process->readAllStandardError();
    qDebug() << "[STDERR] Received" << data.size() << "bytes:" << QString::fromUtf8(data).left(300);
    parseStandardError(data);
}

void YtDlpWorker::parseStandardOutput(const QByteArray &output) {
    m_outputBuffer.append(output);

    qsizetype lastDelimiter = qMax(m_outputBuffer.lastIndexOf('\n'), m_outputBuffer.lastIndexOf('\r'));
    if (lastDelimiter == -1) return;

    static const QRegularExpression newlineRegex(QStringLiteral("[\\r\\n]"));
    QStringList lines = QString::fromUtf8(m_outputBuffer.left(lastDelimiter + 1)).split(newlineRegex, Qt::SkipEmptyParts);
    m_outputBuffer.remove(0, lastDelimiter + 1);

    for (const QString &line : lines) {
        handleOutputLine(line.trimmed());
    }
}

void YtDlpWorker::parseStandardError(const QByteArray &output) {
    m_errorBuffer.append(output);
    qDebug() << "parseStandardError called. Current buffer size:" << m_errorBuffer.size();

    qsizetype lastDelimiter = qMax(m_errorBuffer.lastIndexOf('\n'), m_errorBuffer.lastIndexOf('\r'));
    if (lastDelimiter == -1) return;

    static const QRegularExpression newlineRegex(QStringLiteral("[\\r\\n]"));
    QStringList lines = QString::fromUtf8(m_errorBuffer.left(lastDelimiter + 1)).split(newlineRegex, Qt::SkipEmptyParts);
    m_errorBuffer.remove(0, lastDelimiter + 1);

    for (const QString &line : lines) {
        qDebug() << "Processing stderr line:" << line.trimmed();
        handleOutputLine(line.trimmed());
    }
}

void YtDlpWorker::readInfoJsonWithRetry() {
    qDebug() << "readInfoJsonWithRetry: Attempting to read info.json. Path:" << m_infoJsonPath << "Retry:" << m_infoJsonRetryCount;

    if (m_infoJsonPath.isEmpty()) {
        qDebug() << "readInfoJsonWithRetry: No info.json path set.";
        return;
    }

    auto scheduleRetry = [this](const QString& reason) {
        qWarning().noquote() << "readInfoJsonWithRetry:" << reason;
        if (m_infoJsonRetryCount < 5) {
            m_infoJsonRetryCount++;
            QTimer::singleShot(std::chrono::milliseconds(500), this, &YtDlpWorker::readInfoJsonWithRetry);
            qDebug() << "readInfoJsonWithRetry: Retrying in 500ms. Attempt:" << m_infoJsonRetryCount;
        } else {
            qWarning() << "readInfoJsonWithRetry: Max retries reached for info.json.";
            m_infoJsonPath.clear(); // Give up
        }
    };

    QFile jsonFile(m_infoJsonPath);
    if (!jsonFile.exists()) {
        scheduleRetry(QStringLiteral("info.json file does not exist yet at: %1").arg(m_infoJsonPath));
        return;
    }

    if (!jsonFile.open(QIODevice::ReadOnly)) {
        scheduleRetry(QStringLiteral("Could not open info.json file at: %1 Error: %2").arg(m_infoJsonPath, jsonFile.errorString()));
        return;
    }

    QByteArray jsonData = jsonFile.readAll();
    qDebug() << "readInfoJsonWithRetry: Successfully opened and read info.json. Data size:" << jsonData.size();
    jsonFile.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        scheduleRetry(QStringLiteral("Failed to parse info.json as JSON or it's not an object. Error: %1").arg(parseError.errorString()));
        return;
    }

    // If we successfully parsed the file, we don't need to retry anymore.
    m_infoJsonRetryCount = 0;

    QJsonObject obj = doc.object();
    QVariantMap updateData;

    // Store the full metadata for use in onProcessFinished.
    // Important: only replace inferred transfer ordering when info.json actually
    // provides requested_downloads. Some yt-dlp runs omit that field entirely,
    // and clearing our earlier stderr-derived mapping causes audio handoff labels
    // to briefly switch correctly and then regress back to "video".
    m_fullMetadata = obj.toVariantMap();
    const QVariantList requestedDownloads = m_fullMetadata.value(QStringLiteral("requested_downloads")).toList();
    if (!requestedDownloads.isEmpty()) {
        m_requestedTransferStatuses.clear();
        m_requestedTransferFormatIds.clear();
        m_requestedTransferSizes.clear();
    }
    for (const QVariant &requestedDownload : requestedDownloads) {
        const QVariantMap requestMap = requestedDownload.toMap();
        const QString vcodec = requestMap.value(QStringLiteral("vcodec")).toString();
        const QString acodec = requestMap.value(QStringLiteral("acodec")).toString();
        const QString formatId = requestMap.value(QStringLiteral("format_id")).toString().trimmed();
        if (!vcodec.isEmpty() && vcodec != QStringLiteral("none") && (acodec.isEmpty() || acodec == QStringLiteral("none"))) {
            m_requestedTransferStatuses.append(tr("Downloading video stream..."));
            m_requestedTransferFormatIds.append(formatId);
            m_requestedTransferSizes.append(inferPrimaryStreamSizeBytes(requestMap));
        } else if (!acodec.isEmpty() && acodec != QStringLiteral("none") && (vcodec.isEmpty() || vcodec == QStringLiteral("none"))) {
            m_requestedTransferStatuses.append(tr("Downloading audio stream..."));
            m_requestedTransferFormatIds.append(formatId);
            m_requestedTransferSizes.append(inferPrimaryStreamSizeBytes(requestMap));
        } else if ((!vcodec.isEmpty() && vcodec != QStringLiteral("none")) || (!acodec.isEmpty() && acodec != QStringLiteral("none"))) {
            m_requestedTransferStatuses.append(tr("Downloading media stream..."));
            m_requestedTransferFormatIds.append(formatId);
            m_requestedTransferSizes.append(inferPrimaryStreamSizeBytes(requestMap));
        }
    }
    if (requestedDownloads.isEmpty()) {
        qDebug() << "[YtDlpWorker] info.json did not provide requested_downloads; preserving previously inferred transfer order:"
                 << m_requestedTransferFormatIds << m_requestedTransferStatuses;
    }
    qDebug() << "[YtDlpWorker] requested transfer statuses:" << m_requestedTransferStatuses;
    qDebug() << "[YtDlpWorker] requested transfer format IDs:" << m_requestedTransferFormatIds;
    qDebug() << "[YtDlpWorker] requested transfer sizes:" << m_requestedTransferSizes;

    if (m_videoTitle.isEmpty() && obj.contains(QStringLiteral("title")) && obj[QStringLiteral("title")].isString()) {
        m_videoTitle = obj[QStringLiteral("title")].toString();
        updateData[QStringLiteral("title")] = m_videoTitle;
        qDebug() << "Extracted title from info.json:" << m_videoTitle;
    }
    
    // Extract thumbnail path if available from the info.json
    bool hasWaitThumbnail = !m_thumbnailPath.isEmpty() && QFileInfo(m_thumbnailPath).fileName().startsWith(QStringLiteral("%1_wait_thumbnail").arg(m_id));
    if ((m_thumbnailPath.isEmpty() || hasWaitThumbnail) && obj.contains(QStringLiteral("thumbnails")) && obj[QStringLiteral("thumbnails")].isArray()) {
        QJsonArray thumbnails = obj[QStringLiteral("thumbnails")].toArray();
        // yt-dlp adds a "filepath" key to the thumbnail entry it downloaded.
        for (const QJsonValue &thumbValue : thumbnails) {
            QJsonObject thumbObj = thumbValue.toObject();
            if (thumbObj.contains(QStringLiteral("filepath")) && thumbObj[QStringLiteral("filepath")].isString()) {
                QString newThumb = QDir::toNativeSeparators(thumbObj[QStringLiteral("filepath")].toString());
                if (hasWaitThumbnail && newThumb != m_thumbnailPath) {
                    QFile::remove(m_thumbnailPath); // Clean up the wait thumbnail since we found a real one
                }
                m_thumbnailPath = newThumb;
                updateData[QStringLiteral("thumbnail_path")] = m_thumbnailPath;
                qDebug() << "Extracted thumbnail path from info.json:" << m_thumbnailPath;
                break; // Found it
            }
        }
    }

    if (!updateData.isEmpty()) {
        emit progressUpdated(m_id, updateData);
    }
}
