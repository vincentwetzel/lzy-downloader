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
#include <QUrl>
#include <chrono>

void YtDlpWorker::handleOutputLine(const QString &line) {
    const QString normalizedLine = normalizeConsoleLine(line);
    if (normalizedLine.isEmpty()) {
        return;
    }

    m_allOutputLines.append(normalizedLine); // Store all output lines

    emit outputReceived(m_id, normalizedLine);

    if (normalizedLine.startsWith(QStringLiteral("LZY_FINAL_PATH:"))) {
        m_finalFilename = normalizedLine.mid(15).trimmed(); // 15 is length of "LZY_FINAL_PATH:"
        qDebug() << "Captured precise Final Path:" << m_finalFilename;
        return; // No need to process this line further
    }

    // Parse ERROR: lines from stderr for specific error types
    if (normalizedLine.startsWith(QStringLiteral("ERROR:"))) {
        m_errorLines.append(normalizedLine);
        
        auto emitError = [&](const QString& type, const QString& msg) {
            if (!m_errorEmitted) {
                m_errorEmitted = true;
                emit ytDlpErrorDetected(m_id, type, msg, normalizedLine);
            }
        };

        // Check for private video error
        if (normalizedLine.contains(QStringLiteral("private"), Qt::CaseInsensitive) || 
            normalizedLine.contains(QStringLiteral("This video is private"), Qt::CaseInsensitive)) {
            emitError(QStringLiteral("private"), tr("This video is private and cannot be downloaded."));
        }
        // Check for unavailable video error
        else if (normalizedLine.contains(QStringLiteral("unavailable"), Qt::CaseInsensitive) ||
                 normalizedLine.contains(QStringLiteral("Video is unavailable"), Qt::CaseInsensitive) ||
                 normalizedLine.contains(QStringLiteral("This video is no longer available"), Qt::CaseInsensitive) ||
                 normalizedLine.contains(QStringLiteral("does not exist"), Qt::CaseInsensitive)) {
            emitError(QStringLiteral("unavailable"), tr("This video is unavailable or has been removed."));
        }
        // Check for geo-restriction error
        else if (normalizedLine.contains(QStringLiteral("geo"), Qt::CaseInsensitive) && 
                 (normalizedLine.contains(QStringLiteral("restrict"), Qt::CaseInsensitive) ||
                  normalizedLine.contains(QStringLiteral("unavailable in your country"), Qt::CaseInsensitive))) {
            emitError(QStringLiteral("geo_restricted"), tr("This video is not available in your region."));
        }
        // Check for members-only error
        else if (normalizedLine.contains(QStringLiteral("members"), Qt::CaseInsensitive) &&
                 normalizedLine.contains(QStringLiteral("only"), Qt::CaseInsensitive)) {
            emitError(QStringLiteral("members_only"), tr("This video is exclusive to channel members."));
        }
        // Check for age-restriction error
        else if (normalizedLine.contains(QStringLiteral("age"), Qt::CaseInsensitive) &&
                 (normalizedLine.contains(QStringLiteral("restrict"), Qt::CaseInsensitive) ||
                  normalizedLine.contains(QStringLiteral("verify your age"), Qt::CaseInsensitive) ||
                  normalizedLine.contains(QStringLiteral("confirm your age"), Qt::CaseInsensitive))) {
            emitError(QStringLiteral("age_restricted"), tr("This video requires age verification. Try enabling cookies from your browser."));
        }
        // Check for content removed/unavailable (e.g., deleted tweet)
        else if (normalizedLine.contains(QStringLiteral("Requested tweet is unavailable"), Qt::CaseInsensitive) ||
                 normalizedLine.contains(QStringLiteral("This content is no longer available"), Qt::CaseInsensitive) ||
                 normalizedLine.contains(QStringLiteral("The requested content was removed"), Qt::CaseInsensitive) ||
                 normalizedLine.contains(QStringLiteral("Suspended"), Qt::CaseInsensitive)) {
            emitError(QStringLiteral("content_removed"), tr("The requested content is unavailable or has been removed by the uploader."));
        }
        // Check for No video formats found / Ghost IDs
        else if (normalizedLine.contains(QStringLiteral("No video formats found"), Qt::CaseInsensitive)) {
            emitError(QStringLiteral("no_video_formats"), tr("No video formats found for this URL.\n\n"
                                             "This usually means the site requires authentication, the media is unsupported, or the link is a 'ghost ID' that needs to be redirected.\n\n"
                                             "Try opening the link in a normal web browser to get the real URL, or check your authentication settings."));
        }
        // Check for scheduled livestream/premiere
        else if (normalizedLine.contains(QStringLiteral("Premieres in"), Qt::CaseInsensitive) ||
                 normalizedLine.contains(QStringLiteral("Premiering in"), Qt::CaseInsensitive) ||
                 normalizedLine.contains(QStringLiteral("Premiere will begin"), Qt::CaseInsensitive) ||
                 normalizedLine.contains(QStringLiteral("live event will begin"), Qt::CaseInsensitive) ||
                 normalizedLine.contains(QStringLiteral("is upcoming"), Qt::CaseInsensitive) ||
                 normalizedLine.contains(QStringLiteral("Offline (expected)"), Qt::CaseInsensitive) ||
                 normalizedLine.contains(QStringLiteral("Offline expected"), Qt::CaseInsensitive) ||
                 normalizedLine.contains(QStringLiteral("waiting for premiere"), Qt::CaseInsensitive) ||
                 normalizedLine.contains(QStringLiteral("waiting for livestream"), Qt::CaseInsensitive) ||
                 normalizedLine.contains(QStringLiteral("Live in "), Qt::CaseInsensitive) ||
                 normalizedLine.contains(QStringLiteral("Starting in "), Qt::CaseInsensitive)) {
            emitError(QStringLiteral("scheduled_livestream"), 
                tr("This video is a scheduled livestream or premiere that has not started yet.\n\n"
                   "Would you like to wait for the video to begin and download it automatically?"));
        }
    }

    // Parse lifecycle / post-processing operations so the UI can follow the real stage
    if (handleLifecycleStatusLine(normalizedLine)) {
        return;
    }

    static const QRegularExpression formatListRegex(QStringLiteral(R"(^\[info\].*Downloading\s+\d+\s+format\(s\):\s+(.+)$)"));
    const QRegularExpressionMatch formatListMatch = formatListRegex.match(normalizedLine);
    if (formatListMatch.hasMatch()) {
        inferRequestedTransfersFromFormatList(formatListMatch.captured(1).trimmed());
    }

    if (handleAria2CommandLine(normalizedLine)) {
        return;
    }

    // If we are waiting for a scheduled livestream, fetch metadata in the background so the UI 
    // can show the title and thumbnail instead of just the URL during the long wait.
    if (normalizedLine.startsWith(QStringLiteral("[wait]"), Qt::CaseInsensitive) || normalizedLine.startsWith(QStringLiteral("[download] Waiting for video"), Qt::CaseInsensitive)) {
        
        QString statusText = tr("Waiting for livestream to start...");
        if (normalizedLine.startsWith(QStringLiteral("[wait] Remaining time until next attempt:"), Qt::CaseInsensitive)) {
            const QString prefix = QStringLiteral("[wait] Remaining time until next attempt: ");
            statusText = tr("Next check in %1").arg(normalizedLine.mid(prefix.length()).trimmed());
        }

        // Ensure indeterminate progress is emitted before metadata routines potentially return early
        QVariantMap initialProgressData;
        initialProgressData[QStringLiteral("progress")] = -1; // Indeterminate state
        initialProgressData[QStringLiteral("status")] = statusText;
        if (!m_videoTitle.isEmpty()) {
            initialProgressData[QStringLiteral("title")] = m_videoTitle;
        }
        if (!m_thumbnailPath.isEmpty()) {
            initialProgressData[QStringLiteral("thumbnail_path")] = m_thumbnailPath;
        }
        emit progressUpdated(m_id, initialProgressData);

        if (m_videoTitle.isEmpty() && !property("fetchingPreWaitMetadata").toBool()) {
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
                        QString tempDir = m_configManager->get(QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory")).toString();
                        QDir().mkpath(tempDir);
                        QString ext = reply->url().toString().contains(QStringLiteral(".webp"), Qt::CaseInsensitive) ? QStringLiteral(".webp") : QStringLiteral(".jpg");
                        QString newThumbPath = QDir(tempDir).filePath(QStringLiteral("%1_wait_thumbnail%2").arg(m_id, ext));
                        QFile file(newThumbPath);
                        if (file.open(QIODevice::WriteOnly)) {
                            QByteArray data = reply->readAll();
                            if (!data.isEmpty()) {
                                file.write(data);
                                file.close();
                                m_thumbnailPath = QDir::toNativeSeparators(newThumbPath);
                                qDebug() << "[YtDlpWorker] Pre-wait thumbnail downloaded to:" << m_thumbnailPath;

                                QVariantMap pd;
                                pd[QStringLiteral("progress")] = -1;
                                pd[QStringLiteral("status")] = tr("Waiting for livestream to start...");
                                pd[QStringLiteral("title")] = m_videoTitle;
                                pd[QStringLiteral("thumbnail_path")] = m_thumbnailPath;
                                emit progressUpdated(m_id, pd);
                            } else {
                                file.close();
                                file.remove();
                            }
                        }
                    } else {
                        qWarning() << "[YtDlpWorker] Failed to download pre-wait thumbnail:" << reply->errorString();
                    }
                    reply->deleteLater();
                    manager->deleteLater();
                });
            };

            QString url;
            for (const QString &arg : m_args) {
                if (arg.startsWith(QStringLiteral("http"))) { url = arg; break; }
            }
            if (url.isEmpty()) {
                for (const QString &arg : m_args) {
                    // Grab the first non-flag argument as a fallback URL
                    if (!arg.startsWith(QLatin1Char('-'))) { url = arg; break; }
                }
            }
            
            // Fast-path: YouTube oEmbed API avoids spawning a yt-dlp process and bypassing 
            // the ExtractorError completely for upcoming livestreams.
            if (url.contains(QStringLiteral("youtube.com")) || url.contains(QStringLiteral("youtu.be"))) {
                qDebug() << "[YtDlpWorker] Detected [wait] state. Using YouTube oEmbed API for pre-wait metadata...";
                QNetworkAccessManager *manager = new QNetworkAccessManager(this);
                QUrl oembedUrl(QStringLiteral("https://www.youtube.com/oembed?url=%1&format=json").arg(url));
                QNetworkRequest request(oembedUrl);
                request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
                request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LzyDownloader"));
                constexpr int oembedTimeoutMs = 15000;
                request.setTransferTimeout(oembedTimeoutMs);
                QNetworkReply *reply = manager->get(request);
                connect(reply, &QNetworkReply::finished, this, [this, reply, manager, fetchThumbnail]() {
                    if (reply->error() == QNetworkReply::NoError) {
                        QJsonParseError parseError;
                        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
                        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                            QJsonObject obj = doc.object();
                            m_videoTitle = obj.value(QStringLiteral("title")).toString();
                            QString thumbUrl = obj.value(QStringLiteral("thumbnail_url")).toString();
                            
                            qDebug() << "[YtDlpWorker] oEmbed title:" << m_videoTitle << "thumb:" << thumbUrl;
                            
                            QVariantMap progressData;
                            progressData[QStringLiteral("progress")] = -1;
                            progressData[QStringLiteral("status")] = tr("Waiting for livestream to start...");
                            progressData[QStringLiteral("title")] = m_videoTitle;
                            if (!m_thumbnailPath.isEmpty()) {
                                progressData[QStringLiteral("thumbnail_path")] = m_thumbnailPath;
                            }
                            emit progressUpdated(m_id, progressData);

                            if (!thumbUrl.isEmpty() && m_thumbnailPath.isEmpty()) {
                                fetchThumbnail(thumbUrl);
                            }
                            manager->deleteLater();
                        } else {
                            manager->deleteLater();
                        }
                    } else {
                        qWarning() << "[YtDlpWorker] oEmbed API failed:" << reply->errorString();
                        manager->deleteLater();
                    }
                    reply->deleteLater();
                });
                return;
            }

            // Fallback for non-YouTube sites
            qDebug() << "[YtDlpWorker] Detected [wait] state. Fetching pre-wait metadata via yt-dlp in background...";
            QProcess *fetchProcess = new QProcess(this);
            ProcessUtils::setProcessEnvironment(*fetchProcess);
            
            QString ytDlpPath = ProcessUtils::findBinary(QStringLiteral("yt-dlp"), m_configManager).path;
            QStringList fetchArgs;
            
            fetchArgs << QStringLiteral("--dump-single-json") << QStringLiteral("--flat-playlist") << QStringLiteral("--ignore-errors") << url;
            int cookieIdx = m_args.indexOf(QStringLiteral("--cookies-from-browser"));
            if (cookieIdx != -1 && cookieIdx + 1 < m_args.size()) {
                fetchArgs << QStringLiteral("--cookies-from-browser") << m_args[cookieIdx + 1];
            }

            qDebug() << "[YtDlpWorker] Pre-wait fetch command:" << ytDlpPath << fetchArgs;
            connect(fetchProcess, &QProcess::finished, this, [this, fetchProcess, fetchThumbnail](int exitCode, QProcess::ExitStatus) {
                    QByteArray jsonData = fetchProcess->readAllStandardOutput();
                    QJsonParseError parseError;
                    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
                    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                        QJsonObject obj = doc.object();
                        m_fullMetadata = obj.toVariantMap();
                        if (obj.contains(QStringLiteral("title")) && obj.value(QStringLiteral("title")).isString()) {
                            m_videoTitle = obj.value(QStringLiteral("title")).toString();
                            qDebug() << "[YtDlpWorker] Pre-wait title fetched:" << m_videoTitle;
                            
                            // Immediately update the UI with the title before we wait for the thumbnail
                            QVariantMap progressData;
                            progressData[QStringLiteral("progress")] = -1;
                            progressData[QStringLiteral("status")] = tr("Waiting for livestream to start...");
                            progressData[QStringLiteral("title")] = m_videoTitle;
                            if (!m_thumbnailPath.isEmpty()) {
                                progressData[QStringLiteral("thumbnail_path")] = m_thumbnailPath;
                            }
                            emit progressUpdated(m_id, progressData);
                        }
                        
                        QString thumbUrl;
                        if (obj.contains(QStringLiteral("thumbnails")) && obj.value(QStringLiteral("thumbnails")).isArray()) {
                            QJsonArray thumbs = obj.value(QStringLiteral("thumbnails")).toArray();
                            if (!thumbs.isEmpty()) {
                                thumbUrl = thumbs.last().toObject().value(QStringLiteral("url")).toString();
                            }
                        } else if (obj.contains(QStringLiteral("thumbnail"))) {
                            thumbUrl = obj.value(QStringLiteral("thumbnail")).toString();
                        }

                        if (!thumbUrl.isEmpty()) {
                            qDebug() << "[YtDlpWorker] Pre-wait thumbnail URL found:" << thumbUrl;
                            fetchThumbnail(thumbUrl);
                        }
                    } else {
                        qWarning() << "[YtDlpWorker] Pre-wait metadata fetch failed or returned invalid JSON. Exit code:" << exitCode;
                        qWarning() << "[YtDlpWorker] Stderr:" << fetchProcess->readAllStandardError();
                    }
                fetchProcess->deleteLater();
            });
            
            connect(fetchProcess, &QProcess::errorOccurred, this, [fetchProcess](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    fetchProcess->deleteLater();
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

    static const QRegularExpression thumbnailRegex(QStringLiteral("\\[ThumbnailsConvertor\\] Converting thumbnail \"([^\"]+)\" to (\\w+)"));
    QRegularExpressionMatch thumbnailMatch = thumbnailRegex.match(normalizedLine);
    if (thumbnailMatch.hasMatch()) {
        QString originalPath = thumbnailMatch.captured(1);
        QString format = thumbnailMatch.captured(2);
        
        int extIndex = originalPath.lastIndexOf(QLatin1Char('.'));
        if (extIndex != -1) {
            originalPath = originalPath.left(extIndex + 1) + format;
        }
        m_thumbnailPath = QDir::toNativeSeparators(originalPath);
        QVariantMap updateData;
        updateData[QStringLiteral("thumbnail_path")] = m_thumbnailPath;
        qDebug() << "[LOG] YtDlpWorker: Found converted thumbnail path for" << m_id << ":" << m_thumbnailPath;
        emit progressUpdated(m_id, updateData);
    }

    // 1. Capture the info.json file path and store it, then initiate retry mechanism
    static const QRegularExpression infoJsonRegex(QStringLiteral(R"(\[info\] (?:Writing video metadata as JSON to|Video metadata is already present in|Video description metadata is already present in):\s*(.*\.info\.json))"));
    QRegularExpressionMatch infoJsonMatch = infoJsonRegex.match(normalizedLine);
    if (infoJsonMatch.hasMatch()) {
        m_infoJsonPath = infoJsonMatch.captured(1).trimmed();
        // Normalize path separators if necessary, although QFile should handle it
        m_infoJsonPath = QDir::toNativeSeparators(m_infoJsonPath);

        qDebug() << "Detected info.json path and initiating retry mechanism:" << m_infoJsonPath;
        m_infoJsonRetryCount = 0; // Reset retry count for a new file
        readInfoJsonWithRetry(); // Start the retry mechanism
    }

    // Capture subtitle sidecar paths so they are tracked for cleanup
    static const QRegularExpression subtitleRegex(QStringLiteral(R"(\[info\] (?:Writing video subtitles to|Video subtitles are already present in):\s+(.+)$)"));
    QRegularExpressionMatch subtitleMatch = subtitleRegex.match(normalizedLine);
    if (subtitleMatch.hasMatch()) {
        QString subtitlePath = subtitleMatch.captured(1).trimmed();
        updateTransferTarget(subtitlePath);
        emitStatusUpdate(statusForCurrentTransfer());
    }

    static const QRegularExpression ariaFileRegex(QStringLiteral(R"(^FILE:\s+(.+)$)"));
    const QRegularExpressionMatch ariaFileMatch = ariaFileRegex.match(normalizedLine);
    if (ariaFileMatch.hasMatch()) {
        const QString previousTarget = m_currentTransferTarget;
        const QString previousStatus = m_currentTransferStatus;
        const QString filename = ariaFileMatch.captured(1).trimmed();
        updateTransferTarget(filename);
        if (m_currentTransferTarget != previousTarget || m_currentTransferStatus != previousStatus) {
            emitStatusUpdate(statusForCurrentTransfer());
        }
        return;
    }

    static const QRegularExpression destinationRegex(QStringLiteral(R"(^\[download\]\s+Destination:\s+(.+)$)"));
    const QRegularExpressionMatch destinationMatch = destinationRegex.match(normalizedLine);
    if (destinationMatch.hasMatch()) {
        const QString filename = destinationMatch.captured(1).trimmed();
        updateTransferTarget(filename);
        emitStatusUpdate(statusForCurrentTransfer());
        if (!m_currentTransferIsAuxiliary && m_originalDownloadedFilename.isEmpty()) {
            m_originalDownloadedFilename = filename;
        }
    }

    static const QRegularExpression totalFragmentsRegex(QStringLiteral(R"(^\[download\]\s+Total fragments:\s+(\d+).*$)"));
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

    if (!parseYtDlpProgressLine(normalizedLine)) {
        parseAria2ProgressLine(normalizedLine);
    }
}

QString YtDlpWorker::normalizeConsoleLine(const QString &line) const {
    QString normalized = line;
    static const QRegularExpression ansiRegex(QStringLiteral(R"(\x1B\[[0-9;]*[A-Za-z])"));
    normalized.remove(ansiRegex);
    return normalized.trimmed();
}
