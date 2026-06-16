#include "YtDlpWorker.h"

#include <QDebug>
#include <QDir>
#include <QHash>
#include <QRegularExpression>
#include <cmath>
#include <numeric>
#include <array>

namespace {
    void populateCommonData(QVariantMap &progressData, const QString &title, const QString &thumbnail, const QString &origFilename, const QString &transferTarget, bool isAux, const QString &infoJson) {
        if (!title.isEmpty()) {
            progressData.insert(QStringLiteral("title"), title);
        }
        if (!thumbnail.isEmpty()) {
            progressData.insert(QStringLiteral("thumbnail_path"), thumbnail);
        }
        
        QString currentFile;
        if (!origFilename.isEmpty()) {
            currentFile = origFilename;
        } else if (!transferTarget.isEmpty() && !isAux) {
            currentFile = transferTarget;
        } else if (!infoJson.isEmpty()) {
            currentFile = infoJson;
        }
        if (!currentFile.isEmpty()) {
            progressData.insert(QStringLiteral("current_file"), currentFile);
        }
    }
}

double YtDlpWorker::parseSizeStringToBytes(const QString &sizeString) {
    QStringView view(sizeString);
    view = view.trimmed();

    if (view.startsWith(u'~')) {
        view = view.mid(1).trimmed();
    }

    if (view.isEmpty() || view.startsWith(u"Unknown", Qt::CaseInsensitive)) {
        return 0.0;
    }

    qsizetype i = 0;
    while (i < view.size() && (view.at(i).isDigit() || view.at(i) == u'.')) {
        ++i;
    }

    if (i == 0) return 0.0;

    const double value = view.left(i).toDouble();
    QStringView unit = view.mid(i).trimmed();

    if (unit.endsWith(u"/s", Qt::CaseInsensitive)) {
        unit = unit.chopped(2).trimmed();
    }

    double multiplier = 1.0;
    if (unit.isEmpty()) return value;

    const char16_t firstChar = unit.at(0).toUpper().unicode();
    if (unit.length() == 1 && firstChar == u'B') {
        multiplier = 1.0;
    } else if (unit.endsWith(u"IB", Qt::CaseInsensitive)) {
        switch (firstChar) {
            case u'K': multiplier = 1024.0; break;
            case u'M': multiplier = 1024.0 * 1024.0; break;
            case u'G': multiplier = 1024.0 * 1024.0 * 1024.0; break;
            case u'T': multiplier = 1024.0 * 1024.0 * 1024.0 * 1024.0; break;
            case u'P': multiplier = 1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0; break;
        }
    } else if (unit.endsWith(u"B", Qt::CaseInsensitive)) {
        switch (firstChar) {
            case u'K': multiplier = 1000.0; break;
            case u'M': multiplier = 1000.0 * 1000.0; break;
            case u'G': multiplier = 1000.0 * 1000.0 * 1000.0; break;
            case u'T': multiplier = 1000.0 * 1000.0 * 1000.0 * 1000.0; break;
            case u'P': multiplier = 1000.0 * 1000.0 * 1000.0 * 1000.0 * 1000.0; break;
        }
    }

    return value * multiplier;
}

QString YtDlpWorker::formatBytes(double bytes) {
    if (bytes < 0) return tr("N/A");
    if (bytes == 0) return tr("0 B");

    static constexpr std::array<const char*, 5> units = {"B", "KiB", "MiB", "GiB", "TiB"}; // Use MiB units
    int i = 0;
    double d_bytes = bytes;

    while (d_bytes >= 1024 && i < 4) {
        d_bytes /= 1024;
        i++;
    }

    return QString::number(d_bytes, 'f', (d_bytes < 10 && i > 0) ? 2 : 1) + QLatin1Char(' ') + QString::fromLatin1(units[i]);
}

