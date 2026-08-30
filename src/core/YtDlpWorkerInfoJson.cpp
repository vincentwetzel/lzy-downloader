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
            const QString tempDir = DownloadTempCleanup::resolveRoot(m_configManager);
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
        for (qsizetype i = 0; i < m_requestedTransferFormatIds.size() && i < m_requestedTransferSizes.size(); ++i) {
            if (m_requestedTransferSizes.at(i) <= 0.0) {
                m_requestedTransferSizes[i] = inferPrimaryStreamSizeFromMetadata(m_requestedTransferFormatIds.at(i));
            }
        }
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
                    const QString newThumb = QDir::fromNativeSeparators(filepathVal.toString());
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


