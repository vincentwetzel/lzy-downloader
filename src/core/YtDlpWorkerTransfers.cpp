#include "YtDlpWorker.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <limits>

void YtDlpWorker::updateTransferTarget(const QString &path) {
    m_currentTransferTarget = QDir::toNativeSeparators(path);
    m_currentTransferIsAuxiliary = isAuxiliaryTransferTarget(m_currentTransferTarget);

    if (m_currentTransferIsAuxiliary) {
        const QString lowerPath = m_currentTransferTarget.toLower();
        if (lowerPath.endsWith(QStringLiteral(".info.json"))) {
            m_currentTransferStatus = tr("Downloading metadata...");
        } else if (lowerPath.contains(QStringLiteral(".jpg")) || lowerPath.contains(QStringLiteral(".jpeg")) || lowerPath.contains(QStringLiteral(".png")) || lowerPath.contains(QStringLiteral(".webp")) || lowerPath.contains(QStringLiteral(".avif"))) {
            m_currentTransferStatus = tr("Downloading thumbnail...");
        } else if (lowerPath.contains(QStringLiteral(".srt")) || lowerPath.contains(QStringLiteral(".vtt")) || lowerPath.contains(QStringLiteral(".ass")) || lowerPath.contains(QStringLiteral(".lrc")) || lowerPath.contains(QStringLiteral(".sbv"))) {
            m_currentTransferStatus = tr("Downloading subtitles...");
        } else {
            m_currentTransferStatus = tr("Downloading auxiliary file...");
        }
    } else {
        const int inferredIndex = inferPrimaryStreamIndexFromPath(m_currentTransferTarget);
        if (inferredIndex >= 0 && inferredIndex < m_requestedTransferStatuses.size()) {
            m_inferredTransferIndex = inferredIndex;
            m_currentTransferStatus = m_requestedTransferStatuses.at(inferredIndex);
        } else {
            const QString inferredStatus = inferPrimaryStreamStatusFromPath(m_currentTransferTarget);
            if (!inferredStatus.isEmpty()) {
                m_currentTransferStatus = inferredStatus;
                if (m_currentTransferStatus.contains(QStringLiteral("video"), Qt::CaseInsensitive)) {
                    m_inferredTransferIndex = 0;
                } else if (m_currentTransferStatus.contains(QStringLiteral("audio"), Qt::CaseInsensitive) && m_requestedTransferStatuses.size() > 1) {
                    m_inferredTransferIndex = 1;
                }
            } else {
                m_currentTransferStatus = inferPrimaryStreamStatusFromMetadata(qMax(0, m_inferredTransferIndex));
            }
        }
    }

    qDebug() << "[YtDlpWorker] Transfer target:" << m_currentTransferTarget
             << "auxiliary:" << m_currentTransferIsAuxiliary
             << "status:" << m_currentTransferStatus;
}

double YtDlpWorker::inferPrimaryStreamSizeBytes(const QVariantMap &requestMap) const {
    const double exactSize = requestMap.value(QStringLiteral("filesize")).toDouble();
    if (exactSize > 0.0) {
        return exactSize;
    }
    const double approxSize = requestMap.value(QStringLiteral("filesize_approx")).toDouble();
    if (approxSize > 0.0) {
        return approxSize;
    }
    return 0.0;
}

void YtDlpWorker::inferRequestedTransfersFromFormatList(const QString &formatList) {
    if (!m_requestedTransferStatuses.isEmpty() || formatList.isEmpty()) {
        return;
    }

    const QStringList parts = formatList.split('+', Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return;
    }

    for (int i = 0; i < parts.size(); ++i) {
        const QString part = parts.at(i).trimmed();
        if (part.isEmpty()) {
            continue;
        }

        m_requestedTransferFormatIds.append(part);
        if (parts.size() == 1) {
            m_requestedTransferStatuses.append(tr("Downloading media stream..."));
        } else if (i == 0) {
            m_requestedTransferStatuses.append(tr("Downloading video stream..."));
        } else {
            m_requestedTransferStatuses.append(tr("Downloading audio stream..."));
        }
        m_requestedTransferSizes.append(0.0);
    }

    qDebug() << "[YtDlpWorker] Seeded requested transfers from format list:" << m_requestedTransferFormatIds << m_requestedTransferStatuses;
}