bool YtDlpWorker::parseYtDlpProgressLine(const QString &line) {
    const QStringView normalizedView = QStringView(line).trimmed();

    if (!normalizedView.startsWith(u"[download]")) {
        return false;
    }

    static const QRegularExpression progressRegex(
        QStringLiteral(R"(^\[download\]\s+([\d\.]+)%\s+of\s+(?:~\s*)?(.+?)(?=\s+at\s+|\s+ETA\s+|\s+\(frag\s+\d+/\d+\)|$)(?:\s+at\s+(.+?)(?=\s+ETA\s+|\s+\(frag\s+\d+/\d+\)|$))?(?:\s+ETA\s+([^\s]+))?(?:\s+\(frag\s+(\d+)/(\d+)\))?)"));
    static const QRegularExpression completedRegex(
        QStringLiteral(R"(^\[download\]\s+100(?:\.0+)?%\s+of\s+(?:~\s*)?(.+?)(?=\s+in\s+|\s+at\s+|\s+\(frag\s+\d+/\d+\)|$)(?:\s+in\s+([^\s]+))?(?:\s+at\s+(.+?)(?=\s+\(frag\s+\d+/\d+\)|$))?(?:\s+\(frag\s+(\d+)/(\d+)\))?)"));
    static const QRegularExpression indeterminateRegex(
        QStringLiteral(R"(^\[download\]\s+(.+?)\s+at\s+(.+?)\s+\(([^)]+)\))"));

    bool matchedCompletedFormat = false;
    bool matchedIndeterminate = false;
    QRegularExpressionMatch match = progressRegex.matchView(normalizedView);

    if (!match.hasMatch()) {
        match = completedRegex.matchView(normalizedView);
        if (match.hasMatch()) {
            matchedCompletedFormat = true;
        } else {
            match = indeterminateRegex.matchView(normalizedView);
            if (match.hasMatch()) {
                matchedIndeterminate = true;
            } else {
                qWarning() << "[YtDlpWorker] Unmatched native progress line:" << normalizedView;
                return false;
            }
        }
    }

    if (m_currentTransferIsAuxiliary) {
        QVariantMap progressData;
        progressData.insert(QStringLiteral("status"), statusForCurrentTransfer());
        progressData.insert(QStringLiteral("progress"), -1);
        populateCommonData(progressData, m_videoTitle, m_thumbnailPath, m_originalDownloadedFilename, m_currentTransferTarget, m_currentTransferIsAuxiliary, m_infoJsonPath);
        emit progressUpdated(m_id, progressData);
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
    QString customDownloadedSize;
    QString customTotalSize;

    if (matchedIndeterminate) {
        percentage = -1.0; // Puts the progress bar into indeterminate scrolling mode
        downloadedBytes = parseSizeStringToBytes(match.captured(1));
        totalString = tr("Unknown");
        speedString = match.captured(2).trimmed();
        speedBytes = parseSizeStringToBytes(speedString);
        etaString = match.captured(3).trimmed();
        if (etaString.isEmpty()) etaString = tr("Unknown");

        if (etaString.startsWith(u"frag ")) {
            QStringView fragView(etaString);
            fragView = fragView.mid(5).trimmed();
            const qsizetype slashIdx = fragView.indexOf(u'/');
            if (slashIdx > 0) {
                const QStringView fragCurrentStr = fragView.left(slashIdx);
                const QStringView fragTotalStr = fragView.mid(slashIdx + 1);
                const double fragCurrent = fragCurrentStr.toDouble();
                const double fragTotal = fragTotalStr.toDouble();
                if (fragTotal > 0) {
                    percentage = (fragCurrent / fragTotal) * 100.0;
                    etaString = tr("Unknown");
                    customDownloadedSize = tr("%1 Segs").arg(fragCurrentStr.toString());
                    customTotalSize = tr("%1 Segs").arg(fragTotalStr.toString());
                    totalBytes = 0.0;
                }
            }
        }
    } else {
        percentage = matchedCompletedFormat ? 100.0 : match.capturedView(1).toDouble();
        totalString = (matchedCompletedFormat ? match.capturedView(1) : match.capturedView(2)).trimmed().toString();
        speedString = match.capturedView(3).trimmed().toString();
        etaString = matchedCompletedFormat ? QStringLiteral("0:00") : match.capturedView(4).trimmed().toString();

        const QStringView fragCurrentStr = matchedCompletedFormat ? match.capturedView(4) : match.capturedView(5);
        const QStringView fragTotalStr = matchedCompletedFormat ? match.capturedView(5) : match.capturedView(6);

        if (!fragCurrentStr.isEmpty() && !fragTotalStr.isEmpty()) {
            const double fragCurrent = fragCurrentStr.toDouble();
            const double fragTotal = fragTotalStr.toDouble();
            if (fragTotal > 0) {
                const double fragPercentage = (fragCurrent / fragTotal) * 100.0;
                const double printedPercentage = percentage;

                // If printed percentage differs from fragment overall progress by more than 1%,
                // yt-dlp is reporting progress for the individual fragment, not the overall video.
                // We override it to display the true overall progress.
                if (qAbs(printedPercentage - fragPercentage) > 1.0) {
                    percentage = fragPercentage;
                    customDownloadedSize = tr("%1 Segs").arg(fragCurrentStr.toString());
                    customTotalSize = tr("%1 Segs").arg(fragTotalStr.toString());
                    totalString = tr("Unknown"); // so totalBytes becomes 0
                }
            }
        }

        totalBytes = parseSizeStringToBytes(totalString);
        downloadedBytes = totalBytes > 0.0 ? (totalBytes * (percentage / 100.0)) : 0.0;
        speedBytes = parseSizeStringToBytes(speedString);
    }

    updateInferredTransferStage(percentage, downloadedBytes, totalBytes);

    progressData.insert(QStringLiteral("progress"), percentage);
    progressData.insert(QStringLiteral("status"), statusForCurrentTransfer());
    progressData.insert(QStringLiteral("downloaded_size"), !customDownloadedSize.isEmpty() ? customDownloadedSize : (downloadedBytes > 0.0 ? formatBytes(downloadedBytes) : tr("N/A")));
    progressData.insert(QStringLiteral("total_size"), !customTotalSize.isEmpty() ? customTotalSize : (totalBytes > 0.0 ? formatBytes(totalBytes) : totalString));
    applyOverallPrimaryProgress(progressData, percentage, downloadedBytes, totalBytes);
    progressData.insert(QStringLiteral("speed"), speedBytes > 0.0 ? (formatBytes(speedBytes) + tr("/s")) : (speedString.isEmpty() ? tr("Unknown") : speedString));
    progressData.insert(QStringLiteral("speed_bytes"), speedBytes);
    progressData.insert(QStringLiteral("eta"), etaString.isEmpty() ? tr("Unknown") : etaString);
    
    populateCommonData(progressData, m_videoTitle, m_thumbnailPath, m_originalDownloadedFilename, m_currentTransferTarget, m_currentTransferIsAuxiliary, m_infoJsonPath);

    emit progressUpdated(m_id, progressData);
    return true;
}

bool YtDlpWorker::parseAria2ProgressLine(const QString &line) {
    // High-frequency optimization: bypass expensive regex evaluation entirely for non-aria2 lines
    if (!line.contains(u"[#")) {
        return false;
    }
    const QStringView normalizedView = QStringView(line).trimmed();

    static const QRegularExpression ariaRegex(
        QStringLiteral(R"(\[#\w+\s+([\d\.]+\s*[KMGTPE]?i?B)(?:/([\d\.]+\s*[KMGTPE]?i?B)\(([\d\.]+)%\))?(?:\s+CN:\d+)?(?:\s+DL:((?:[\d\.]+\s*[KMGTPE]?i?B(?:/s)?)|(?:0B(?:/s)?)))?(?:\s+ETA:([\d\w:]+))?\])"));
    const QRegularExpressionMatch match = ariaRegex.matchView(normalizedView);
    if (!match.hasMatch()) {
        return false;
    }

    QVariantMap progressData;
    const double downloadedBytes = parseSizeStringToBytes(match.captured(1));
    const QString totalBytesStr = match.capturedView(2).toString();
    const double totalBytes = totalBytesStr.isEmpty() ? 0.0 : parseSizeStringToBytes(totalBytesStr);
    const double percentage = match.capturedView(3).isEmpty() ? -1.0 : match.capturedView(3).toDouble();
    const double speedBytes = parseSizeStringToBytes(match.captured(4));
    const QString etaStr = match.capturedView(5).toString();

    updateInferredTransferStage(percentage, downloadedBytes, totalBytes);

    progressData.insert(QStringLiteral("progress"), percentage);
    progressData.insert(QStringLiteral("status"), statusForCurrentTransfer());
    progressData.insert(QStringLiteral("downloaded_size"), formatBytes(downloadedBytes));
    progressData.insert(QStringLiteral("total_size"), formatBytes(totalBytes));
    applyOverallPrimaryProgress(progressData, percentage, downloadedBytes, totalBytes);
    progressData.insert(QStringLiteral("speed"), speedBytes > 0.0 ? (formatBytes(speedBytes) + tr("/s")) : tr("0 B/s"));
    progressData.insert(QStringLiteral("speed_bytes"), speedBytes);
    progressData.insert(QStringLiteral("eta"), etaStr.isEmpty() ? tr("N/A") : etaStr);
    
    populateCommonData(progressData, m_videoTitle, m_thumbnailPath, m_originalDownloadedFilename, m_currentTransferTarget, m_currentTransferIsAuxiliary, m_infoJsonPath);

    emit progressUpdated(m_id, progressData);
    return true;
}

void YtDlpWorker::applyOverallPrimaryProgress(QVariantMap &progressData, double percentage, double downloadedBytes, double totalBytes) {
    if (m_currentTransferIsAuxiliary || m_requestedTransferSizes.size() <= 1 || m_inferredTransferIndex < 0 || m_inferredTransferIndex >= m_requestedTransferSizes.size()) {
        return;
    }

    const double completedBytes = std::accumulate(m_requestedTransferSizes.begin(), m_requestedTransferSizes.begin() + m_inferredTransferIndex, 0.0);
    const double overallTotalBytes = std::accumulate(m_requestedTransferSizes.begin(), m_requestedTransferSizes.end(), 0.0);

    if (overallTotalBytes <= 0.0) {
        return;
    }

    const double plannedCurrentBytes = m_requestedTransferSizes.at(m_inferredTransferIndex);
    const double effectiveCurrentTotal = totalBytes > 0.0 ? totalBytes : plannedCurrentBytes;
    double effectiveCurrentDownloaded = downloadedBytes;
    if (effectiveCurrentDownloaded <= 0.0 && effectiveCurrentTotal > 0.0 && percentage >= 0.0) {
        effectiveCurrentDownloaded = effectiveCurrentTotal * (percentage / 100.0);
    }
    effectiveCurrentDownloaded = qMax(0.0, effectiveCurrentDownloaded);
    if (plannedCurrentBytes > 0.0) {
        effectiveCurrentDownloaded = qMin(effectiveCurrentDownloaded, plannedCurrentBytes);
    }

    const double overallDownloadedBytes = completedBytes + effectiveCurrentDownloaded;
    const double overallPercentage = qBound(0.0, (overallDownloadedBytes / overallTotalBytes) * 100.0, 100.0);

    progressData.insert(QStringLiteral("overall_progress"), overallPercentage);
    progressData.insert(QStringLiteral("overall_downloaded_size"), formatBytes(overallDownloadedBytes));
    progressData.insert(QStringLiteral("overall_total_size"), formatBytes(overallTotalBytes));
}
