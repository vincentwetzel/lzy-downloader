#include "YtDlpWorker.h"

#include "core/ConfigManager.h"

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

namespace {
    [[nodiscard]] QString resolveTempDirectory(ConfigManager* configManager) {
        if (!configManager) {
            return QString();
        }
        QString tempDir = configManager->get(QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory")).toString();
        if (tempDir.isEmpty()) {
            if (const QString completedDir = configManager->get(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory")).toString(); !completedDir.isEmpty()) {
                tempDir = QDir(completedDir).filePath(QStringLiteral("temp_downloads"));
            }
        }
        return tempDir;
    }

    [[nodiscard]] bool isWaitThumbnail(const QString& thumbnailPath, const QString& id) {
        if (thumbnailPath.isEmpty()) {
            return false;
        }
        const QString waitThumbnailPrefix = QStringLiteral("%1_wait_thumbnail").arg(id);
        return QFileInfo(thumbnailPath).fileName().startsWith(waitThumbnailPrefix);
    }

    void cleanupWaitThumbnail(QString& thumbnailPath, const QString& id) {
        if (isWaitThumbnail(thumbnailPath, id)) {
            if (!QFile::remove(thumbnailPath) && QFile::exists(thumbnailPath)) {
                qWarning() << "Failed to clean up orphaned wait thumbnail:" << thumbnailPath;
            }
            thumbnailPath.clear();
        }
    }
}

void YtDlpWorker::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (m_finishEmitted) {
        return;
    }

    if (m_process) {
        // Process any remaining output. This is crucial for capturing the final file path.
        parseStandardOutput(m_process->readAllStandardOutput());
        // Also process any remaining stderr output
        parseStandardError(m_process->readAllStandardError());
    }

    // Force processing of any remaining buffered output by appending a newline.
    // This ensures the final partial line is processed safely without corrupting UTF-8 characters.
    if (!m_outputBuffer.isEmpty()) {
        parseProcessBuffer(m_outputBuffer, QByteArray(1, '\n'));
    }
    if (!m_errorBuffer.isEmpty()) {
        parseProcessBuffer(m_errorBuffer, QByteArray(1, '\n'));
    }

    m_finishEmitted = true;
    const bool normalExit = (exitStatus == QProcess::NormalExit);
    const bool capturedFinalFileExists = !m_finalFilename.isEmpty() && QFile::exists(m_finalFilename);
    const bool recoveredFromPostProcessorFailure = normalExit && exitCode != 0 && capturedFinalFileExists;
    const bool success = (normalExit && exitCode == 0 && !m_finalFilename.isEmpty()) || recoveredFromPostProcessorFailure;
    QString message = success ? tr("Download completed successfully.") : tr("Download failed.");
    QString postprocessorWarning;

    auto appendErrorPreview = [&message](const QStringList& lines) {
        constexpr qsizetype MAX_ERROR_PREVIEW_LENGTH = 200;
        if (!lines.isEmpty()) {
            message = QStringLiteral("%1\n%2").arg(message, lines.join(QLatin1Char('\n')).left(MAX_ERROR_PREVIEW_LENGTH));
        }
    };

    // Check if we are waiting for a user prompt (scheduled livestream)
    if (!success && m_errorEmitted && !m_promptDelayActive) {
        const QString errorStr = m_errorLines.join(QLatin1Char(' '));

        static const QRegularExpression premiereRegex(
            QStringLiteral("Premieres in|Premiering in|Premiere will begin|live event will begin|is upcoming|Offline \\(expected\\)|Offline expected|waiting for premiere|waiting for livestream|Live in |Starting in "),
            QRegularExpression::CaseInsensitiveOption
        );

        if (premiereRegex.match(errorStr).hasMatch()) {
            m_promptDelayActive = true;
            qDebug() << "[YtDlpWorker] Delaying finished signal to wait for user prompt response.";

            QVariantMap progressData;
            progressData.insert(QStringLiteral("status"), tr("Waiting for user response..."));
            progressData.insert(QStringLiteral("progress"), -1);
            if (!m_videoTitle.isEmpty()) {
                progressData.insert(QStringLiteral("title"), m_videoTitle);
            }
            if (!m_thumbnailPath.isEmpty()) {
                progressData.insert(QStringLiteral("thumbnail_path"), m_thumbnailPath);
            }
            emit progressUpdated(m_id, progressData);

            constexpr auto USER_PROMPT_TIMEOUT = std::chrono::minutes(5);
            QTimer::singleShot(USER_PROMPT_TIMEOUT, this, [this, exitCode, exitStatus]() {
                qDebug() << "[YtDlpWorker] User prompt timeout reached. Emitting finished signal.";
                m_finishEmitted = false; // Reset so onProcessFinished actually executes
                onProcessFinished(exitCode, exitStatus);
            });
            return;
        }
    }

    // Resolve temporary directory once for all fallback logic in this scope
    const QString resolvedTempDir = resolveTempDirectory(m_configManager);

    if (recoveredFromPostProcessorFailure) {
        message = tr("Download completed, but thumbnail/post-processing reported a warning.");
        appendErrorPreview(m_errorLines);
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
        if (isWaitThumbnail(m_thumbnailPath, m_id)) {
            if (!resolvedTempDir.isEmpty()) {
                const QString uuidDirPath = QDir(resolvedTempDir).filePath(m_id);
                const QString thumbnailFileName = QFileInfo(m_thumbnailPath).fileName();
                const QString newThumbPath = QDir(uuidDirPath).filePath(thumbnailFileName);

                if (!QDir(resolvedTempDir).mkpath(m_id)) {
                    qWarning() << "Failed to create UUID directory for thumbnail:" << uuidDirPath;
                }

                if (m_thumbnailPath != newThumbPath) {
                    if (QFile::rename(m_thumbnailPath, newThumbPath) || (QFile::copy(m_thumbnailPath, newThumbPath) && QFile::remove(m_thumbnailPath))) {
                        m_thumbnailPath = newThumbPath;
                        qDebug() << "Moved wait thumbnail into UUID directory for automatic cleanup:" << m_thumbnailPath;
                    } else {
                        qWarning() << "Failed to move wait thumbnail to UUID directory:" << m_thumbnailPath;
                    }
                }
            }
        }

        // Ensure metadata is loaded if it hasn't been asynchronously parsed yet
        if (!m_infoJsonPath.isEmpty()) {
            if (m_fullMetadata.isEmpty()) {
                QFile jsonFile(m_infoJsonPath);
                if (jsonFile.open(QIODevice::ReadOnly)) {
                    const QByteArray jsonData = jsonFile.readAll();
                    jsonFile.close();

                    QJsonParseError parseError;
                    const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
                    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                        m_fullMetadata = doc.object().toVariantMap();
                    } else {
                        qWarning() << "Failed to parse info.json in onProcessFinished:" << parseError.errorString();
                    }
                }
            }

            if (QFile::remove(m_infoJsonPath)) {
                qDebug() << "Cleaned up info.json file:" << m_infoJsonPath;
            } else if (QFile::exists(m_infoJsonPath)) {
                qWarning() << "Failed to clean up info.json file:" << m_infoJsonPath;
            }

            m_infoJsonPath.clear(); // Clear the path so any pending readInfoJsonWithRetry timers abort cleanly
        }

        // Use the full metadata that was already parsed from info.json during readInfoJsonWithRetry
        if (!m_fullMetadata.isEmpty()) {
            metadata = m_fullMetadata;
            qDebug() << "onProcessFinished: Using cached metadata with" << metadata.size() << "keys. Keys:" << metadata.keys();
            // Ensure m_videoTitle is consistent with what's in metadata
            if (m_videoTitle.isEmpty()) {
                m_videoTitle = metadata.value(QStringLiteral("title")).toString();
            }
            if (metadata.contains(QStringLiteral("uploader"))) {
                qDebug() << "onProcessFinished: uploader from metadata:" << metadata.value(QStringLiteral("uploader")).toString();
            } else {
                qWarning() << "onProcessFinished: uploader NOT found in cached metadata!";
            }
        } else {
            qWarning() << "onProcessFinished: No cached metadata available. Sorting rules may not work.";
        }

        // CRITICAL FIX: Emit 100% progress update before finished signal to ensure
        // the UI progress bar reaches 100% and turns green, not stuck at <100%
        QVariantMap finalProgressData;
        finalProgressData.insert(QStringLiteral("progress"), 100);
        finalProgressData.insert(QStringLiteral("status"), tr("Complete"));
        if (!m_videoTitle.isEmpty()) {
            finalProgressData.insert(QStringLiteral("title"), m_videoTitle);
        }
        if (!m_thumbnailPath.isEmpty()) {
            finalProgressData.insert(QStringLiteral("thumbnail_path"), m_thumbnailPath);
        }
        emit progressUpdated(m_id, finalProgressData);
    }

