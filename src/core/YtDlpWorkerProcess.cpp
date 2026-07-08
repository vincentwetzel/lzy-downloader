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
#include <utility>

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
        const QStringView view(thumbnailPath);
        const qsizetype slashIdx = qMax(view.lastIndexOf(QLatin1Char('/')), view.lastIndexOf(QLatin1Char('\\')));
        const QStringView fileName = (slashIdx != -1) ? view.mid(slashIdx + 1) : view;
        return fileName.startsWith(id) && fileName.mid(id.length()).startsWith(QStringLiteral("_wait_thumbnail"));
    }

    void safeRemoveFile(const QString& filePath, const QString& description, int retries = 3) {
        if (filePath.isEmpty()) return;
        if (QFile::remove(filePath)) {
            qDebug() << "Cleaned up" << description << "file:" << filePath;
        } else if (QFile::exists(filePath)) {
            qWarning() << "Failed to clean up" << description << "file:" << filePath;
            if (retries > 0) {
                QTimer::singleShot(100, [filePath, description, retries]() {
                    safeRemoveFile(filePath, description, retries - 1);
                });
            } else {
                qWarning() << "Failed to clean up" << description << "file:" << filePath << "after bounded retries.";
            }
        }
    }

    void cleanupWaitThumbnail(QString& thumbnailPath, const QString& id) {
        if (isWaitThumbnail(thumbnailPath, id)) {
            safeRemoveFile(thumbnailPath, QStringLiteral("orphaned wait thumbnail"));
            thumbnailPath.clear();
        }
    }

    void cleanupEmptyUuidDir(ConfigManager* configManager, const QString& id) {
        const QString resolvedTempDir = resolveTempDirectory(configManager);
        if (!resolvedTempDir.isEmpty() && QDir(resolvedTempDir).rmdir(id)) {
            qDebug() << "Removed empty UUID directory:" << QDir(resolvedTempDir).filePath(id);
        }
    }

    [[nodiscard]] QJsonObject parseJsonData(const QByteArray& jsonData, QString* errorStr = nullptr) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            return doc.object();
        }
        if (errorStr) *errorStr = parseError.errorString();
        return {};
    }
}