bool YtDlpWorker::handleAria2CommandLine(const QString &line) {
    if (!line.startsWith(QStringLiteral("[debug] aria2c.exe command line:"), Qt::CaseInsensitive)) {
        return false;
    }

    static const QRegularExpression outRegex(QStringLiteral(R"(--out\s+"[^"]*\.f([^\."]+)\.[^"]*")"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression itagRegex(QStringLiteral(R"([?&]itag=(\d+))"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression mimeRegex(QStringLiteral(R"([?&]mime=([^&"]+))"), QRegularExpression::CaseInsensitiveOption);

    QString formatId;
    const QRegularExpressionMatch outMatch = outRegex.match(line);
    if (outMatch.hasMatch()) {
        formatId = outMatch.captured(1).trimmed();
    }

    const QRegularExpressionMatch mimeMatch = mimeRegex.match(line);
    const QString mimeValue = mimeMatch.hasMatch() ? QUrl::fromPercentEncoding(mimeMatch.captured(1).toUtf8()).toLower() : QString();

    if (m_requestedTransferStatuses.isEmpty()) {
        const QRegularExpressionMatch itagMatch = itagRegex.match(line);
        if (itagMatch.hasMatch()) {
            const QString itag = itagMatch.captured(1).trimmed();
            if (!itag.isEmpty()) {
                if (mimeValue.startsWith(QStringLiteral("audio/"))) {
                    m_requestedTransferFormatIds.append(itag);
                    m_requestedTransferStatuses.append(tr("Downloading audio stream..."));
                    m_requestedTransferSizes.append(0.0);
                } else if (mimeValue.startsWith(QStringLiteral("video/"))) {
                    m_requestedTransferFormatIds.append(itag);
                    m_requestedTransferStatuses.append(tr("Downloading video stream..."));
                    m_requestedTransferSizes.append(0.0);
                }
            }
        }
    }

    if (!formatId.isEmpty()) {
        const int pathIndex = inferPrimaryStreamIndexFromPath(QStringLiteral("dummy.f%1.part").arg(formatId));
        if (pathIndex >= 0 && pathIndex < m_requestedTransferStatuses.size()) {
            m_inferredTransferIndex = pathIndex;
            m_currentTransferStatus = m_requestedTransferStatuses.at(pathIndex);
        }
    }

    if (mimeValue.startsWith(QStringLiteral("audio/"))) {
        m_currentTransferStatus = tr("Downloading audio stream...");
        if (m_requestedTransferStatuses.size() > 1) {
            m_inferredTransferIndex = qMax(1, m_inferredTransferIndex);
        }
    } else if (mimeValue.startsWith(QStringLiteral("video/"))) {
        m_currentTransferStatus = tr("Downloading video stream...");
        if (m_inferredTransferIndex < 0) {
            m_inferredTransferIndex = 0;
        }
    }

    if (!mimeValue.isEmpty()) {
        qDebug() << "[YtDlpWorker] aria2 command line inferred mime" << mimeValue << "status" << m_currentTransferStatus << "formatId" << formatId;
    }

    return false;
}

int YtDlpWorker::inferPrimaryStreamIndexFromPath(const QString &path) const {
    if (m_requestedTransferFormatIds.isEmpty()) {
        return -1;
    }

    static const QRegularExpression formatIdRegex(QStringLiteral(R"(\.f([A-Za-z0-9-]+)\.)"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = formatIdRegex.match(path);
    if (!match.hasMatch()) {
        return -1;
    }

    const QString formatIdFromPath = match.captured(1).trimmed().toLower();
    for (int i = 0; i < m_requestedTransferFormatIds.size(); ++i) {
        const QString requestedFormatId = m_requestedTransferFormatIds.at(i).trimmed().toLower();
        if (!requestedFormatId.isEmpty() && requestedFormatId == formatIdFromPath) {
            return i;
        }
    }

    return -1;
}

int YtDlpWorker::inferPrimaryStreamIndexFromTotalBytes(double totalBytes) const {
    if (totalBytes <= 0.0 || m_requestedTransferSizes.isEmpty()) {
        return -1;
    }

    int bestIndex = -1;
    double bestRelativeDiff = std::numeric_limits<double>::max();

    for (int i = 0; i < m_requestedTransferSizes.size(); ++i) {
        const double plannedSize = m_requestedTransferSizes.at(i);
        if (plannedSize <= 0.0) {
            continue;
        }

        const double relativeDiff = qAbs(plannedSize - totalBytes) / qMax(plannedSize, totalBytes);
        if (relativeDiff < bestRelativeDiff) {
            bestRelativeDiff = relativeDiff;
            bestIndex = i;
        }
    }

    if (bestIndex >= 0 && bestRelativeDiff <= 0.35) {
        return bestIndex;
    }

    return -1;
}

bool YtDlpWorker::requestedAudioExtraction() const {
    return m_args.contains("-x") || m_args.contains("--extract-audio");
}

QString YtDlpWorker::inferPrimaryStreamStatusFromPath(const QString &path) const {
    const int inferredIndex = inferPrimaryStreamIndexFromPath(path);
    if (inferredIndex >= 0 && inferredIndex < m_requestedTransferStatuses.size()) {
        return m_requestedTransferStatuses.at(inferredIndex);
    }

    const QString lowerPath = path.toLower();
    static const QStringList audioMarkers = { // NOLINT(clang-diagnostic-exit-time-destructors)
        QStringLiteral(".m4a"), QStringLiteral(".mp3"), QStringLiteral(".aac"), QStringLiteral(".opus"), QStringLiteral(".ogg"), QStringLiteral(".flac"), QStringLiteral(".wav"), QStringLiteral(".weba"), QStringLiteral(".mpga")
    };
    static const QStringList videoMarkers = { // NOLINT(clang-diagnostic-exit-time-destructors)
        QStringLiteral(".mp4"), QStringLiteral(".m4v"), QStringLiteral(".mkv"), QStringLiteral(".webm"), QStringLiteral(".mov"), QStringLiteral(".avi"), QStringLiteral(".ts"), QStringLiteral(".m2ts")
    };

    for (const QString &marker : audioMarkers) {
        if (lowerPath.contains(marker)) {
            return tr("Downloading audio stream...");
        }
    }
    for (const QString &marker : videoMarkers) {
        if (lowerPath.contains(marker)) {
            if (requestedAudioExtraction()) {
                return tr("Downloading audio stream...");
            }
            return tr("Downloading video stream...");
        }
    }
    return QString();
}

QString YtDlpWorker::inferPrimaryStreamStatusFromMetadata(int index) const {
    if (m_requestedTransferStatuses.isEmpty()) {
        return tr("Downloading...");
    }
    const int boundedIndex = qBound(0, index, m_requestedTransferStatuses.size() - 1);
    return m_requestedTransferStatuses.at(boundedIndex);
}

void YtDlpWorker::updateInferredTransferStage(double percentage, double downloadedBytes, double totalBytes) {
    if (m_currentTransferIsAuxiliary) {
        return;
    }

    const int pathMatchedIndex = inferPrimaryStreamIndexFromPath(m_currentTransferTarget);
    if (pathMatchedIndex >= 0 && pathMatchedIndex < m_requestedTransferStatuses.size()) {
        if (m_inferredTransferIndex != pathMatchedIndex || m_currentTransferStatus != m_requestedTransferStatuses.at(pathMatchedIndex)) {
            m_inferredTransferIndex = pathMatchedIndex;
            m_currentTransferStatus = m_requestedTransferStatuses.at(pathMatchedIndex);
        }
    }

    const int sizeMatchedIndex = inferPrimaryStreamIndexFromTotalBytes(totalBytes);
    if (sizeMatchedIndex >= 0 && sizeMatchedIndex < m_requestedTransferStatuses.size()) {
        if (m_inferredTransferIndex != sizeMatchedIndex || m_currentTransferStatus != m_requestedTransferStatuses.at(sizeMatchedIndex)) {
            m_inferredTransferIndex = sizeMatchedIndex;
            m_currentTransferStatus = m_requestedTransferStatuses.at(sizeMatchedIndex);
            qDebug() << "[YtDlpWorker] Inferred transfer stage from total size" << totalBytes
                     << "-> index" << m_inferredTransferIndex << m_currentTransferStatus;
        }
    }

    if (m_inferredTransferIndex < 0) {
        m_inferredTransferIndex = 0;
        if (m_currentTransferStatus.isEmpty() || m_currentTransferStatus == tr("Downloading...")) {
            m_currentTransferStatus = inferPrimaryStreamStatusFromMetadata(m_inferredTransferIndex);
        }
    }

    const bool restartedAfterCompletion = m_lastPrimaryProgress >= 95.0 && percentage < 25.0;
    const bool totalDroppedMeaningfully = m_lastPrimaryTotalBytes > 0.0 && totalBytes > 0.0 && totalBytes < (m_lastPrimaryTotalBytes * 0.7);
    if (sizeMatchedIndex < 0 && (restartedAfterCompletion || totalDroppedMeaningfully) && m_requestedTransferStatuses.size() > 1 && m_inferredTransferIndex < m_requestedTransferStatuses.size() - 1) {
        ++m_inferredTransferIndex;
        m_currentTransferStatus = inferPrimaryStreamStatusFromMetadata(m_inferredTransferIndex);
        qDebug() << "[YtDlpWorker] Inferred transfer stage advanced to" << m_inferredTransferIndex << m_currentTransferStatus
                 << "after progress reset. Last progress:" << m_lastPrimaryProgress << "current:" << percentage
                 << "last total:" << m_lastPrimaryTotalBytes << "current total:" << totalBytes;
    }

    m_lastPrimaryProgress = percentage;
    if (totalBytes > 0.0) {
        m_lastPrimaryTotalBytes = totalBytes;
    }
}

bool YtDlpWorker::isAuxiliaryTransferTarget(const QString &path) const {
    const QString lowerPath = path.toLower();
    if (lowerPath.endsWith(QStringLiteral(".info.json")) || lowerPath.endsWith(QStringLiteral(".description"))) {
        return true;
    }

    const QString suffix = QFileInfo(path).suffix().toLower();
    static const QSet<QString> auxiliaryExtensions = { // NOLINT(clang-diagnostic-exit-time-destructors)
        QStringLiteral("json"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("webp"), QStringLiteral("avif"), QStringLiteral("gif"), QStringLiteral("vtt"), QStringLiteral("srt"), QStringLiteral("ass"), QStringLiteral("lrc"), QStringLiteral("sbv")
    };
    return auxiliaryExtensions.contains(suffix);
}

QString YtDlpWorker::statusForCurrentTransfer() const {
    return m_currentTransferStatus.isEmpty() ? tr("Downloading...") : m_currentTransferStatus;
}

void YtDlpWorker::emitStatusUpdate(const QString &status, int progress) {
    QVariantMap progressData;
    progressData[QStringLiteral("status")] = status;
    if (progress != -2) {
        progressData[QStringLiteral("progress")] = progress;
    }
    if (!m_videoTitle.isEmpty()) {
        progressData[QStringLiteral("title")] = m_videoTitle;
    }
    if (!m_thumbnailPath.isEmpty()) {
        progressData[QStringLiteral("thumbnail_path")] = m_thumbnailPath;
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
            progressData[QStringLiteral("current_file")] = currentFile;
        }
    emit progressUpdated(m_id, progressData);
}

bool YtDlpWorker::handleLifecycleStatusLine(const QString &line) {
    if (line.startsWith(QStringLiteral("[Merger]"))) {
        emitStatusUpdate(tr("Merging segments with ffmpeg..."), 100);
        return true;
    }
    if (line.startsWith(QStringLiteral("[ExtractAudio]"))) {
        emitStatusUpdate(tr("Extracting audio..."), 100);
        return true;
    }
    if (line.startsWith(QStringLiteral("[VideoConvertor]"))) {
        emitStatusUpdate(tr("Converting video format..."), 100);
        return true;
    }
    if (line.startsWith(QStringLiteral("[Metadata]"))) {
        emitStatusUpdate(tr("Applying metadata..."), 100);
        return true;
    }
    if (line.startsWith(QStringLiteral("[FixupM3u8]"))) {
        emitStatusUpdate(tr("Fixing stream timestamps..."), 100);
        return true;
    }
    if (line.startsWith(QStringLiteral("[youtube]")) || line.startsWith(QStringLiteral("[generic]")) || line.startsWith(QStringLiteral("[info]"))) {
        if (line.contains(QStringLiteral("Extracting URL"), Qt::CaseInsensitive) ||
            line.contains(QStringLiteral("Downloading webpage"), Qt::CaseInsensitive) ||
            line.contains(QStringLiteral("Downloading android"), Qt::CaseInsensitive) ||
            line.contains(QStringLiteral("Downloading player"), Qt::CaseInsensitive) ||
            line.contains(QStringLiteral("Downloading m3u8"), Qt::CaseInsensitive) ||
            line.contains(QStringLiteral("Downloading API JSON"), Qt::CaseInsensitive) ||
            line.contains(QStringLiteral("Downloading initial data API JSON"), Qt::CaseInsensitive) ||
            line.contains(QStringLiteral("Downloading tv client config"), Qt::CaseInsensitive) ||
            line.contains(QStringLiteral("Downloading web creator player API JSON"), Qt::CaseInsensitive) ||
            line.contains(QStringLiteral("Downloading ios player API JSON"), Qt::CaseInsensitive) ||
            line.contains(QStringLiteral("Solving JS challenges"), Qt::CaseInsensitive)) {
            emitStatusUpdate(tr("Extracting media information..."), -1);
            return true;
        }
    }
    return false;
}