    if (!success) {
        if (exitStatus == QProcess::CrashExit) {
            message = QStringLiteral("%1\n%2").arg(message, tr("Process crashed: %1").arg(m_process ? m_process->errorString() : tr("Unknown error")));
        }

        if (!m_errorLines.isEmpty()) {
            appendErrorPreview(m_errorLines);
        } else {
            // Fallback: Use the last few lines of general output if no ERROR: lines were captured
            constexpr qsizetype MAX_FALLBACK_LINES = 5;
            if (!m_allOutputLines.isEmpty()) {
                appendErrorPreview(m_allOutputLines.mid(qMax(qsizetype(0), m_allOutputLines.size() - MAX_FALLBACK_LINES)));
            }
        }

        cleanupWaitThumbnail(m_thumbnailPath, m_id);
    }

    // Try to clean up empty UUID directory if yt-dlp failed before writing anything,
    // or if the process completed but left the directory empty (e.g. skipped downloads).
    if (!resolvedTempDir.isEmpty()) {
        if (QDir(resolvedTempDir).rmdir(m_id)) { // Only succeeds if the directory is empty
            qDebug() << "Removed empty UUID directory:" << QDir(resolvedTempDir).filePath(m_id);
        }
    }

    // Ensure m_videoTitle is included in the metadata for the finished signal
    if (!m_videoTitle.isEmpty() && !metadata.contains(QStringLiteral("title"))) {
        metadata.insert(QStringLiteral("title"), m_videoTitle);
    }
    if (!m_thumbnailPath.isEmpty() && !metadata.contains(QStringLiteral("thumbnail_path"))) {
        metadata.insert(QStringLiteral("thumbnail_path"), m_thumbnailPath);
    }
    if (!postprocessorWarning.isEmpty()) {
        metadata.insert(QStringLiteral("postprocessor_warning"), postprocessorWarning);
    }