void YtDlpWorker::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (this->property("proactiveCookieRetryActive").toBool()) {
        return;
    }
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

    // Check for critical errors that should always result in failure, even if yt-dlp claims to have produced a file or exited with code 1.
    bool hasCriticalError = false;
    static const QRegularExpression criticalErrorRegex(
        QStringLiteral("this video is unavailable|private video|video unavailable|this video has been removed|violating youtube's terms of service"),
        QRegularExpression::CaseInsensitiveOption
    );
    for (const QString& line : std::as_const(m_errorLines)) {
        if (line.contains(QStringLiteral("video"), Qt::CaseInsensitive) || line.contains(QStringLiteral("violating"), Qt::CaseInsensitive)) {
            if (criticalErrorRegex.match(line).hasMatch()) {
                hasCriticalError = true;
                qWarning() << "[YtDlpWorker] Detected critical error in output, forcing download failure for" << m_id;
                break;
            }
        }
    }

    const bool recoveredFromPostProcessorFailure = normalExit && exitCode != 0 && capturedFinalFileExists;
    const bool success = !hasCriticalError && ((normalExit && exitCode == 0 && !m_finalFilename.isEmpty()) || recoveredFromPostProcessorFailure);
    if (!success) {
        // Existing logging for hard failures
        qWarning() << "[YtDlpWorker] yt-dlp finished unsuccessfully for" << m_id << " (critical error detected: " << hasCriticalError << ")"
                   << "exitCode:" << exitCode
                   << "exitStatus:" << exitStatus;
        if (!m_errorLines.isEmpty()) {
            qWarning().noquote() << "[YtDlpWorker] Error output captured:" << m_errorLines.join(QLatin1Char('\n'));
        }
        if (!m_allOutputLines.isEmpty()) {
            constexpr qsizetype MAX_FALLBACK_LOG_LINES = 50; // Log up to 50 lines of context on any failure
            qWarning().noquote() << "[YtDlpWorker] Last diagnostic output (no specific errors captured):"
                                 << m_allOutputLines.mid(qMax(qsizetype(0), m_allOutputLines.size() - MAX_FALLBACK_LOG_LINES)).join(QLatin1Char('\n'));
        }
    }
    // Add specific logging for "completed with warnings" scenarios
    else if (recoveredFromPostProcessorFailure) {
        qWarning() << "[YtDlpWorker] yt-dlp exited with code" << exitCode << "after producing final media for" << m_id
                   << ". This is treated as a completion with warnings. Full output for diagnostics:";
        if (!m_errorLines.isEmpty()) {
            qWarning().noquote() << "[YtDlpWorker] Captured error/warning lines:" << m_errorLines.join(QLatin1Char('\n'));
        }
        if (!m_allOutputLines.isEmpty()) {
            qWarning().noquote() << "[YtDlpWorker] Full yt-dlp output (stdout/stderr combined):" << m_allOutputLines.join(QLatin1Char('\n'));
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
    const QString resolvedTempDir = resolveTempDirectory(m_configManager);

    if (recoveredFromPostProcessorFailure) {
        message = tr("Download completed, but thumbnail/post-processing reported a warning.");
        appendErrorPreview(m_errorLines);
        postprocessorWarning = message;
        exitCodeWarning = tr("yt-dlp exited with non-zero code %1 after producing final media.").arg(exitCode);
        qWarning() << "yt-dlp exited with code" << exitCode
                   << "after producing final media. Continuing finalization for"
                   << m_id << "at" << m_finalFilename;
    }

    if (!m_finalFilename.isEmpty()) {
        qDebug() << "Final filename captured:" << m_finalFilename;
    } else {
        qWarning() << "Could not determine final filename. Download may have failed or produced no output.";
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

        // Add a clear hint about cookie authentication if cookies were utilized or attempted
        const bool hadCookies = m_retriedWithoutBrowserCookies || m_args.contains(QStringLiteral("--cookies-from-browser")) || m_args.contains(QStringLiteral("--cookies"));
        if (hadCookies) {
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
    cleanupEmptyUuidDir(m_configManager, m_id);

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

    auto containsAny = [](const QStringList& list, const QRegularExpression& regex, const QStringList& gateKeywords = {}) {
        const bool hasGates = !gateKeywords.isEmpty();
        for (const QString& line : std::as_const(list)) {
            if (line.startsWith(QStringLiteral("[debug]"))) {
                continue;
            }
            if (hasGates) {
                bool passedGate = false;
                for (const QString& gate : gateKeywords) {
                    if (line.contains(gate, Qt::CaseInsensitive)) {
                        passedGate = true;
                        break;
                    }
                }
                if (!passedGate) continue;
            }
            if (regex.match(line).hasMatch()) return true;
        }
        return false;
    };

    static const QRegularExpression permissionRegex(
        QStringLiteral("Access is denied|Permission denied|PermissionError|database is locked|locked"),
        QRegularExpression::CaseInsensitiveOption
    );
    static const QStringList permissionGates = {QStringLiteral("Access"), QStringLiteral("Permission"), QStringLiteral("locked")};
    const bool permissionFailure = containsAny(m_errorLines, permissionRegex, permissionGates) || containsAny(m_allOutputLines, permissionRegex, permissionGates);

    static const QRegularExpression cookieRegex(
        QStringLiteral("temporary\\.sqlite|cookies\\.sqlite|yt_dlp|HTTP Error 400|Bad Request|JSON metadata"),
        QRegularExpression::CaseInsensitiveOption
    );
    static const QStringList cookieGates = {QStringLiteral("cookie"), QStringLiteral("sqlite"), QStringLiteral("yt_dlp"), QStringLiteral("HTTP"), QStringLiteral("Request"), QStringLiteral("metadata")};
    const bool browserCookieFailure = containsAny(m_errorLines, cookieRegex, cookieGates) || containsAny(m_allOutputLines, cookieRegex, cookieGates);

    static const QRegularExpression endedRegex(
        QStringLiteral("live event has ended"),
        QRegularExpression::CaseInsensitiveOption
    );
    static const QStringList endedGates = {QStringLiteral("ended")};
    const bool endedLiveExtractorFailure = containsAny(m_errorLines, endedRegex, endedGates) || containsAny(m_allOutputLines, endedRegex, endedGates);

    const bool cookieFailure = (permissionFailure && browserCookieFailure) || endedLiveExtractorFailure || browserCookieFailure;
    if (!cookieFailure) {
        return false;
    }

    m_retriedWithoutBrowserCookies = true;
    
    // Remove --cookies-from-browser and its argument
    qsizetype idx = m_args.indexOf(QStringLiteral("--cookies-from-browser"));
    if (idx != -1) {
        m_args.removeAt(idx);
        if (idx < m_args.size()) {
            m_args.removeAt(idx);
        }
    }

    // Remove --cookies and its argument
    idx = m_args.indexOf(QStringLiteral("--cookies"));
    if (idx != -1) {
        m_args.removeAt(idx);
        if (idx < m_args.size()) {
            m_args.removeAt(idx);
        }
    }

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
    if (this->property("proactiveCookieRetryActive").toBool()) {
        return;
    }
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
        cleanupEmptyUuidDir(m_configManager, m_id);

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
                const QString trimmedLine = QString::fromUtf8(chunk).trimmed();
                if (!trimmedLine.isEmpty()) {
                    handleOutputLine(trimmedLine);
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

    QString parseErrorStr;
    const QJsonObject obj = parseJsonData(jsonData, &parseErrorStr);
    if (obj.isEmpty()) {
        scheduleRetry(QStringLiteral("Failed to parse info.json as JSON or it's not an object. Error: %1").arg(parseErrorStr));
        return;
    }

    // If we successfully parsed the file, we don't need to retry anymore.
    m_infoJsonRetryCount = 0;

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
    for (const QVariant &requestedDownload : std::as_const(requestedDownloads)) {
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

    if (obj.contains(QStringLiteral("live_status"))) {
        const QString liveStatus = obj.value(QStringLiteral("live_status")).toString();
        if (liveStatus == QStringLiteral("was_live") || liveStatus == QStringLiteral("not_live") || liveStatus == QStringLiteral("post_live")) {
            updateData.insert(QStringLiteral("is_live"), false);
        } else if (liveStatus == QStringLiteral("is_live") || liveStatus == QStringLiteral("is_upcoming")) {
            updateData.insert(QStringLiteral("is_live"), true);
        }
    } else if (const QJsonValue isLiveVal = obj.value(QStringLiteral("is_live")); isLiveVal.isBool()) {
        updateData.insert(QStringLiteral("is_live"), isLiveVal.toBool());
        qDebug() << "Extracted is_live from info.json (fallback):" << isLiveVal.toBool();
    }

    // Extract thumbnail path if available from the info.json
    if (const QJsonValue thumbnailsVal = obj.value(QStringLiteral("thumbnails")); (m_thumbnailPath.isEmpty() || isWaitThumbnail(m_thumbnailPath, m_id)) && thumbnailsVal.isArray()) {
        const QJsonArray thumbnails = thumbnailsVal.toArray();
        // yt-dlp adds a "filepath" key to the thumbnail entry it downloaded.
        for (const QJsonValue &thumbValue : std::as_const(thumbnails)) {
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
