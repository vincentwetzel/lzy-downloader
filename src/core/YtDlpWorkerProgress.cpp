#include "YtDlpWorker.h"

#include <QDebug>
#include <QDir>
#include <QMap>
#include <QRegularExpression>
#include <cmath>

double YtDlpWorker::parseSizeStringToBytes(const QString &sizeString) {
    const QString normalized = sizeString.trimmed().remove('~');
    if (normalized.isEmpty() || normalized.startsWith("Unknown", Qt::CaseInsensitive)) {
        return 0.0;
    }

    static const QRegularExpression re(R"(^([\d\.]+)\s*([KMGTPE]?i?B)(?:/s)?$)", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(normalized);
    if (!match.hasMatch()) {
        return 0.0;
    }

    const double value = match.captured(1).toDouble();
    const QString unit = match.captured(2).toUpper();

    static const QMap<QString, double> multipliers = {
        {"B", 1.0},
        {"KB", 1000.0},
        {"MB", 1000.0 * 1000.0},
        {"GB", 1000.0 * 1000.0 * 1000.0},
        {"TB", 1000.0 * 1000.0 * 1000.0 * 1000.0},
        {"PB", 1000.0 * 1000.0 * 1000.0 * 1000.0 * 1000.0},
        {"KIB", 1024.0},
        {"MIB", 1024.0 * 1024.0},
        {"GIB", 1024.0 * 1024.0 * 1024.0},
        {"TIB", 1024.0 * 1024.0 * 1024.0 * 1024.0},
        {"PIB", 1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0}
    };

    const double multiplier = multipliers.value(unit, 0.0);
    const double bytes = value * multiplier;
    qDebug() << "Converted" << value << unit << "to" << bytes << "bytes";
    return bytes;
}

QString YtDlpWorker::formatBytes(double bytes) {
    if (bytes < 0) return "N/A";
    if (bytes == 0) return "0 B";

    const QStringList units = {"B", "KiB", "MiB", "GiB", "TiB"}; // Use MiB units
    int i = 0;
    double d_bytes = bytes;

    while (d_bytes >= 1024 && i < units.size() - 1) {
        d_bytes /= 1024;
        i++;
    }

    return QString::number(d_bytes, 'f', (d_bytes < 10 && i > 0) ? 2 : 1) + " " + units[i];
}

bool YtDlpWorker::parseYtDlpProgressLine(const QString &line) {
    const QString normalized = line.trimmed();

    if (!normalized.contains("[download]")) {
        return false;
    }

    static const QRegularExpression progressRegex(
        R"(^\[download\]\s+([\d\.]+)%\s+of\s+(?:~\s*)?(.+?)(?=\s+at\s+|\s+ETA\s+|\s+\(frag\s+\d+/\d+\)|$)(?:\s+at\s+(.+?)(?=\s+ETA\s+|\s+\(frag\s+\d+/\d+\)|$))?(?:\s+ETA\s+([^\s]+))?(?:\s+\(frag\s+\d+/\d+\))?.*$)");
    static const QRegularExpression completedRegex(
        R"(^\[download\]\s+100(?:\.0+)?%\s+of\s+(?:~\s*)?(.+?)(?=\s+in\s+|\s+at\s+|\s+\(frag\s+\d+/\d+\)|$)(?:\s+in\s+([^\s]+))?(?:\s+at\s+(.+?)(?=\s+\(frag\s+\d+/\d+\)|$))?(?:\s+\(frag\s+\d+/\d+\))?.*$)");
    static const QRegularExpression indeterminateRegex(
        R"(^\[download\]\s+(.+?)\s+at\s+(.+?)\s+\(([^)]+)\).*$)");

    QRegularExpressionMatch match = progressRegex.match(normalized);
    const bool matchedCompletedFormat = !match.hasMatch() && (match = completedRegex.match(normalized)).hasMatch();
    const bool matchedIndeterminate = !match.hasMatch() && (match = indeterminateRegex.match(normalized)).hasMatch();
    if (!match.hasMatch()) {
        qDebug() << "[YtDlpWorker] Unmatched native progress line:" << normalized;
        return false;
    }

    if (m_currentTransferIsAuxiliary) {
        QVariantMap progressData;
        progressData["status"] = statusForCurrentTransfer();
        progressData["progress"] = -1;
        if (!m_videoTitle.isEmpty()) {
            progressData["title"] = m_videoTitle;
        }
        if (!m_thumbnailPath.isEmpty()) {
            progressData["thumbnail_path"] = m_thumbnailPath;
        }
        emit progressUpdated(m_id, progressData);
        qDebug() << "yt-dlp: Ignoring auxiliary transfer progress for" << m_currentTransferTarget;
        return true;
    }

    QVariantMap progressData;
    double percentage = 0.0;
    QString totalString;
    QString speedString;
    QString etaString;
    double totalBytes = 0.0;
    double downloadedBytes = 0.0;
    double speedBytes = 0.0;

    if (matchedIndeterminate) {
        percentage = -1.0; // Puts the progress bar into indeterminate scrolling mode
        downloadedBytes = parseSizeStringToBytes(match.captured(1));
        totalString = "Unknown";
        speedString = match.captured(2).trimmed();
        speedBytes = parseSizeStringToBytes(speedString);
        etaString = match.captured(3).trimmed();
        if (etaString.isEmpty()) etaString = "Unknown";
    } else {
        percentage = matchedCompletedFormat ? 100.0 : match.captured(1).toDouble();
        totalString = (matchedCompletedFormat ? match.captured(1) : match.captured(2)).trimmed();
        speedString = match.captured(3).trimmed();
        etaString = matchedCompletedFormat ? QStringLiteral("0:00") : match.captured(4).trimmed();

        totalBytes = parseSizeStringToBytes(totalString);
        downloadedBytes = totalBytes > 0.0 ? (totalBytes * (percentage / 100.0)) : 0.0;
        speedBytes = parseSizeStringToBytes(speedString);
    }

    updateInferredTransferStage(percentage, downloadedBytes, totalBytes);

    progressData["progress"] = percentage;
    progressData["status"] = statusForCurrentTransfer();
    progressData["downloaded_size"] = downloadedBytes > 0.0 ? formatBytes(downloadedBytes) : QString("N/A");
    progressData["total_size"] = totalBytes > 0.0 ? formatBytes(totalBytes) : totalString;
    applyOverallPrimaryProgress(progressData, percentage, downloadedBytes, totalBytes);
    progressData["speed"] = speedBytes > 0.0 ? formatBytes(speedBytes) + "/s" : (speedString.isEmpty() ? QString("Unknown") : speedString);
    progressData["speed_bytes"] = speedBytes;
    progressData["eta"] = etaString.isEmpty() ? QString("Unknown") : etaString;
    if (!m_videoTitle.isEmpty()) {
        progressData["title"] = m_videoTitle;
    }
    if (!m_thumbnailPath.isEmpty()) {
        progressData["thumbnail_path"] = m_thumbnailPath;
    }
        
        QString currentFile;
        if (!m_originalDownloadedFilename.isEmpty()) {
            currentFile = m_originalDownloadedFilename;
        } else if (!m_currentTransferTarget.isEmpty() && !m_currentTransferIsAuxiliary) {
            currentFile = m_currentTransferTarget;
        } else if (!m_infoJsonPath.isEmpty()) {
            currentFile = m_infoJsonPath;
        }
        if (!currentFile.isEmpty()) {
            progressData["current_file"] = currentFile;
        }

    emit progressUpdated(m_id, progressData);
    qDebug() << "yt-dlp: Progress match found (native).";
    return true;
}

bool YtDlpWorker::parseAria2ProgressLine(const QString &line) {
    const QString normalized = line.trimmed();

    static const QRegularExpression ariaRegex(
        R"(^.*\[#\w+\s+([\d\.]+\s*[KMGTPE]?i?B)(?:/([\d\.]+\s*[KMGTPE]?i?B)\(([\d\.]+)%\))?(?:\s+CN:\d+)?(?:\s+DL:((?:[\d\.]+\s*[KMGTPE]?i?B(?:/s)?)|(?:0B(?:/s)?)))?(?:\s+ETA:([\d\w:]+))?\].*$)");
    const QRegularExpressionMatch match = ariaRegex.match(normalized);
    if (!match.hasMatch()) {
        return false;
    }

    QVariantMap progressData;
    double downloadedBytes = parseSizeStringToBytes(match.captured(1));
    double totalBytes = match.captured(2).isEmpty() ? 0.0 : parseSizeStringToBytes(match.captured(2));
    double percentage = match.captured(3).isEmpty() ? -1.0 : match.captured(3).toDouble();
    const double speedBytes = parseSizeStringToBytes(match.captured(4));

    updateInferredTransferStage(percentage, downloadedBytes, totalBytes);

    progressData["progress"] = percentage;
    progressData["status"] = statusForCurrentTransfer();
    progressData["downloaded_size"] = formatBytes(downloadedBytes);
    progressData["total_size"] = formatBytes(totalBytes);
    applyOverallPrimaryProgress(progressData, percentage, downloadedBytes, totalBytes);
    progressData["speed"] = speedBytes > 0.0 ? formatBytes(speedBytes) + "/s" : QString("0 B/s");
    progressData["speed_bytes"] = speedBytes;
    progressData["eta"] = match.captured(5).isEmpty() ? QString("N/A") : match.captured(5);
    if (!m_videoTitle.isEmpty()) {
        progressData["title"] = m_videoTitle;
    }
    if (!m_thumbnailPath.isEmpty()) {
        progressData["thumbnail_path"] = m_thumbnailPath;
    }
        
        QString currentFile;
        if (!m_originalDownloadedFilename.isEmpty()) {
            currentFile = m_originalDownloadedFilename;
        } else if (!m_currentTransferTarget.isEmpty() && !m_currentTransferIsAuxiliary) {
            currentFile = m_currentTransferTarget;
        } else if (!m_infoJsonPath.isEmpty()) {
            currentFile = m_infoJsonPath;
        }
        if (!currentFile.isEmpty()) {
            progressData["current_file"] = currentFile;
        }

    emit progressUpdated(m_id, progressData);
    qDebug() << "yt-dlp: Progress match found (aria2c raw).";
    return true;
}

void YtDlpWorker::applyOverallPrimaryProgress(QVariantMap &progressData, double percentage, double downloadedBytes, double totalBytes) {
    if (m_currentTransferIsAuxiliary || m_requestedTransferSizes.size() <= 1 || m_inferredTransferIndex < 0 || m_inferredTransferIndex >= m_requestedTransferSizes.size()) {
        return;
    }

    double completedBytes = 0.0;
    for (int i = 0; i < m_inferredTransferIndex; ++i) {
        completedBytes += m_requestedTransferSizes.at(i);
    }

    double overallTotalBytes = 0.0;
    for (double transferSize : m_requestedTransferSizes) {
        overallTotalBytes += transferSize;
    }
    if (overallTotalBytes <= 0.0) {
        return;
    }

    const double plannedCurrentBytes = m_requestedTransferSizes.at(m_inferredTransferIndex);
    const double effectiveCurrentTotal = totalBytes > 0.0 ? totalBytes : plannedCurrentBytes;
    double effectiveCurrentDownloaded = downloadedBytes;
    if (effectiveCurrentDownloaded <= 0.0 && effectiveCurrentTotal > 0.0) {
        effectiveCurrentDownloaded = effectiveCurrentTotal * (percentage / 100.0);
    }
    if (plannedCurrentBytes > 0.0) {
        effectiveCurrentDownloaded = qMin(effectiveCurrentDownloaded, plannedCurrentBytes);
    }

    const double overallDownloadedBytes = completedBytes + effectiveCurrentDownloaded;
    const double overallPercentage = qBound(0.0, (overallDownloadedBytes / overallTotalBytes) * 100.0, 100.0);

    progressData["overall_progress"] = overallPercentage;
    progressData["overall_downloaded_size"] = formatBytes(overallDownloadedBytes);
    progressData["overall_total_size"] = formatBytes(overallTotalBytes);
}