    emit finished(m_id, success, message, m_finalFilename, m_originalDownloadedFilename, metadata);
}


void YtDlpWorker::onProcessError(QProcess::ProcessError error) {
    if (m_finishEmitted) {
        return;
    }

    if (!m_process) {
        return;
    }

    // Crashed, ReadError, and WriteError will eventually emit finished() anyway.
    // We only need to manually emit finished and abort if the process FailedToStart,
    // because finished() is never emitted in that state.
    if (error == QProcess::FailedToStart) {
        m_finishEmitted = true;
        const QString message = tr("Failed to start yt-dlp process. Please check your yt-dlp installation.\nError: %1")
                                    .arg(m_process->errorString());
        qWarning() << message;

        cleanupWaitThumbnail(m_thumbnailPath, m_id);

        // Try to clean up empty UUID directory since finished() won't run
        const QString resolvedTempDir = resolveTempDirectory(m_configManager);
        if (!resolvedTempDir.isEmpty()) {
            QDir(resolvedTempDir).rmdir(m_id);
        }

        emit finished(m_id, false, message, QString(), QString(), QVariantMap());
    }
}

void YtDlpWorker::onReadyReadStandardOutput() {
    if (!m_process) {
        return;
    }
    const QByteArray data = m_process->readAllStandardOutput();
    parseStandardOutput(data);
}

