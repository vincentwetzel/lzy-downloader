#include "YtDlpWorker.h"
#include "core/ConfigManager.h"
#include "core/ProcessUtils.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTimer>
#include <QRegularExpression>
#include <QPointer>
#include <QUrl>
#include <chrono>

void YtDlpWorker::handleOutputLine(const QString &line) {
    const QString normalizedLine = normalizeConsoleLine(line);
    if (normalizedLine.isEmpty()) {
        return;
    }

    m_allOutputLines.append(normalizedLine);
    // Optimization: Batch-remove to prevent unbounded memory growth while avoiding O(N) array shifts on every single line
    constexpr qsizetype MAX_LINES = 150;
    if (m_allOutputLines.size() > MAX_LINES) {
        m_allOutputLines.remove(0, MAX_LINES - 100); // Keep the last 100 lines
    }

    emit outputReceived(m_id, normalizedLine);

    if (normalizedLine.startsWith(QStringLiteral("LZY_FINAL_PATH:"))) {
        static const QString prefix = QStringLiteral("LZY_FINAL_PATH:");
        m_finalFilename = normalizedLine.mid(prefix.length()).trimmed();
        qDebug() << "Captured precise Final Path:" << m_finalFilename;
        return; // No need to process this line further
    }

    // Check if browser cookies are causing a metadata/live status bad request or wait state block
    bool hasCookies = m_args.contains(QStringLiteral("--cookies-from-browser")) || m_args.contains(QStringLiteral("--cookies"));
    if (hasCookies && !m_retriedWithoutBrowserCookies && !this->property("proactiveCookieRetryActive").toBool() && !normalizedLine.startsWith(QStringLiteral("[debug]"))) {
        static const QRegularExpression cookieErrorRe(
            QStringLiteral("HTTP Error 400|Bad Request|Unable to download JSON metadata|not currently live|live event has ended|empty media response|cookie.*(?:invalid|expired|failed|error|rotate|refresh)|decryption|permission denied|sqlite|locked|access is denied"),
            QRegularExpression::CaseInsensitiveOption
        );
        if (cookieErrorRe.match(normalizedLine).hasMatch()) {
            if (!m_errorLines.contains(normalizedLine)) {
                m_errorLines.append(normalizedLine);
            }
            qWarning() << "[YtDlpWorker] Proactively triggering cookie retry due to detected failure line:" << normalizedLine;
            
            this->setProperty("proactiveCookieRetryActive", true);
            killProcess();

            QVariantMap progressData;
            progressData.insert(QStringLiteral("status"), tr("Browser cookies failed; retrying without browser cookies..."));
            progressData.insert(QStringLiteral("progress"), -1);
            emit progressUpdated(m_id, progressData);

            QTimer::singleShot(1000, this, [this]() {
                this->setProperty("proactiveCookieRetryActive", false);
                if (m_process) {
                    m_process->setProperty("accumulated_stderr", QString());
                }
                retryWithoutBrowserCookiesIfCookieExtractionFailed();
            });
            return;
        }
    }

    // Parse ERROR: lines from stderr for specific error types
    // Optimization: Gate expensive regex and array lookups behind fast string containment checks
    bool cookiePermissionDiagnostic = false;
    if (normalizedLine.contains(QStringLiteral("Permission"), Qt::CaseInsensitive) ||
        normalizedLine.contains(QStringLiteral("sqlite"), Qt::CaseInsensitive) ||
        normalizedLine.contains(QStringLiteral("Access"), Qt::CaseInsensitive)) {
        static const QRegularExpression cookiePermissionRegex(
            QStringLiteral("temporary\\.sqlite|PermissionError|Access is denied|Permission denied"),
            QRegularExpression::CaseInsensitiveOption
        );
        if (cookiePermissionRegex.match(normalizedLine).hasMatch() && m_args.contains(QStringLiteral("--cookies-from-browser"))) {
            cookiePermissionDiagnostic = true;
        }
    }

    const bool hasErrorPrefix = normalizedLine.startsWith(QStringLiteral("ERROR:"), Qt::CaseInsensitive) ||
                                normalizedLine.startsWith(QStringLiteral("[download] Got error:"), Qt::CaseInsensitive);

    const bool exitedWithCodeError = normalizedLine.contains(QStringLiteral("exited with code"), Qt::CaseInsensitive) &&
                                     !normalizedLine.contains(QStringLiteral("code 0"));

    if (hasErrorPrefix || exitedWithCodeError || cookiePermissionDiagnostic) {
        // Always capture error lines for diagnostics, even if they don't match a specific known error type.
        // This guarantees the terminal exit popup has context for unknown yt-dlp failures.
        m_errorLines.append(normalizedLine);
        constexpr qsizetype MAX_ERROR_LINES = 150;
        if (m_errorLines.size() > MAX_ERROR_LINES) {
            m_errorLines.remove(0, MAX_ERROR_LINES - 100);
        }

        auto emitError = [this, normalizedLine](const QString& type, const QString& msg) {
            if (!m_errorEmitted) {
                m_errorEmitted = true;
                emit ytDlpErrorDetected(m_id, type, msg, normalizedLine);
            }
        };
        static const QRegularExpression geoRegex(
            QStringLiteral("restrict|unavailable in your country|not available in your country"),
            QRegularExpression::CaseInsensitiveOption
        );
        static const QRegularExpression ageRegex(
            QStringLiteral("restrict|verify your age|confirm your age"),
            QRegularExpression::CaseInsensitiveOption
        );
        static const QRegularExpression contentRemovedRegex(
            QStringLiteral("Requested tweet is unavailable|This content is no longer available|The requested content was removed|Suspended"),
            QRegularExpression::CaseInsensitiveOption
        );
        static const QRegularExpression missingFfmpegRegex(
            QStringLiteral("ffmpeg not found|ffprobe not found|location of ffmpeg"),
            QRegularExpression::CaseInsensitiveOption
        );
        static const QRegularExpression unavailableRegex(
            QStringLiteral("unavailable|does not exist|not found"),
            QRegularExpression::CaseInsensitiveOption
        );
        static const QRegularExpression premiereRegex(
            QStringLiteral("Premieres in|Premiering in|Premiere will begin|live event will begin|is upcoming|Offline \\(expected\\)|Offline expected|waiting for premiere|waiting for livestream|Live in |Starting in "),
            QRegularExpression::CaseInsensitiveOption
        );

        // Check for cookie permission / access denied error
        if (cookiePermissionDiagnostic) {
            emitError(QStringLiteral("cookie_permission_denied"),
                tr("Failed to access browser cookies (the database may be locked by a running browser, or access was denied).\n\n"
                   "The download will automatically retry once without cookies. If it still fails, please close your browser and try again."));
        }
        // Check for private video error
        else if (normalizedLine.contains(QStringLiteral("private"), Qt::CaseInsensitive)) {
            emitError(QStringLiteral("private"), tr("This video is private and cannot be downloaded."));
        }
        // Check for geo-restriction error
        else if (normalizedLine.contains(QStringLiteral("geo"), Qt::CaseInsensitive) && geoRegex.match(normalizedLine).hasMatch()) {
            emitError(QStringLiteral("geo_restricted"), tr("This video is not available in your region."));
        }
        // Check for members-only error
        else if (normalizedLine.contains(QStringLiteral("members"), Qt::CaseInsensitive) &&
                 normalizedLine.contains(QStringLiteral("only"), Qt::CaseInsensitive)) {
            emitError(QStringLiteral("members_only"), tr("This video is exclusive to channel members."));
        }
        // Check for age-restriction error
        else if (normalizedLine.contains(QStringLiteral("age"), Qt::CaseInsensitive) && ageRegex.match(normalizedLine).hasMatch()) {
            emitError(QStringLiteral("age_restricted"), tr("This video requires age verification. Try enabling cookies from your browser."));
        }
        // Check for content removed/unavailable (e.g., deleted tweet)
        else if (contentRemovedRegex.match(normalizedLine).hasMatch()) {
            emitError(QStringLiteral("content_removed"), tr("The requested content is unavailable or has been removed by the uploader."));
        }
        // Check for No video formats found / Ghost IDs
        else if (normalizedLine.contains(QStringLiteral("No video formats found"), Qt::CaseInsensitive)) {
            emitError(QStringLiteral("no_video_formats"), tr("No video formats found for this URL.\n\n"
                                             "This usually means the site requires authentication, the media is unsupported, or the link is a 'ghost ID' that needs to be redirected.\n\n"
                                             "Try opening the link in a normal web browser to get the real URL, or check your authentication settings."));
        }
        // Check for missing FFmpeg or FFprobe runtime errors reported by yt-dlp
        else if (missingFfmpegRegex.match(normalizedLine).hasMatch()) {
            emitError(QStringLiteral("missing_ffmpeg"),
                tr("FFmpeg or FFprobe was not found by yt-dlp.\n\n"
                   "To fix this, please configure the correct paths in Advanced Settings -> External Tools, or install them via your system package manager."));
        }
        // Check for generic unavailable video error (guarding against FFmpeg's "Option not found" or input errors)
        else if (unavailableRegex.match(normalizedLine).hasMatch() &&
                 !normalizedLine.contains(QStringLiteral("option not found"), Qt::CaseInsensitive) &&
                 !normalizedLine.contains(QStringLiteral("error opening input"), Qt::CaseInsensitive)) {
            emitError(QStringLiteral("unavailable"), tr("This video is unavailable or has been removed."));
        } else if (normalizedLine.contains(QStringLiteral("Option ignore_editlist not found"), Qt::CaseInsensitive)) {
            emitError(QStringLiteral("ffmpeg_option_not_found"),
                      tr("Your FFmpeg version does not support the '-ignore_editlist' option, which is required for accurate SponsorBlock/section cuts.\n\n"
                         "Please update FFmpeg to a recent version (e.g., 6.0 or newer) from the External Tools settings."));
        } else if (normalizedLine.contains(QStringLiteral("Error opening input files"), Qt::CaseInsensitive) ||
                   normalizedLine.contains(QStringLiteral("Option not found"), Qt::CaseInsensitive)) {
            // Generic FFmpeg post-processing failure, but not the specific ignore_editlist issue.
            // This is still a failure of a post-processing step, so it should be an error.
            emitError(QStringLiteral("ffmpeg_postprocessor_error"),
                      tr("FFmpeg post-processing failed: Error opening input files or an unknown option was not found. This may indicate a problem with the downloaded media or FFmpeg configuration."));
        }
        // Check for scheduled livestream/premiere
        else if (premiereRegex.match(normalizedLine).hasMatch()) {
            emitError(QStringLiteral("scheduled_livestream"),
                tr("This video is a scheduled livestream or premiere that has not started yet.\n\n"
                   "Would you like to wait for the video to begin and download it automatically?"));
        }
    } else if (normalizedLine.startsWith(QStringLiteral("WARNING:"))) { // Separate handling for WARNING: lines
        // Filter out specific non-critical warnings that don't need to be surfaced to the user
        if (normalizedLine.contains(QStringLiteral("The extractor specified to use impersonation for this download"), Qt::CaseInsensitive)) {
            // Silently tag this on the worker so we can append an optimization recommendation tip to the completion metadata
            this->setProperty("missing_impersonation", true);
            return;
        }
        // Add other WARNING lines to m_errorLines for diagnostics, even if not a specific error type.
        else {
            m_errorLines.append(normalizedLine);
            // Optimization: Prevent unbounded memory growth if yt-dlp spams warning lines
            constexpr qsizetype MAX_WARNING_LINES = 150;
            if (m_errorLines.size() > MAX_WARNING_LINES) {
                m_errorLines.remove(0, MAX_WARNING_LINES - 100);
            }
        }

        // Parse specific WARNING: lines that should be surfaced as errors/guidance to the user
        if (normalizedLine.contains(QStringLiteral("YouTube account cookies are no longer valid"), Qt::CaseInsensitive)) {
            if (!m_errorEmitted) {
                emit ytDlpErrorDetected(m_id, QStringLiteral("invalid_cookies"),
                    tr("Your browser cookies are no longer valid (they may have expired or been rotated by YouTube).\n\n"
                       "To fix this: Open your configured browser, log out of YouTube, log back in, and try your download again."), normalizedLine);
            }
        }
    }

    // Parse lifecycle / post-processing operations so the UI can follow the real stage
    if (handleLifecycleStatusLine(normalizedLine)) {
        return;
    }

    if (handleAria2CommandLine(normalizedLine)) {
        return;
    }

    // If we are waiting for a scheduled livestream, fetch metadata in the background so the UI
    // can show the title and thumbnail instead of just the URL during the long wait.
    if (normalizedLine.startsWith(QStringLiteral("[wait]"), Qt::CaseInsensitive) || normalizedLine.startsWith(QStringLiteral("[download] Waiting for video"), Qt::CaseInsensitive)) {

        QString statusText = tr("Waiting for livestream to start...");
        if (normalizedLine.startsWith(QStringLiteral("[wait] Remaining time until next attempt:"), Qt::CaseInsensitive)) {
            static const QString prefix = QStringLiteral("[wait] Remaining time until next attempt: ");
            statusText = tr("Next check in %1").arg(normalizedLine.mid(prefix.length()).trimmed());
        }

        // Ensure indeterminate progress is emitted before metadata routines potentially return early
        QVariantMap initialProgressData;
        initialProgressData.insert(QStringLiteral("progress"), -1); // Indeterminate state
        initialProgressData.insert(QStringLiteral("status"), statusText);
        if (!m_videoTitle.isEmpty()) {
            initialProgressData.insert(QStringLiteral("title"), m_videoTitle);
        }
        if (!m_thumbnailPath.isEmpty()) {
            initialProgressData.insert(QStringLiteral("thumbnail_path"), m_thumbnailPath);
        }
        emit progressUpdated(m_id, initialProgressData);

        if ((m_videoTitle.isEmpty() || m_thumbnailPath.isEmpty()) && !property("fetchingPreWaitMetadata").toBool()) {
            setProperty("fetchingPreWaitMetadata", true);

            auto fetchThumbnail = [this](const QString& thumbUrl) {
                QNetworkAccessManager *manager = new QNetworkAccessManager(this);
                QNetworkRequest request{QUrl(thumbUrl)};
                request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
                request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LzyDownloader"));
                constexpr int thumbTimeoutMs = 15000;
                request.setTransferTimeout(thumbTimeoutMs);
                QNetworkReply *reply = manager->get(request);
                connect(reply, &QNetworkReply::finished, this, [this, reply, manager]() {
                    if (reply->error() == QNetworkReply::NoError) {
                        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                        if (statusCode >= 200 && statusCode < 300) {
                            QString tempDir;
                            if (m_configManager) {
                                tempDir = m_configManager->get(QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory")).toString();
                                if (tempDir.isEmpty()) {
                                    const QString completedDir = m_configManager->get(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory")).toString();
                                    if (!completedDir.isEmpty()) {
                                        tempDir = QDir(completedDir).filePath(QStringLiteral("temp_downloads"));
                                    }
                                }
                            }

                            if (!tempDir.isEmpty()) {
                                if (!QDir().mkpath(tempDir)) {
                                    qWarning() << "[YtDlpWorker] Failed to create temp directory for pre-wait thumbnail:" << tempDir;
                                }
                                const QString ext = reply->url().path().endsWith(QStringLiteral(".webp"), Qt::CaseInsensitive) ? QStringLiteral(".webp") : QStringLiteral(".jpg");
                                const QString newThumbPath = QDir(tempDir).filePath(QStringLiteral("%1_wait_thumbnail%2").arg(m_id, ext));
                                QFile file(newThumbPath);
                                if (file.open(QIODevice::WriteOnly)) {
                                    const QByteArray data = reply->readAll();
                                    if (!data.isEmpty()) {
                                        if (file.write(data) == data.size()) {
                                            file.close();
                                            m_thumbnailPath = QDir::toNativeSeparators(newThumbPath);
                                            qDebug() << "[YtDlpWorker] Pre-wait thumbnail downloaded to:" << m_thumbnailPath;

                                            QVariantMap pd;
                                            pd.insert(QStringLiteral("progress"), -1);
                                            pd.insert(QStringLiteral("status"), tr("Waiting for livestream to start..."));
                                            pd.insert(QStringLiteral("title"), m_videoTitle);
                                            pd.insert(QStringLiteral("thumbnail_path"), m_thumbnailPath);
                                            emit progressUpdated(m_id, pd);
                                        } else {
                                            qWarning() << "[YtDlpWorker] Failed to write pre-wait thumbnail:" << file.errorString();
                                            file.close();
                                            file.remove();
                                        }
                                    } else {
                                        file.close();
                                        if (!file.remove()) {
                                            qWarning() << "[YtDlpWorker] Failed to remove empty pre-wait thumbnail file:" << newThumbPath;
                                        }
                                    }
                                } else {
                                    qWarning() << "[YtDlpWorker] Failed to open temp file for pre-wait thumbnail:" << file.errorString();
                                }
                            } else {
                                qWarning() << "[YtDlpWorker] Failed to resolve temporary directory for pre-wait thumbnail.";
                            }
                        } else {
                            qWarning() << "[YtDlpWorker] Failed to download pre-wait thumbnail, HTTP status:" << statusCode;
                        }
                    } else {
                        qWarning() << "[YtDlpWorker] Failed to download pre-wait thumbnail:" << reply->errorString();
                    }
                    reply->deleteLater();
                    manager->deleteLater();
                });
            };

            QString url;
            for (auto it = m_args.crbegin(); it != m_args.crend(); ++it) {
                if (it->startsWith(QStringLiteral("http"), Qt::CaseInsensitive)) {
                    url = *it;
                    break;
                }
            }
            if (url.isEmpty() && !m_args.isEmpty() && !m_args.last().startsWith(QLatin1Char('-'))) {
                url = m_args.last();
            }

            qDebug() << "[YtDlpWorker] Detected [wait] state. Fetching pre-wait metadata via yt-dlp in background...";
            const QString ytDlpPath = ProcessUtils::findBinary(QStringLiteral("yt-dlp"), m_configManager).path;
            if (ytDlpPath.isEmpty()) {
                qWarning() << "[YtDlpWorker] Cannot fetch pre-wait metadata: yt-dlp path is empty.";
                return;
            }

            QProcess *fetchProcess = new QProcess(this);
            QPointer<QProcess> safeProcess(fetchProcess);
            ProcessUtils::setProcessEnvironment(*fetchProcess);
            QStringList fetchArgs;

            fetchArgs << QStringLiteral("--dump-single-json") << QStringLiteral("--flat-playlist") << QStringLiteral("--ignore-errors") << url;
            const qsizetype cookieIdx = m_args.indexOf(QStringLiteral("--cookies-from-browser"));
            if (cookieIdx != -1 && cookieIdx + 1 < m_args.size()) {
                fetchArgs << QStringLiteral("--cookies-from-browser") << m_args[cookieIdx + 1];
            }

            qDebug() << "[YtDlpWorker] Pre-wait fetch command:" << ytDlpPath << fetchArgs;
            connect(fetchProcess, &QProcess::finished, this, [this, safeProcess, fetchThumbnail](int exitCode, QProcess::ExitStatus) {
                    if (!safeProcess) return;
                    const QByteArray jsonData = safeProcess->readAllStandardOutput();
                    QJsonParseError parseError;
                    const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
                    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                        const QJsonObject obj = doc.object();
                        m_fullMetadata = obj.toVariantMap();
                        const QJsonValue titleVal = obj.value(QStringLiteral("title"));
                        if (titleVal.isString()) {
                            m_videoTitle = titleVal.toString();
                            qDebug() << "[YtDlpWorker] Pre-wait title fetched:" << m_videoTitle;

                            // Immediately update the UI with the title before we wait for the thumbnail
                            QVariantMap progressData;
                            progressData.insert(QStringLiteral("progress"), -1);
                            progressData.insert(QStringLiteral("status"), tr("Waiting for livestream to start..."));
                            progressData.insert(QStringLiteral("title"), m_videoTitle);
                            if (!m_thumbnailPath.isEmpty()) {
                                progressData.insert(QStringLiteral("thumbnail_path"), m_thumbnailPath);
                            }
                            emit progressUpdated(m_id, progressData);
                        }

                        QString thumbUrl;
                        if (const QJsonValue thumbnailsVal = obj.value(QStringLiteral("thumbnails")); thumbnailsVal.isArray()) {
                            if (const QJsonArray thumbs = thumbnailsVal.toArray(); !thumbs.isEmpty()) {
                                if (const QJsonValue lastThumb = thumbs.last(); lastThumb.isObject()) {
                                    if (const QJsonValue urlVal = lastThumb.toObject().value(QStringLiteral("url")); urlVal.isString()) {
                                        thumbUrl = urlVal.toString();
                                    }
                                }
                            }
                        } else if (const QJsonValue thumbnailVal = obj.value(QStringLiteral("thumbnail")); thumbnailVal.isString()) {
                            thumbUrl = thumbnailVal.toString();
                        }

                        if (!thumbUrl.isEmpty()) {
                            qDebug() << "[YtDlpWorker] Pre-wait thumbnail URL found:" << thumbUrl;
                            fetchThumbnail(thumbUrl);
                        }
                    } else {
                        qWarning() << "[YtDlpWorker] Pre-wait metadata fetch failed or returned invalid JSON. Exit code:" << exitCode;
                        qWarning() << "[YtDlpWorker] Stderr:" << safeProcess->readAllStandardError();
                    }
                safeProcess->deleteLater();
            });

            connect(fetchProcess, &QProcess::errorOccurred, this, [safeProcess](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart && safeProcess) {
                    qWarning() << "[YtDlpWorker] Pre-wait metadata fetch process failed to start.";
                    safeProcess->deleteLater();
                }
            });

            fetchProcess->start(ytDlpPath, fetchArgs);

            QTimer *watchdog = new QTimer(fetchProcess);
            watchdog->setSingleShot(true);
            connect(watchdog, &QTimer::timeout, fetchProcess, [fetchProcess]() {
                qWarning() << "[YtDlpWorker] Pre-wait metadata fetch timed out. Killing process.";
                if (fetchProcess->state() != QProcess::NotRunning) {
                    ProcessUtils::terminateProcessTree(fetchProcess);
                    fetchProcess->kill();
                }
            });
            watchdog->start(std::chrono::seconds(30)); // 30 seconds
        }
    return;
    }

    if (normalizedLine.startsWith(QStringLiteral("[ThumbnailsConvertor]"))) {
        static const QRegularExpression thumbnailRegex(QStringLiteral("\\[ThumbnailsConvertor\\] Converting thumbnail \"([^\"]+)\" to (\\w+)"));
        const QRegularExpressionMatch thumbnailMatch = thumbnailRegex.match(normalizedLine);
        if (thumbnailMatch.hasMatch()) {
            QString originalPath = thumbnailMatch.captured(1);
            const QString format = thumbnailMatch.captured(2);

            const qsizetype extIndex = originalPath.lastIndexOf(QLatin1Char('.'));
            if (extIndex != -1) {
                originalPath.truncate(extIndex + 1);
                originalPath.append(format);
            }
            m_thumbnailPath = QDir::toNativeSeparators(originalPath);
            QVariantMap updateData;
            updateData.insert(QStringLiteral("thumbnail_path"), m_thumbnailPath);
            qDebug() << "[LOG] YtDlpWorker: Found converted thumbnail path for" << m_id << ":" << m_thumbnailPath;
            emit progressUpdated(m_id, updateData);
        }
        return;
    }

    if (normalizedLine.startsWith(QStringLiteral("[info]"))) {
        if (normalizedLine.contains(QStringLiteral("format(s):"))) {
            static const QRegularExpression formatListRegex(QStringLiteral("^\\[info\\].*Downloading\\s+\\d+\\s+format\\(s\\):\\s+(.+)$"));
            const QRegularExpressionMatch formatListMatch = formatListRegex.match(normalizedLine);
            if (formatListMatch.hasMatch()) {
                inferRequestedTransfersFromFormatList(formatListMatch.captured(1).trimmed());
            }
        }

        // 1. Capture the info.json file path and store it, then initiate retry mechanism
        if (normalizedLine.contains(QStringLiteral(".info.json"))) {
            static const QRegularExpression infoJsonRegex(QStringLiteral("\\[info\\] (?:Writing video metadata as JSON to|Video metadata is already present in|Video description metadata is already present in):\\s*(.*\\.info\\.json)"));
            const QRegularExpressionMatch infoJsonMatch = infoJsonRegex.match(normalizedLine);
            if (infoJsonMatch.hasMatch()) {
                m_infoJsonPath = QDir::toNativeSeparators(infoJsonMatch.captured(1).trimmed());
                qDebug() << "Detected info.json path and initiating retry mechanism:" << m_infoJsonPath;
                m_infoJsonRetryCount = 0; // Reset retry count for a new file
                readInfoJsonWithRetry(); // Start the retry mechanism
            }
        }

        // Capture raw thumbnail paths (in case they don't need conversion and bypass ThumbnailsConvertor)
        if (normalizedLine.contains(QStringLiteral("thumbnail"))) {
            static const QRegularExpression rawThumbnailRegex(QStringLiteral("\\[info\\] (?:Writing video thumbnail.* to|Video thumbnail.* is already present in):\\s+(.+)$"));
            const QRegularExpressionMatch rawThumbnailMatch = rawThumbnailRegex.match(normalizedLine);
            if (rawThumbnailMatch.hasMatch()) {
                m_thumbnailPath = QDir::toNativeSeparators(rawThumbnailMatch.captured(1).trimmed());
                QVariantMap updateData;
                updateData.insert(QStringLiteral("thumbnail_path"), m_thumbnailPath);
                qDebug() << "[LOG] YtDlpWorker: Found raw thumbnail path for" << m_id << ":" << m_thumbnailPath;
                emit progressUpdated(m_id, updateData);
            }
        }

        // Capture subtitle sidecar paths so they are tracked for cleanup
        if (normalizedLine.contains(QStringLiteral("subtitle"))) {
            static const QRegularExpression subtitleRegex(QStringLiteral("\\[info\\] (?:Writing video subtitles to|Video subtitles are already present in):\\s+(.+)$"));
            const QRegularExpressionMatch subtitleMatch = subtitleRegex.match(normalizedLine);
            if (subtitleMatch.hasMatch()) {
                const QString subtitlePath = subtitleMatch.captured(1).trimmed();
                updateTransferTarget(subtitlePath);
                emitStatusUpdate(statusForCurrentTransfer());
            }
        }
        return;
    }

    if (normalizedLine.startsWith(QStringLiteral("FILE:"))) {
        const QString filename = normalizedLine.mid(5).trimmed(); // Length of "FILE:"
        const QString previousTarget = m_currentTransferTarget;
        const QString previousStatus = m_currentTransferStatus;
        updateTransferTarget(filename);
        if (m_currentTransferTarget != previousTarget || m_currentTransferStatus != previousStatus) {
            emitStatusUpdate(statusForCurrentTransfer());
        }
        return;
    }

    if (normalizedLine.startsWith(QStringLiteral("[download]"))) {
        if (normalizedLine.contains(QStringLiteral("Destination:"))) {
            static const QRegularExpression destinationRegex(QStringLiteral("\\[download\\]\\s+Destination:\\s+(.+)"));
            const QRegularExpressionMatch destinationMatch = destinationRegex.match(normalizedLine);
            if (destinationMatch.hasMatch()) {
                const QString filename = destinationMatch.captured(1).trimmed();
                updateTransferTarget(filename);
                emitStatusUpdate(statusForCurrentTransfer());
                if (!m_currentTransferIsAuxiliary && m_originalDownloadedFilename.isEmpty()) {
                    m_originalDownloadedFilename = filename;
                }
                return;
            }
        }

        if (normalizedLine.contains(QStringLiteral("Total fragments:"))) {
            static const QRegularExpression totalFragmentsRegex(QStringLiteral("\\[download\\]\\s+Total fragments:\\s+(\\d+)"));
            const QRegularExpressionMatch totalFragmentsMatch = totalFragmentsRegex.match(normalizedLine);
            if (totalFragmentsMatch.hasMatch()) {
                const QString segmentCount = totalFragmentsMatch.captured(1);
                const QString transferStatus = statusForCurrentTransfer();
                QString segmentStatus;
                if (transferStatus.contains(QStringLiteral("audio"), Qt::CaseInsensitive)) {
                    segmentStatus = tr("Downloading %1 audio segment(s)...").arg(segmentCount);
                } else if (transferStatus.contains(QStringLiteral("video"), Qt::CaseInsensitive)) {
                    segmentStatus = tr("Downloading %1 video segment(s)...").arg(segmentCount);
                } else {
                    segmentStatus = tr("Downloading %1 segment(s)...").arg(segmentCount);
                }
                emitStatusUpdate(segmentStatus);
                return;
            }
        }
    }

    if (!parseYtDlpProgressLine(normalizedLine)) {
        parseAria2ProgressLine(normalizedLine);
    }
}

QString YtDlpWorker::normalizeConsoleLine(const QString &line) const {
    // High-frequency optimization: bypass the regex engine entirely if no ANSI characters are present
    if (!line.contains(QLatin1Char('\x1B'))) {
        return line; // Already trimmed by upstream buffer parser
    }

    QString normalized = line;
    static const QRegularExpression ansiRegex(QStringLiteral("\\x1B\\[[0-9;]*[A-Za-z]"));
    normalized.remove(ansiRegex);
    return normalized.trimmed();
}
