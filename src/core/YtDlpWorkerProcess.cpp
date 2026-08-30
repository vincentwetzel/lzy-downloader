#include "YtDlpWorker.h"

#include "core/ConfigManager.h"
#include "core/DownloadTempCleanup.h"

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
#include <utility>

#include "YtDlpWorkerProcessHelpers.h"

using namespace YtDlpWorkerProcessHelpers;

void YtDlpWorker::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (m_finishEmitted) {
        return;
    }

    if (m_progressPollTimer) {
        m_progressPollTimer->stop();
    }

    if (m_process) {
        // Process any remaining output. This is crucial for capturing the final file path.
        parseStandardOutput(m_process->readAllStandardOutput());
        // Also process any remaining stderr output
        parseStandardError(m_process->readAllStandardError());
    }

    // Force processing of any remaining buffered output by appending a newline.
    // This ensures the final partial line is processed safely without corrupting UTF-8 characters.
    auto flushBuffer = [this](QByteArray& buffer) {
        if (!buffer.isEmpty()) {
            parseProcessBuffer(buffer, QByteArrayLiteral("\n"));
        }
    };
    flushBuffer(m_outputBuffer);
    flushBuffer(m_errorBuffer);

    m_finishEmitted = true;
    const bool normalExit = (exitStatus == QProcess::NormalExit);
    const bool capturedFinalFileExists = !m_finalFilename.isEmpty() && QFile::exists(m_finalFilename);

    // A printed final path is not sufficient evidence of a usable file. yt-dlp
    // can print that path after producing a partial stream, and FFmpeg may then
    // report an invalid container while trying to repair it.
    const bool hasFatalDiagnostic = hasFatalDownloadDiagnostic();
    if (hasFatalDiagnostic) {
        qWarning() << "[YtDlpWorker] Detected a fatal download diagnostic; refusing to finalize the observed path for" << m_id;
    }

    const bool recoveredFromPostProcessorFailure = normalExit && exitCode != 0 && capturedFinalFileExists && !hasFatalDiagnostic;
    const bool success = !hasFatalDiagnostic && ((normalExit && exitCode == 0 && !m_finalFilename.isEmpty()) || recoveredFromPostProcessorFailure);
    if (!success) {
        // Existing logging for hard failures
        qWarning() << "[YtDlpWorker] yt-dlp finished unsuccessfully for" << m_id << " (fatal diagnostic detected: " << hasFatalDiagnostic << ")"
                   << "exitCode:" << exitCode
                   << "exitStatus:" << exitStatus;
        if (!m_errorLines.isEmpty()) {
            qWarning().noquote() << "[YtDlpWorker] Error output captured:" << m_errorLines.join(QStringLiteral("\n"));
        }
        if (!m_allOutputLines.isEmpty()) {
            constexpr qsizetype MAX_FALLBACK_LOG_LINES = 50; // Log up to 50 lines of context on any failure
            const QStringList outputLines = m_allOutputLines.lines();
            qWarning().noquote() << "[YtDlpWorker] Last diagnostic output (no specific errors captured):"
                                 << outputLines.mid(qMax(qsizetype(0), outputLines.size() - MAX_FALLBACK_LOG_LINES)).join(QLatin1Char('\n'));
        }
    }
    // Add specific logging for "completed with warnings" scenarios
    else if (recoveredFromPostProcessorFailure) {
        qWarning() << "[YtDlpWorker] yt-dlp exited with code" << exitCode << "after producing final media for" << m_id
                   << ". This is treated as a completion with warnings. Full output for diagnostics:";
        if (!m_errorLines.isEmpty()) {
            qWarning().noquote() << "[YtDlpWorker] Captured error/warning lines:" << m_errorLines.join(QStringLiteral("\n"));
        }
        if (!m_allOutputLines.isEmpty()) {
            qWarning().noquote() << "[YtDlpWorker] Full yt-dlp output (stdout/stderr combined):" << m_allOutputLines.join(QStringLiteral("\n"));
        } else {
            qWarning() << "[YtDlpWorker] No output lines captured for this warning.";
        }
    }
    if (!success && retryWithoutBrowserCookiesIfCookieExtractionFailed()) {
        return;
    }
    QString message = success ? tr("Download completed successfully.") : tr("Download failed.");
    QString postprocessorWarning;
    QString exitCodeWarning;

    auto createProgressData = [this](const QString& status, int progress) {
        QVariantMap data;
        data.insert(QStringLiteral("status"), status);
        data.insert(QStringLiteral("progress"), progress);
        if (!m_videoTitle.isEmpty()) {
            data.insert(QStringLiteral("title"), m_videoTitle);
        }
        if (!m_thumbnailPath.isEmpty()) {
            data.insert(QStringLiteral("thumbnail_path"), m_thumbnailPath);
        }
        return data;
    };

    auto appendMessage = [&message](const QString& extraText) {
        if (!extraText.isEmpty()) {
            if (!message.isEmpty()) message += QLatin1Char('\n');
            message += extraText;
        }
    };

    auto appendErrorPreview = [&appendMessage](const QStringList& lines) {
        if (!lines.isEmpty()) {
            constexpr qsizetype MAX_ERROR_PREVIEW_LENGTH = 200;
            QString preview;
            preview.reserve(MAX_ERROR_PREVIEW_LENGTH + 16); // Reserve with some slack
            for (const QString& line : std::as_const(lines)) {
                if (!preview.isEmpty()) {
                    preview.append(QLatin1Char('\n'));
                }
                preview.append(line);
                if (preview.length() >= MAX_ERROR_PREVIEW_LENGTH) {
                    break;
                }
            }
            if (preview.length() > MAX_ERROR_PREVIEW_LENGTH) {
                preview.truncate(MAX_ERROR_PREVIEW_LENGTH);
            }
            appendMessage(preview);
        }
    };

    // Check if we are waiting for a user prompt (scheduled livestream)
    if (!success && m_errorEmitted && !m_promptDelayActive) {
        static const QRegularExpression premiereRegex(
            QStringLiteral("Premieres in|Premiering in|Premiere will begin|live event will begin|is upcoming|Offline \\(expected\\)|Offline expected|waiting for premiere|waiting for livestream|Live in |Starting in "),
            QRegularExpression::CaseInsensitiveOption
        );

        bool matchesPremiere = false;
        for (const QString& line : std::as_const(m_errorLines)) {
            if (line.contains(QStringLiteral("Premiere"), Qt::CaseInsensitive) ||
                line.contains(QStringLiteral("live"), Qt::CaseInsensitive) ||
                line.contains(QStringLiteral("Offline"), Qt::CaseInsensitive) ||
                line.contains(QStringLiteral("Starting"), Qt::CaseInsensitive) ||
                line.contains(QStringLiteral("upcoming"), Qt::CaseInsensitive)) {
                if (premiereRegex.match(line).hasMatch()) {
                    matchesPremiere = true;
                    break;
                }
            }
        }

        if (matchesPremiere) {
            m_promptDelayActive = true;
            qDebug() << "[YtDlpWorker] Delaying finished signal to wait for user prompt response.";

            emit progressUpdated(m_id, createProgressData(tr("Waiting for user response..."), -1));

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
    const QString resolvedTempDir = DownloadTempCleanup::resolveRoot(m_configManager);

    if (recoveredFromPostProcessorFailure) {
        message = tr("Download completed, but thumbnail/post-processing reported a warning.");
        appendErrorPreview(m_errorLines.lines());
        postprocessorWarning = message;
        exitCodeWarning = tr("yt-dlp exited with non-zero code %1 after producing final media.").arg(exitCode);
        qWarning() << "yt-dlp exited with code" << exitCode
                   << "after producing final media. Continuing finalization for"
                   << m_id << "at" << m_finalFilename;
    }

    if (!m_finalFilename.isEmpty()) {
        qDebug() << "Final filename captured:" << m_finalFilename;
    } else {
        if (!m_errorEmitted && !hasFatalDiagnostic) {
            qWarning() << "Could not determine final filename. Download may have failed or produced no output.";
        }
        if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
            appendMessage(tr("Could not determine final filename."));
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
                    if (QFile::rename(m_thumbnailPath, newThumbPath)) {
                        m_thumbnailPath = newThumbPath;
                        qDebug() << "Moved wait thumbnail into UUID directory for automatic cleanup:" << m_thumbnailPath;
                    } else if (QFile::copy(m_thumbnailPath, newThumbPath)) {
                        safeRemoveFile(m_thumbnailPath, QStringLiteral("original wait thumbnail after copy"));
                        m_thumbnailPath = newThumbPath;
                        qDebug() << "Copied wait thumbnail into UUID directory for automatic cleanup:" << m_thumbnailPath;
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
                    QString errorStr;
                    if (const QJsonObject obj = parseJsonData(jsonFile.readAll(), &errorStr); !obj.isEmpty()) {
                        m_fullMetadata = obj.toVariantMap();
                    } else {
                        qWarning() << "Failed to parse info.json in onProcessFinished:" << errorStr;
                    }
                }
            }

            safeRemoveFile(m_infoJsonPath, QStringLiteral("info.json"));

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
        emit progressUpdated(m_id, createProgressData(tr("Complete"), 100));
    }

    if (!success) {
        if (exitStatus == QProcess::CrashExit) {
            appendMessage(tr("Process crashed: %1").arg(m_process ? m_process->errorString() : tr("Unknown error")));
        }

        // Avoid showing authentication tips if yt-dlp explicitly handed off to ffmpeg,
        // because that means metadata/auth extraction succeeded and the error is in the muxer/downloader.
        bool hasFfmpegError = false;
        for (const QString& line : std::as_const(m_errorLines)) {
            if (line.contains(QStringLiteral("ffmpeg exited with code"), Qt::CaseInsensitive)) {
                hasFfmpegError = true;
                break;
            }
        }

        // Add a clear hint about cookie authentication if cookies were utilized or attempted
        const bool hadCookieFailure = m_retriedWithoutBrowserCookies || hasBrowserCookieFailureDiagnostic();
        if (hadCookieFailure && !hasFfmpegError) {
            QString browserName = m_configManager ? m_configManager->get(QStringLiteral("General"), QStringLiteral("cookies_from_browser"), tr("None")).toString() : QString();
            if (browserName.isEmpty() || browserName.compare(QStringLiteral("None"), Qt::CaseInsensitive) == 0 || browserName.compare(tr("None"), Qt::CaseInsensitive) == 0) {
                browserName = tr("configured browser");
            }
            appendMessage(tr("Authentication Tip: If this platform requires a login (or is a live stream), "
                             "please verify you are signed in to the service in your %1. "
                             "Stale or signed-out browser profiles will cause these downloads to fail.")
                             .arg(browserName));
        }

        if (!m_errorLines.isEmpty()) {
            appendErrorPreview(m_errorLines.lines());
        } else {
            // Fallback: Use the last few lines of general output if no ERROR: lines were captured
            constexpr qsizetype MAX_FALLBACK_LINES = 5;
            if (!m_allOutputLines.isEmpty()) {
                appendErrorPreview(m_allOutputLines.mid(qMax(qsizetype(0), m_allOutputLines.size() - MAX_FALLBACK_LINES)));
            }
        }

        if (hasIncompleteMediaDiagnostic()) {
            appendMessage(tr("The media transfer was incomplete or invalid. FFmpeg could not read the partial output; this was a download/stream failure, not a missing FFmpeg installation."));
        }

        if (!m_recoveryDiagnostic.isEmpty()) {
            appendMessage(tr("Automatic downloader fallback: %1").arg(m_recoveryDiagnostic));
        }

        cleanupWaitThumbnail(m_thumbnailPath, m_id);
    }

    // Try to clean up empty UUID directory if yt-dlp failed before writing anything,
    // or if the process completed but left the directory empty (e.g. skipped downloads).
    (void)DownloadTempCleanup::removeEmptyOwnedDirectory(m_id, DownloadTempCleanup::pathForId(resolvedTempDir, m_id));

    // Ensure core properties are included in the metadata for the finished signal
    auto insertMetadataIfMissing = [&metadata](const QString& key, const QString& value) {
        if (!value.isEmpty() && !metadata.contains(key)) {
            metadata.insert(key, value);
        }
    };
    
    insertMetadataIfMissing(QStringLiteral("title"), m_videoTitle);
    insertMetadataIfMissing(QStringLiteral("thumbnail_path"), m_thumbnailPath);
    insertMetadataIfMissing(QStringLiteral("postprocessor_warning"), postprocessorWarning);
    insertMetadataIfMissing(QStringLiteral("yt_dlp_exit_code_warning"), exitCodeWarning);

    // Inject clean environmental tips to guide Python-based yt-dlp users elegantly
    if (this->property("missing_impersonation").toBool()) {
        insertMetadataIfMissing(QStringLiteral("dependency_recommendation"),
            tr("System Tip: To enable browser impersonation and prevent future blocks, run this in your terminal:\npip install curl-cffi"));
    }

    emit finished(m_id, success, message, m_finalFilename, m_originalDownloadedFilename, metadata);
}
bool YtDlpWorker::retryWithoutBrowserCookiesIfCookieExtractionFailed() {
    if (m_retriedWithoutBrowserCookies) {
        return false;
    }

    const qsizetype cookieArgIndex = m_args.indexOf(QStringLiteral("--cookies-from-browser"));
    const qsizetype cookiesFileIndex = m_args.indexOf(QStringLiteral("--cookies"));
    
    if (cookieArgIndex < 0 && cookiesFileIndex < 0) {
        return false;
    }

    if (!hasBrowserCookieFailureDiagnostic()) {
        return false;
    }

    m_retriedWithoutBrowserCookies = true;
    
    removeBrowserCookieArguments();

    qWarning() << "[YtDlpWorker] Browser cookies caused yt-dlp failure for" << m_id
               << "; retrying once without cookie options.";

    QVariantMap progressData;
    progressData.insert(QStringLiteral("status"), tr("Browser cookies failed; retrying without browser cookies..."));
    progressData.insert(QStringLiteral("progress"), -1);
    emit progressUpdated(m_id, progressData);

    start();
    return true;
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
        const QString tempRoot = DownloadTempCleanup::resolveRoot(m_configManager);
        (void)DownloadTempCleanup::removeEmptyOwnedDirectory(m_id, DownloadTempCleanup::pathForId(tempRoot, m_id));

        emit finished(m_id, false, message, QString(), QString(), QVariantMap());
    }
}
