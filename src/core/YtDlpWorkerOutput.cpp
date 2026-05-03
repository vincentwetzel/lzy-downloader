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
#include <QRegularExpression>
#include <QUrl>

void YtDlpWorker::handleOutputLine(const QString &line) {
    const QString normalizedLine = normalizeConsoleLine(line);
    if (normalizedLine.isEmpty()) {
        return;
    }

    m_allOutputLines.append(normalizedLine); // Store all output lines

    emit outputReceived(m_id, normalizedLine);
    // qDebug().noquote() << "yt-dlp (processed line):" << normalizedLine;

    if (normalizedLine.startsWith("LZY_FINAL_PATH:")) {
        m_finalFilename = normalizedLine.mid(15).trimmed(); // 15 is length of "LZY_FINAL_PATH:"
        qDebug() << "Captured precise Final Path:" << m_finalFilename;
        return; // No need to process this line further
    }

    // Parse ERROR: lines from stderr for specific error types
    if (normalizedLine.startsWith("ERROR:")) {
        m_errorLines.append(normalizedLine);
        
        // Check for private video error
        if (normalizedLine.contains("private", Qt::CaseInsensitive) || 
            normalizedLine.contains("This video is private", Qt::CaseInsensitive)) {
            if (!m_errorEmitted) {
                m_errorEmitted = true;
                emit ytDlpErrorDetected(m_id, "private", 
                    "This video is private and cannot be downloaded.", 
                    normalizedLine);
            }
        }
        // Check for unavailable video error
        else if (normalizedLine.contains("unavailable", Qt::CaseInsensitive) ||
                 normalizedLine.contains("Video is unavailable", Qt::CaseInsensitive) ||
                 normalizedLine.contains("This video is no longer available", Qt::CaseInsensitive) ||
                 normalizedLine.contains("does not exist", Qt::CaseInsensitive)) {
            if (!m_errorEmitted) {
                m_errorEmitted = true;
                emit ytDlpErrorDetected(m_id, "unavailable",
                    "This video is unavailable or has been removed.",
                    normalizedLine);
            }
        }
        // Check for geo-restriction error
        else if (normalizedLine.contains("geo", Qt::CaseInsensitive) && 
                 (normalizedLine.contains("restrict", Qt::CaseInsensitive) ||
                  normalizedLine.contains("unavailable in your country", Qt::CaseInsensitive))) {
            if (!m_errorEmitted) {
                m_errorEmitted = true;
                emit ytDlpErrorDetected(m_id, "geo_restricted",
                    "This video is not available in your region.",
                    normalizedLine);
            }
        }
        // Check for members-only error
        else if (normalizedLine.contains("members", Qt::CaseInsensitive) &&
                 normalizedLine.contains("only", Qt::CaseInsensitive)) {
            if (!m_errorEmitted) {
                m_errorEmitted = true;
                emit ytDlpErrorDetected(m_id, "members_only",
                    "This video is exclusive to channel members.",
                    normalizedLine);
            }
        }
        // Check for age-restriction error
        else if (normalizedLine.contains("age", Qt::CaseInsensitive) &&
                 (normalizedLine.contains("restrict", Qt::CaseInsensitive) ||
                  normalizedLine.contains("verify your age", Qt::CaseInsensitive) ||
                  normalizedLine.contains("confirm your age", Qt::CaseInsensitive))) {
            if (!m_errorEmitted) {
                m_errorEmitted = true;
                emit ytDlpErrorDetected(m_id, "age_restricted",
                    "This video requires age verification. Try enabling cookies from your browser.",
                    normalizedLine);
            }
        }
        // Check for content removed/unavailable (e.g., deleted tweet)
        else if (normalizedLine.contains("Requested tweet is unavailable", Qt::CaseInsensitive) ||
                 normalizedLine.contains("This content is no longer available", Qt::CaseInsensitive) ||
                 normalizedLine.contains("The requested content was removed", Qt::CaseInsensitive) ||
                 normalizedLine.contains("Suspended", Qt::CaseInsensitive)) {
            if (!m_errorEmitted) {
                m_errorEmitted = true;
                emit ytDlpErrorDetected(m_id, "content_removed",
                    "The requested content is unavailable or has been removed by the uploader.",
                    normalizedLine);
            }
        }
        // Check for scheduled livestream/premiere
        else if (normalizedLine.contains("Premieres in", Qt::CaseInsensitive) ||
                 normalizedLine.contains("Premiering in", Qt::CaseInsensitive) ||
                 normalizedLine.contains("Premiere will begin", Qt::CaseInsensitive) ||
                 normalizedLine.contains("live event will begin", Qt::CaseInsensitive) ||
                 normalizedLine.contains("is upcoming", Qt::CaseInsensitive) ||
                 normalizedLine.contains("Offline (expected)", Qt::CaseInsensitive) ||
                 normalizedLine.contains("Offline expected", Qt::CaseInsensitive) ||
                 normalizedLine.contains("waiting for premiere", Qt::CaseInsensitive) ||
                 normalizedLine.contains("waiting for livestream", Qt::CaseInsensitive) ||
                 normalizedLine.contains("Live in ", Qt::CaseInsensitive) ||
                 normalizedLine.contains("Starting in ", Qt::CaseInsensitive)) {
            if (!m_errorEmitted) {
                m_errorEmitted = true;
                emit ytDlpErrorDetected(m_id, "scheduled_livestream",
                    "This video is a scheduled livestream or premiere that has not started yet.\n\n"
                    "Would you like to wait for the video to begin and download it automatically?",
                    normalizedLine);
            }
        }
    }

    // Parse lifecycle / post-processing operations so the UI can follow the real stage
    if (handleLifecycleStatusLine(normalizedLine)) {
        return;
    }

    static const QRegularExpression formatListRegex(R"(^\[info\].*Downloading\s+\d+\s+format\(s\):\s+(.+)$)");
    const QRegularExpressionMatch formatListMatch = formatListRegex.match(normalizedLine);
    if (formatListMatch.hasMatch()) {
        inferRequestedTransfersFromFormatList(formatListMatch.captured(1).trimmed());
    }

    if (handleAria2CommandLine(normalizedLine)) {
        return;
    }

    // If we are waiting for a scheduled livestream, fetch metadata in the background so the UI 
    // can show the title and thumbnail instead of just the URL during the long wait.
    if (normalizedLine.startsWith("[wait]", Qt::CaseInsensitive) || normalizedLine.startsWith("[download] Waiting for video", Qt::CaseInsensitive)) {
        if (m_videoTitle.isEmpty() && !property("fetchingPreWaitMetadata").toBool()) {
            setProperty("fetchingPreWaitMetadata", true);
            
            QString url;
            for (const QString &arg : m_args) {
                if (arg.startsWith("http")) { url = arg; break; }
            }
            if (url.isEmpty()) {
                for (const QString &arg : m_args) {
                    // Grab the first non-flag argument as a fallback URL
                    if (!arg.startsWith("-")) { url = arg; break; }
                }
            }
            
            // Fast-path: YouTube oEmbed API avoids spawning a yt-dlp process and bypassing 
            // the ExtractorError completely for upcoming livestreams.
            if (url.contains("youtube.com") || url.contains("youtu.be")) {
                qDebug() << "[YtDlpWorker] Detected [wait] state. Using YouTube oEmbed API for pre-wait metadata...";
                QNetworkAccessManager *manager = new QNetworkAccessManager(this);
                QUrl oembedUrl("https://www.youtube.com/oembed?url=" + url + "&format=json");
                QNetworkRequest request(oembedUrl);
                request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
                QNetworkReply *reply = manager->get(request);
                connect(reply, &QNetworkReply::finished, this, [this, reply, manager]() {
                    if (reply->error() == QNetworkReply::NoError) {
                        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                        if (doc.isObject()) {
                            QJsonObject obj = doc.object();
                            m_videoTitle = obj.value("title").toString();
                            QString thumbUrl = obj.value("thumbnail_url").toString();
                            
                            qDebug() << "[YtDlpWorker] oEmbed title:" << m_videoTitle << "thumb:" << thumbUrl;
                            
                            QVariantMap progressData;
                            progressData["progress"] = -1;
                            progressData["status"] = "Waiting for livestream to start...";
                            progressData["title"] = m_videoTitle;
                            if (!m_thumbnailPath.isEmpty()) {
                                progressData["thumbnail_path"] = m_thumbnailPath;
                            }
                            emit progressUpdated(m_id, progressData);

                            if (!thumbUrl.isEmpty() && m_thumbnailPath.isEmpty()) {
                                QNetworkRequest thumbReq((QUrl(thumbUrl)));
                                thumbReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
                                QNetworkReply *thumbReply = manager->get(thumbReq);
                                connect(thumbReply, &QNetworkReply::finished, this, [this, thumbReply, manager]() {
                                    if (thumbReply->error() == QNetworkReply::NoError) {
                                        QString tempDir = m_configManager->get("Paths", "temporary_downloads_directory").toString();
                                        QDir().mkpath(tempDir);
                                        QString ext = ".jpg";
                                        if (thumbReply->url().toString().contains(".webp", Qt::CaseInsensitive)) ext = ".webp";
                                        QString newThumbPath = QDir(tempDir).filePath(m_id + "_wait_thumbnail" + ext);
                                        QFile file(newThumbPath);
                                        if (file.open(QIODevice::WriteOnly)) {
                                            QByteArray data = thumbReply->readAll();
                                            if (!data.isEmpty()) {
                                                file.write(data);
                                                file.close();
                                                m_thumbnailPath = QDir::toNativeSeparators(newThumbPath);
                                                qDebug() << "[YtDlpWorker] Pre-wait thumbnail downloaded to:" << m_thumbnailPath;

                                                QVariantMap pd;
                                                pd["progress"] = -1;
                                                pd["status"] = "Waiting for livestream to start...";
                                                pd["title"] = m_videoTitle;
                                                pd["thumbnail_path"] = m_thumbnailPath;
                                                emit progressUpdated(m_id, pd);
                                            } else {
                                                file.close();
                                                file.remove();
                                            }
                                        }
                                    }
                                    thumbReply->deleteLater();
                                    manager->deleteLater();
                                });
                            } else {
                                manager->deleteLater();
                            }
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
            
            QString ytDlpPath = ProcessUtils::findBinary("yt-dlp", m_configManager).path;
            QStringList fetchArgs;
            
            fetchArgs << "--dump-single-json" << "--flat-playlist" << "--ignore-errors" << url;
            int cookieIdx = m_args.indexOf("--cookies-from-browser");
            if (cookieIdx != -1 && cookieIdx + 1 < m_args.size()) {
                fetchArgs << "--cookies-from-browser" << m_args[cookieIdx + 1];
            }

            qDebug() << "[YtDlpWorker] Pre-wait fetch command:" << ytDlpPath << fetchArgs;
            connect(fetchProcess, &QProcess::finished, this, [this, fetchProcess](int exitCode, QProcess::ExitStatus) {
                    QByteArray jsonData = fetchProcess->readAllStandardOutput();
                    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
                    if (doc.isObject()) {
                        QJsonObject obj = doc.object();
                        m_fullMetadata = obj.toVariantMap();
                        if (obj.contains("title") && obj["title"].isString()) {
                            m_videoTitle = obj["title"].toString();
                            qDebug() << "[YtDlpWorker] Pre-wait title fetched:" << m_videoTitle;
                            
                            // Immediately update the UI with the title before we wait for the thumbnail
                            QVariantMap progressData;
                            progressData["progress"] = -1;
                            progressData["status"] = "Waiting for livestream to start...";
                            progressData["title"] = m_videoTitle;
                            if (!m_thumbnailPath.isEmpty()) {
                                progressData["thumbnail_path"] = m_thumbnailPath;
                            }
                            emit progressUpdated(m_id, progressData);
                        }
                        
                        QString thumbUrl;
                        if (obj.contains("thumbnails") && obj["thumbnails"].isArray()) {
                            QJsonArray thumbs = obj["thumbnails"].toArray();
                            if (!thumbs.isEmpty()) {
                                thumbUrl = thumbs.last().toObject().value("url").toString();
                            }
                        } else if (obj.contains("thumbnail")) {
                            thumbUrl = obj.value("thumbnail").toString();
                        }

                        if (!thumbUrl.isEmpty()) {
                            qDebug() << "[YtDlpWorker] Pre-wait thumbnail URL found:" << thumbUrl;
                            QNetworkAccessManager *manager = new QNetworkAccessManager(this);
                            QNetworkRequest request((QUrl(thumbUrl)));
                            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
                            QNetworkReply *reply = manager->get(request);
                            connect(reply, &QNetworkReply::finished, this, [this, reply, manager]() {
                                if (reply->error() == QNetworkReply::NoError) {
                                    QString tempDir = m_configManager->get("Paths", "temporary_downloads_directory").toString();
                                    QDir().mkpath(tempDir);
                                    QString ext = ".jpg";
                                    if (reply->url().toString().contains(".webp", Qt::CaseInsensitive)) ext = ".webp";
                                    QString newThumbPath = QDir(tempDir).filePath(m_id + "_wait_thumbnail" + ext);
                                    QFile file(newThumbPath);
                                    if (file.open(QIODevice::WriteOnly)) {
                                        QByteArray data = reply->readAll();
                                        if (!data.isEmpty()) {
                                            file.write(data);
                                            file.close();
                                            m_thumbnailPath = QDir::toNativeSeparators(newThumbPath);
                                            qDebug() << "[YtDlpWorker] Pre-wait thumbnail downloaded to:" << m_thumbnailPath;

                                            QVariantMap pd;
                                            pd["progress"] = -1;
                                            pd["status"] = "Waiting for livestream to start...";
                                            pd["title"] = m_videoTitle;
                                            pd["thumbnail_path"] = m_thumbnailPath;
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
                        }
                    } else {
                        qWarning() << "[YtDlpWorker] Pre-wait metadata fetch failed or returned invalid JSON. Exit code:" << exitCode;
                        qWarning() << "[YtDlpWorker] Stderr:" << fetchProcess->readAllStandardError();
                    }
                fetchProcess->deleteLater();
            });
            
            fetchProcess->start(ytDlpPath, fetchArgs);
        }
    }

    if (normalizedLine.startsWith("[wait] Remaining time until next attempt:")) {
        const QString prefix = "[wait] Remaining time until next attempt: ";
        QString time = normalizedLine.mid(prefix.length()).trimmed();
        QVariantMap progressData;
        progressData["progress"] = -1; // Indeterminate state
        progressData["status"] = QString("Next check in %1").arg(time);
        if (!m_videoTitle.isEmpty()) {
            progressData["title"] = m_videoTitle;
        }
        if (!m_thumbnailPath.isEmpty()) {
            progressData["thumbnail_path"] = m_thumbnailPath;
        }
        emit progressUpdated(m_id, progressData);
        return; // This line is handled, no further processing needed.
    }

    if (normalizedLine.startsWith("[download] Waiting for video", Qt::CaseInsensitive) || normalizedLine.startsWith("[wait]", Qt::CaseInsensitive)) {
        QVariantMap progressData;
        progressData["progress"] = -1; // Indeterminate state
        progressData["status"] = "Waiting for livestream to start...";
        if (!m_videoTitle.isEmpty()) {
            progressData["title"] = m_videoTitle;
        }
        if (!m_thumbnailPath.isEmpty()) {
            progressData["thumbnail_path"] = m_thumbnailPath;
        }
        emit progressUpdated(m_id, progressData);
        return;
    }

    static QRegularExpression thumbnailRegex("\\[ThumbnailsConvertor\\] Converting thumbnail \"([^\"]+)\" to jpg");
    QRegularExpressionMatch thumbnailMatch = thumbnailRegex.match(normalizedLine);
    if (thumbnailMatch.hasMatch()) {
        QString webpPath = thumbnailMatch.captured(1);
        m_thumbnailPath = QDir::toNativeSeparators(webpPath.replace(".webp", ".jpg"));
        QVariantMap updateData;
        updateData["thumbnail_path"] = m_thumbnailPath;
        qDebug() << "[LOG] YtDlpWorker: Found converted thumbnail path for" << m_id << ":" << m_thumbnailPath;
        emit progressUpdated(m_id, updateData);
    }

    // 1. Capture the info.json file path and store it, then initiate retry mechanism
    static QRegularExpression infoJsonRegex(R"(\[info\] (?:Writing video metadata as JSON to|Video metadata is already present in|Video description metadata is already present in):\s*(.*\.info\.json))");
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
    static const QRegularExpression subtitleRegex(R"(\[info\] (?:Writing video subtitles to|Video subtitles are already present in):\s+(.+)$)");
    QRegularExpressionMatch subtitleMatch = subtitleRegex.match(normalizedLine);
    if (subtitleMatch.hasMatch()) {
        QString subtitlePath = subtitleMatch.captured(1).trimmed();
        updateTransferTarget(subtitlePath);
        emitStatusUpdate(statusForCurrentTransfer());
    }

    static const QRegularExpression ariaFileRegex(R"(^FILE:\s+(.+)$)");
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

    static const QRegularExpression destinationRegex(R"(^\[download\]\s+Destination:\s+(.+)$)");
    const QRegularExpressionMatch destinationMatch = destinationRegex.match(normalizedLine);
    if (destinationMatch.hasMatch()) {
        const QString filename = destinationMatch.captured(1).trimmed();
        updateTransferTarget(filename);
        emitStatusUpdate(statusForCurrentTransfer());
        if (!m_currentTransferIsAuxiliary && m_originalDownloadedFilename.isEmpty()) {
            m_originalDownloadedFilename = filename;
        }
    }

    static const QRegularExpression totalFragmentsRegex(R"(^\[download\]\s+Total fragments:\s+(\d+).*$)");
    const QRegularExpressionMatch totalFragmentsMatch = totalFragmentsRegex.match(normalizedLine);
    if (totalFragmentsMatch.hasMatch()) {
        const QString segmentCount = totalFragmentsMatch.captured(1);
        const QString transferStatus = statusForCurrentTransfer();
        QString segmentStatus;
        if (transferStatus.contains("audio", Qt::CaseInsensitive)) {
            segmentStatus = QString("Downloading %1 audio segment(s)...").arg(segmentCount);
        } else if (transferStatus.contains("video", Qt::CaseInsensitive)) {
            segmentStatus = QString("Downloading %1 video segment(s)...").arg(segmentCount);
        } else {
            segmentStatus = QString("Downloading %1 segment(s)...").arg(segmentCount);
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
    static const QRegularExpression ansiRegex(R"(\x1B\[[0-9;]*[A-Za-z])");
    normalized.remove(ansiRegex);
    return normalized.trimmed();
}