void YtDlpWorker::onReadyReadStandardError() {
    if (!m_process) {
        return;
    }
    const QByteArray data = m_process->readAllStandardError();
    parseStandardError(data);
}

void YtDlpWorker::parseProcessBuffer(QByteArray &buffer, const QByteArray &newData) {
    buffer.append(newData);

    const qsizetype lastDelimiter = qMax(buffer.lastIndexOf('\n'), buffer.lastIndexOf('\r'));
    if (lastDelimiter == -1) {
        return;
    }

    qsizetype start = 0;
    for (qsizetype i = 0; i <= lastDelimiter; ++i) {
        const char c = buffer.at(i);
        if (c == '\n' || c == '\r') {
            if (i > start) {
                const QByteArrayView chunk(buffer.constData() + start, i - start);
                const QByteArrayView trimmedChunk = chunk.trimmed();
                if (!trimmedChunk.isEmpty()) {
                    handleOutputLine(QString::fromUtf8(trimmedChunk));
                }
            }
            start = i + 1;
        }
    }

    buffer.remove(0, lastDelimiter + 1);
}

void YtDlpWorker::parseStandardOutput(const QByteArray &output) {
    parseProcessBuffer(m_outputBuffer, output);
}

void YtDlpWorker::parseStandardError(const QByteArray &output) {
    parseProcessBuffer(m_errorBuffer, output);
}

