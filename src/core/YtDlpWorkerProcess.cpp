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
    QString message = success ? "Download completed successfully." : "Download failed.";
    QString postprocessorWarning;

    if (recoveredFromPostProcessorFailure) {
        message = "Download completed, but thumbnail/post-processing reported a warning.";
        if (!m_errorLines.isEmpty()) {
            message += "\n" + m_errorLines.join("\n").left(200);
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
            message += "\nCould not determine final filename.";
        }
    }

    QVariantMap metadata;
    if (success) {
        // Move wait thumbnail inside UUID folder so DownloadFinalizer cleans it up automatically
        if (!m_thumbnailPath.isEmpty() && m_thumbnailPath.endsWith("_wait_thumbnail.jpg")) {
            QString tempDir = m_configManager->get("Paths", "temporary_downloads_directory").toString();
            QString uuidDirPath = QDir(tempDir).filePath(m_id);
            QString newThumbPath = QDir(uuidDirPath).filePath(QFileInfo(m_thumbnailPath).fileName());
            
            QDir().mkpath(uuidDirPath); // Ensure UUID dir exists
            if (QFile::rename(m_thumbnailPath, newThumbPath)) {
                m_thumbnailPath = newThumbPath;
                qDebug() << "Moved wait thumbnail into UUID directory for automatic cleanup:" << m_thumbnailPath;
            }
        }

        // Ensure metadata is loaded if it hasn't been asynchronously parsed yet
        if (m_fullMetadata.isEmpty() && !m_infoJsonPath.isEmpty() && QFile::exists(m_infoJsonPath)) {
            QFile jsonFile(m_infoJsonPath);
            if (jsonFile.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(jsonFile.readAll());
                if (doc.isObject()) {
                    m_fullMetadata = doc.object().toVariantMap();
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
            if (metadata.contains("title") && m_videoTitle.isEmpty()) {
                m_videoTitle = metadata["title"].toString();
            }
            if (metadata.contains("uploader")) {
                qDebug() << "onProcessFinished: uploader from metadata:" << metadata["uploader"].toString();
            } else {
                qWarning() << "onProcessFinished: uploader NOT found in cached metadata!";
            }
        } else {
            qWarning() << "onProcessFinished: No cached metadata available. Sorting rules may not work.";
        }

        // CRITICAL FIX: Emit 100% progress update before finished signal to ensure
        // the UI progress bar reaches 100% and turns green, not stuck at <100%
        QVariantMap finalProgressData;
        finalProgressData["progress"] = 100;
        finalProgressData["status"] = "Complete";
        if (!m_videoTitle.isEmpty()) {
            finalProgressData["title"] = m_videoTitle;
        }
        if (!m_thumbnailPath.isEmpty()) {
            finalProgressData["thumbnail_path"] = m_thumbnailPath;
        }
        emit progressUpdated(m_id, finalProgressData);
    }

    if (!success) {
        if (!m_errorLines.isEmpty()) {
            message += "\n" + m_errorLines.join("\n").left(200);
        } else {
            // Fallback: Ensure stderr is also read as UTF-8
            QString errorOutput = QString::fromUtf8(m_process->readAllStandardError());
            if (!errorOutput.isEmpty()) {
                message += "\n" + errorOutput.left(200);
            }
        }
        
        // Clean up orphaned wait thumbnail on failure
        if (!m_thumbnailPath.isEmpty() && m_thumbnailPath.endsWith("_wait_thumbnail.jpg")) {
            QFile::remove(m_thumbnailPath);
            m_thumbnailPath.clear();
        }
        
        // Try to clean up empty UUID directory if yt-dlp failed before writing anything
        if (m_configManager) {
            QString tempDir = m_configManager->get("Paths", "temporary_downloads_directory").toString();
            QString uuidDirPath = QDir(tempDir).filePath(m_id);
            QDir().rmdir(uuidDirPath); // Only succeeds if the directory is empty
        }
    }

    // Ensure m_videoTitle is included in the metadata for the finished signal
    if (!m_videoTitle.isEmpty() && !metadata.contains("title")) {
        metadata["title"] = m_videoTitle;
    }
    if (!m_thumbnailPath.isEmpty() && !metadata.contains("thumbnail_path")) {
        metadata["thumbnail_path"] = m_thumbnailPath;
    }
    if (!postprocessorWarning.isEmpty()) {
        metadata["postprocessor_warning"] = postprocessorWarning;
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
        const QString message = QString("Download failed.\n"
                                        "Failed to start yt-dlp process (%1): %2")
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

    // Find the last complete line ending
    int lastNewline = m_outputBuffer.lastIndexOf('\n');
    int lastCarriageReturn = m_outputBuffer.lastIndexOf('\r');
    int lastDelimiter = qMax(lastNewline, lastCarriageReturn);

    if (lastDelimiter == -1) {
        // No complete line yet, wait for more data
        return;
    }

    // Extract all complete lines
    QByteArray completeData = m_outputBuffer.left(lastDelimiter + 1);
    m_outputBuffer.remove(0, lastDelimiter + 1);

    // Split and process lines, skipping empty parts
    QStringList lines = QString::fromUtf8(completeData).split(QRegularExpression("[\\r\\n]"), Qt::SkipEmptyParts);

    for (const QString &line : lines) {
        handleOutputLine(line.trimmed()); // Ensure each line is trimmed
    }
}

void YtDlpWorker::parseStandardError(const QByteArray &output) {
    m_errorBuffer.append(output);
    qDebug() << "parseStandardError called. Current buffer size:" << m_errorBuffer.size();

    int lastNewline = m_errorBuffer.lastIndexOf('\n');
    int lastCarriageReturn = m_errorBuffer.lastIndexOf('\r');
    int lastDelimiter = qMax(lastNewline, lastCarriageReturn);

    if (lastDelimiter == -1) {
        return;
    }

    QByteArray completeData = m_errorBuffer.left(lastDelimiter + 1);
    m_errorBuffer.remove(0, lastDelimiter + 1);

    QStringList lines = QString::fromUtf8(completeData).split(QRegularExpression("[\\r\\n]"), Qt::SkipEmptyParts);

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

    QFile jsonFile(m_infoJsonPath);
    if (!jsonFile.exists()) {
        qDebug() << "readInfoJsonWithRetry: info.json file does not exist yet at:" << m_infoJsonPath;
        if (m_infoJsonRetryCount < 5) { // Retry up to 5 times
            m_infoJsonRetryCount++;
            QTimer::singleShot(500, this, &YtDlpWorker::readInfoJsonWithRetry); // Retry after 500ms
            qDebug() << "readInfoJsonWithRetry: Retrying in 500ms. Attempt:" << m_infoJsonRetryCount;
        } else {
            qWarning() << "readInfoJsonWithRetry: Max retries reached for info.json. File not found at:" << m_infoJsonPath;
            m_infoJsonPath.clear(); // Give up
        }
        return;
    }

    if (!jsonFile.open(QIODevice::ReadOnly)) {
        qWarning() << "readInfoJsonWithRetry: Could not open info.json file at:" << m_infoJsonPath << "Error:" << jsonFile.errorString();
        if (m_infoJsonRetryCount < 5) { // Retry up to 5 times
            m_infoJsonRetryCount++;
            QTimer::singleShot(500, this, &YtDlpWorker::readInfoJsonWithRetry); // Retry after 500ms
            qDebug() << "readInfoJsonWithRetry: Retrying in 500ms. Attempt:" << m_infoJsonRetryCount;
        } else {
            qWarning() << "readInfoJsonWithRetry: Max retries reached for info.json. Could not open file at:" << m_infoJsonPath;
            m_infoJsonPath.clear(); // Give up
        }
        return;
    }

    QByteArray jsonData = jsonFile.readAll();
    qDebug() << "readInfoJsonWithRetry: Successfully opened and read info.json. Data size:" << jsonData.size();
    jsonFile.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);

    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "readInfoJsonWithRetry: Failed to parse info.json as JSON or it's not an object. Error:" << parseError.errorString();
        if (m_infoJsonRetryCount < 5) { // Retry up to 5 times
            m_infoJsonRetryCount++;
            QTimer::singleShot(500, this, &YtDlpWorker::readInfoJsonWithRetry); // Retry after 500ms
            qDebug() << "readInfoJsonWithRetry: Retrying in 500ms. Attempt:" << m_infoJsonRetryCount;
        } else {
            qWarning() << "readInfoJsonWithRetry: Max retries reached for info.json. Invalid JSON at:" << m_infoJsonPath;
            m_infoJsonPath.clear(); // Give up
        }
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
    const QVariantList requestedDownloads = m_fullMetadata.value("requested_downloads").toList();
    if (!requestedDownloads.isEmpty()) {
        m_requestedTransferStatuses.clear();
        m_requestedTransferFormatIds.clear();
        m_requestedTransferSizes.clear();
    }
    for (const QVariant &requestedDownload : requestedDownloads) {
        const QVariantMap requestMap = requestedDownload.toMap();
        const QString vcodec = requestMap.value("vcodec").toString();
        const QString acodec = requestMap.value("acodec").toString();
        const QString formatId = requestMap.value("format_id").toString().trimmed();
        if (!vcodec.isEmpty() && vcodec != "none" && (acodec.isEmpty() || acodec == "none")) {
            m_requestedTransferStatuses.append("Downloading video stream...");
            m_requestedTransferFormatIds.append(formatId);
            m_requestedTransferSizes.append(inferPrimaryStreamSizeBytes(requestMap));
        } else if (!acodec.isEmpty() && acodec != "none" && (vcodec.isEmpty() || vcodec == "none")) {
            m_requestedTransferStatuses.append("Downloading audio stream...");
            m_requestedTransferFormatIds.append(formatId);
            m_requestedTransferSizes.append(inferPrimaryStreamSizeBytes(requestMap));
        } else if ((!vcodec.isEmpty() && vcodec != "none") || (!acodec.isEmpty() && acodec != "none")) {
            m_requestedTransferStatuses.append("Downloading media stream...");
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

    if (m_videoTitle.isEmpty() && obj.contains("title") && obj["title"].isString()) {
        m_videoTitle = obj["title"].toString();
        updateData["title"] = m_videoTitle;
        qDebug() << "Extracted title from info.json:" << m_videoTitle;
    }
    
    // Extract thumbnail path if available from the info.json
    bool hasWaitThumbnail = !m_thumbnailPath.isEmpty() && m_thumbnailPath.endsWith("_wait_thumbnail.jpg");
    if ((m_thumbnailPath.isEmpty() || hasWaitThumbnail) && obj.contains("thumbnails") && obj["thumbnails"].isArray()) {
        QJsonArray thumbnails = obj["thumbnails"].toArray();
        // yt-dlp adds a "filepath" key to the thumbnail entry it downloaded.
        for (const QJsonValue &thumbValue : thumbnails) {
            QJsonObject thumbObj = thumbValue.toObject();
            if (thumbObj.contains("filepath") && thumbObj["filepath"].isString()) {
                QString newThumb = QDir::toNativeSeparators(thumbObj["filepath"].toString());
                if (hasWaitThumbnail && newThumb != m_thumbnailPath) {
                    QFile::remove(m_thumbnailPath); // Clean up the wait thumbnail since we found a real one
                }
                m_thumbnailPath = newThumb;
                updateData["thumbnail_path"] = m_thumbnailPath;
                qDebug() << "Extracted thumbnail path from info.json:" << m_thumbnailPath;
                break; // Found it
            }
        }
    }

    if (!updateData.isEmpty()) {
        emit progressUpdated(m_id, updateData);
    }
}
