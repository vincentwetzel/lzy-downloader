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

    const QString waitThumbnailPrefix = QStringLiteral("%1_wait_thumbnail").arg(m_id);

    if (m_process) {
        // Process any remaining output. This is crucial for capturing the final file path.
        parseStandardOutput(m_process->readAllStandardOutput());
        // Also process any remaining stderr output
        parseStandardError(m_process->readAllStandardError());
    }

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
    const bool success = (normalExit && exitCode == 0 && !m_finalFilename.isEmpty()) || recoveredFromPostProcessorFailure;
    QString message = success ? tr("Download completed successfully.") : tr("Download failed.");
    QString postprocessorWarning;

    // Resolve temporary directory once for all fallback logic in this scope
    QString resolvedTempDir;
    if (m_configManager) {
        resolvedTempDir = m_configManager->get(QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory")).toString();
        if (resolvedTempDir.isEmpty()) {
            const QString completedDir = m_configManager->get(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory")).toString();
            if (!completedDir.isEmpty()) {
                resolvedTempDir = QDir(completedDir).filePath(QStringLiteral("temp_downloads"));
            }
        }
    }

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

    if (recoveredFromPostProcessorFailure) {
        message = tr("Download completed, but thumbnail/post-processing reported a warning.");
        if (!m_errorLines.isEmpty()) {
            message = QStringLiteral("%1\n%2").arg(message, m_errorLines.join(QLatin1Char('\n')).left(200));
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
        if (!m_thumbnailPath.isEmpty()) {
            const QString thumbnailFileName = QFileInfo(m_thumbnailPath).fileName();
            if (thumbnailFileName.startsWith(waitThumbnailPrefix)) {
                if (!resolvedTempDir.isEmpty()) {
                    const QString uuidDirPath = QDir(resolvedTempDir).filePath(m_id);
                    const QString newThumbPath = QDir(uuidDirPath).filePath(thumbnailFileName);

                    if (!QDir().mkpath(uuidDirPath)) {
                        qWarning() << "Failed to create UUID directory for thumbnail:" << uuidDirPath;
                    }

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
            QFile jsonFile(m_infoJsonPath);
            if (m_fullMetadata.isEmpty() && jsonFile.open(QIODevice::ReadOnly)) {
                QJsonParseError parseError;
                const QJsonDocument doc = QJsonDocument::fromJson(jsonFile.readAll(), &parseError);
                if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                    m_fullMetadata = doc.object().toVariantMap();
                } else {
                    qWarning() << "Failed to parse info.json in onProcessFinished:" << parseError.errorString();
                }
                jsonFile.close();
            }

            if (jsonFile.remove()) {
                qDebug() << "Cleaned up info.json file:" << m_infoJsonPath;
            } else {
                qWarning() << "Failed to clean up info.json file:" << m_infoJsonPath;
            }
        }

        // Use the full metadata that was already parsed from info.json during readInfoJsonWithRetry
        if (!m_fullMetadata.isEmpty()) {
            metadata = m_fullMetadata;
            qDebug() << "onProcessFinished: Using cached metadata with" << metadata.size() << "keys. Keys:" << metadata.keys();
            // Ensure m_videoTitle is consistent with what's in metadata
            if (metadata.contains(QStringLiteral("title")) && m_videoTitle.isEmpty()) {
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
        if (!m_errorLines.isEmpty()) {
            message = QStringLiteral("%1\n%2").arg(message, m_errorLines.join(QLatin1Char('\n')).left(200));
        } else {
            // Fallback: Use the last few lines of general output if no ERROR: lines were captured
            if (!m_allOutputLines.isEmpty()) {
                message = QStringLiteral("%1\n%2").arg(message, m_allOutputLines.mid(qMax(qsizetype(0), m_allOutputLines.size() - 5)).join(QLatin1Char('\n')).left(200));
            }
        }

        // Clean up orphaned wait thumbnail on failure
        if (!m_thumbnailPath.isEmpty() && QFileInfo(m_thumbnailPath).fileName().startsWith(waitThumbnailPrefix)) {
            if (!QFile::remove(m_thumbnailPath)) {
                qWarning() << "Failed to clean up orphaned wait thumbnail:" << m_thumbnailPath;
            }
            m_thumbnailPath.clear();
        }
    }

    // Try to clean up empty UUID directory if yt-dlp failed before writing anything,
    // or if the process completed but left the directory empty (e.g. skipped downloads).
    if (!resolvedTempDir.isEmpty()) {
        const QString uuidDirPath = QDir(resolvedTempDir).filePath(m_id);
        if (QDir().rmdir(uuidDirPath)) { // Only succeeds if the directory is empty
            qDebug() << "Removed empty UUID directory:" << uuidDirPath;
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

    if (error == QProcess::FailedToStart || error == QProcess::Crashed ||
        error == QProcess::ReadError || error == QProcess::WriteError) {
        m_finishEmitted = true;
        const QString message = tr("Download failed.\nyt-dlp process error (%1): %2")
                                    .arg(static_cast<int>(error))
                                    .arg(m_process->errorString());
        qWarning() << message;
        emit finished(m_id, false, message, QString(), QString(), QVariantMap());
    }
}

void YtDlpWorker::onReadyReadStandardOutput() {
    if (!m_process) return;
    const QByteArray data = m_process->readAllStandardOutput();
    qDebug() << "[STDOUT] Received" << data.size() << "bytes.";
    parseStandardOutput(data);
}

void YtDlpWorker::onReadyReadStandardError() {
    if (!m_process) return;
    const QByteArray data = m_process->readAllStandardError();
    qDebug() << "[STDERR] Received" << data.size() << "bytes.";
    parseStandardError(data);
}

void YtDlpWorker::parseProcessBuffer(QByteArray &buffer, const QByteArray &newData)
{
    buffer.append(newData);

    const qsizetype lastDelimiter = qMax(buffer.lastIndexOf('\n'), buffer.lastIndexOf('\r'));
    if (lastDelimiter == -1) return;

    static const QRegularExpression newlineRegex(QStringLiteral("[\\r\\n]"));
    const QString linesStr = QString::fromUtf8(buffer.constData(), lastDelimiter + 1);
    const QStringList lines = linesStr.split(newlineRegex, Qt::SkipEmptyParts);
    buffer.remove(0, lastDelimiter + 1);

    for (const QString &line : lines) {
        handleOutputLine(line.trimmed());
    }
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
        scheduleRetry(QStringLiteral("Could not open info.json file at: %1 Error: %2").arg(m_infoJsonPath, jsonFile.errorString()));
        return;
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

        if (hasVideo && !hasAudio) {
            m_requestedTransferStatuses.append(tr("Downloading video stream..."));
            m_requestedTransferFormatIds.append(formatId);
            m_requestedTransferSizes.append(inferPrimaryStreamSizeBytes(requestMap));
        } else if (hasAudio && !hasVideo) {
            m_requestedTransferStatuses.append(tr("Downloading audio stream..."));
            m_requestedTransferFormatIds.append(formatId);
            m_requestedTransferSizes.append(inferPrimaryStreamSizeBytes(requestMap));
        } else if (hasVideo || hasAudio) {
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

    if (m_videoTitle.isEmpty() && obj.contains(QStringLiteral("title")) && obj.value(QStringLiteral("title")).isString()) {
        m_videoTitle = obj.value(QStringLiteral("title")).toString();
        updateData.insert(QStringLiteral("title"), m_videoTitle);
        qDebug() << "Extracted title from info.json:" << m_videoTitle;
    }

    if (obj.contains(QStringLiteral("duration"))) {
        updateData.insert(QStringLiteral("duration"), obj.value(QStringLiteral("duration")).toDouble());
    } else if (obj.contains(QStringLiteral("duration_string")) && obj.value(QStringLiteral("duration_string")).isString()) {
        updateData.insert(QStringLiteral("duration_string"), obj.value(QStringLiteral("duration_string")).toString());
    }

    // Extract thumbnail path if available from the info.json
    const QString waitThumbnailPrefix = QStringLiteral("%1_wait_thumbnail").arg(m_id);
    const bool hasWaitThumbnail = !m_thumbnailPath.isEmpty() && QFileInfo(m_thumbnailPath).fileName().startsWith(waitThumbnailPrefix);
    if ((m_thumbnailPath.isEmpty() || hasWaitThumbnail) && obj.contains(QStringLiteral("thumbnails")) && obj.value(QStringLiteral("thumbnails")).isArray()) {
        const QJsonArray thumbnails = obj.value(QStringLiteral("thumbnails")).toArray();
        // yt-dlp adds a "filepath" key to the thumbnail entry it downloaded.
        for (const QJsonValue &thumbValue : thumbnails) {
            if (thumbValue.isObject()) {
                const QJsonObject thumbObj = thumbValue.toObject();
                if (thumbObj.contains(QStringLiteral("filepath")) && thumbObj.value(QStringLiteral("filepath")).isString()) {
                    const QString newThumb = QDir::toNativeSeparators(thumbObj.value(QStringLiteral("filepath")).toString());
                    if (hasWaitThumbnail && newThumb != m_thumbnailPath) {
                        if (!QFile::remove(m_thumbnailPath)) { // Clean up the wait thumbnail since we found a real one
                            qWarning() << "Failed to clean up wait thumbnail:" << m_thumbnailPath;
                        }
                    }
                    m_thumbnailPath = newThumb;
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