void YtDlpWorker::readInfoJsonWithRetry() {
    qDebug() << "readInfoJsonWithRetry: Attempting to read info.json. Path:" << m_infoJsonPath << "Retry:" << m_infoJsonRetryCount;

    if (m_infoJsonPath.isEmpty()) {
        qDebug() << "readInfoJsonWithRetry: No info.json path set.";
        return;
    }

    auto scheduleRetry = [this](const QString& reason) {
        qWarning().noquote() << "readInfoJsonWithRetry:" << reason;
        constexpr int MAX_JSON_RETRIES = 5;
        constexpr auto JSON_RETRY_INTERVAL = std::chrono::milliseconds(500);
        if (m_infoJsonRetryCount < MAX_JSON_RETRIES) {
            m_infoJsonRetryCount++;
            QTimer::singleShot(JSON_RETRY_INTERVAL, this, &YtDlpWorker::readInfoJsonWithRetry);
            qDebug() << "readInfoJsonWithRetry: Retrying in 500ms. Attempt:" << m_infoJsonRetryCount;
        } else {
            qWarning() << "readInfoJsonWithRetry: Max retries reached for info.json.";
            m_infoJsonPath.clear(); // Give up
        }
    };

    QFile jsonFile(m_infoJsonPath);
    if (!jsonFile.open(QIODevice::ReadOnly)) {
        bool foundFallback = false;
        if (m_configManager) {
            const QString tempDir = resolveTempDirectory(m_configManager);
            if (!tempDir.isEmpty()) {
                QDir uuidDir(QDir(tempDir).filePath(m_id));
                if (uuidDir.exists()) {
                    const QStringList infoFiles = uuidDir.entryList({QStringLiteral("*.info.json")}, QDir::Files);
                    if (!infoFiles.isEmpty()) {
                        m_infoJsonPath = uuidDir.absoluteFilePath(infoFiles.first());
                        jsonFile.setFileName(m_infoJsonPath);
                        if (jsonFile.open(QIODevice::ReadOnly)) {
                            foundFallback = true;
                            qDebug() << "readInfoJsonWithRetry: Found info.json via directory scan fallback:" << m_infoJsonPath;
                        }
                    }
                }
            }
        }

        if (!foundFallback) {
            scheduleRetry(QStringLiteral("Could not open info.json file at: %1 Error: %2").arg(m_infoJsonPath, jsonFile.errorString()));
            return;
        }
    }

    const QByteArray jsonData = jsonFile.readAll();
    qDebug() << "readInfoJsonWithRetry: Successfully opened and read info.json. Data size:" << jsonData.size();
    jsonFile.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        scheduleRetry(QStringLiteral("Failed to parse info.json as JSON or it's not an object. Error: %1").arg(parseError.errorString()));
        return;
    }

    // If we successfully parsed the file, we don't need to retry anymore.
    m_infoJsonRetryCount = 0;

    const QJsonObject obj = doc.object();
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

        const bool hasVideo = !vcodec.isEmpty() && vcodec != QStringLiteral("none");
        const bool hasAudio = !acodec.isEmpty() && acodec != QStringLiteral("none");

        if (hasVideo || hasAudio) {
            QString status = tr("Downloading media stream...");
            if (hasVideo && !hasAudio) {
                status = tr("Downloading video stream...");
            } else if (hasAudio && !hasVideo) {
                status = tr("Downloading audio stream...");
            }
            m_requestedTransferStatuses.append(status);
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

    if (m_videoTitle.isEmpty()) {
        if (const QJsonValue titleVal = obj.value(QStringLiteral("title")); titleVal.isString()) {
            m_videoTitle = titleVal.toString();
            updateData.insert(QStringLiteral("title"), m_videoTitle);
            qDebug() << "Extracted title from info.json:" << m_videoTitle;
        }
    }

    if (const QJsonValue durationVal = obj.value(QStringLiteral("duration")); durationVal.isDouble()) {
        updateData.insert(QStringLiteral("duration"), durationVal.toDouble());
    } else if (const QJsonValue durationStrVal = obj.value(QStringLiteral("duration_string")); durationStrVal.isString()) {
        updateData.insert(QStringLiteral("duration_string"), durationStrVal.toString());
    }

    if (const QJsonValue isLiveVal = obj.value(QStringLiteral("is_live")); isLiveVal.isBool()) {
        updateData.insert(QStringLiteral("is_live"), isLiveVal.toBool());
        qDebug() << "Extracted is_live from info.json:" << isLiveVal.toBool();
    }

    // Extract thumbnail path if available from the info.json
    if (const QJsonValue thumbnailsVal = obj.value(QStringLiteral("thumbnails")); (m_thumbnailPath.isEmpty() || isWaitThumbnail(m_thumbnailPath, m_id)) && thumbnailsVal.isArray()) {
        const QJsonArray thumbnails = thumbnailsVal.toArray();
        // yt-dlp adds a "filepath" key to the thumbnail entry it downloaded.
        for (const QJsonValue &thumbValue : thumbnails) {
            if (thumbValue.isObject()) {
                const QJsonObject thumbObj = thumbValue.toObject();
                const QJsonValue filepathVal = thumbObj.value(QStringLiteral("filepath"));
                if (filepathVal.isString()) {
                    const QString newThumb = QDir::toNativeSeparators(filepathVal.toString());
                    if (newThumb != m_thumbnailPath) {
                        cleanupWaitThumbnail(m_thumbnailPath, m_id);
                        m_thumbnailPath = newThumb;
                    }
                    updateData.insert(QStringLiteral("thumbnail_path"), m_thumbnailPath);
                    qDebug() << "Extracted thumbnail path from info.json:" << m_thumbnailPath;
                    break; // Found it
                }
            }
        }
    }

    if (!updateData.isEmpty()) {
        emit progressUpdated(m_id, updateData);
    }
}
