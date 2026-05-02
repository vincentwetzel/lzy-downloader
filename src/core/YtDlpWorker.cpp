#include "YtDlpWorker.h"
#include "core/ConfigManager.h"
#include "core/ProcessUtils.h"

#include <QDebug>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <cmath> // For pow
#include <QTimer>
#include <QProcess>
#include <QVariantList>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QSet>
#include <limits>

YtDlpWorker::YtDlpWorker(const QString &id, const QStringList &args, ConfigManager *configManager, QObject *parent)
    : QObject(parent), m_id(id), m_args(args), m_configManager(configManager), m_process(nullptr), m_finishEmitted(false), m_errorEmitted(false), m_videoTitle(QString()),
      m_thumbnailPath(QString()), m_infoJsonPath(QString()), m_infoJsonRetryCount(0) {

    m_process = new QProcess(this);
    connect(m_process, &QProcess::finished, this, &YtDlpWorker::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &YtDlpWorker::onProcessError);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &YtDlpWorker::onReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &YtDlpWorker::onReadyReadStandardError);
}

void YtDlpWorker::start() {
    qDebug() << "[YtDlpWorker] start() called for ID:" << m_id;
    
    // Clear any leftover state from previous downloads
    m_fullMetadata.clear();
    m_requestedTransferStatuses.clear();
    m_requestedTransferFormatIds.clear();
    m_requestedTransferSizes.clear();
    m_currentTransferTarget.clear();
    m_currentTransferStatus.clear();
    m_currentTransferIsAuxiliary = false;
    m_inferredTransferIndex = -1;
    m_lastPrimaryProgress = -1.0;
    m_lastPrimaryTotalBytes = 0.0;

    const ProcessUtils::FoundBinary ytDlpBinary = ProcessUtils::findBinary("yt-dlp", m_configManager);
    if (ytDlpBinary.source == "Not Found" || ytDlpBinary.path.isEmpty()) {
        const QString message = "Download failed.\n"
                                "yt-dlp could not be found. Configure it in Advanced Settings -> External Tools.";
        qWarning() << message;
        if (!m_finishEmitted) {
            m_finishEmitted = true;
            emit finished(m_id, false, message, QString(), QString(), QVariantMap());
        }
        return;
    }

    const QString ytDlpPath = ytDlpBinary.path;
    const QString workingDirPath = QFileInfo(ytDlpPath).absolutePath();
    m_process->setWorkingDirectory(workingDirPath);
    ProcessUtils::setProcessEnvironment(*m_process);
    

    // Force yt-dlp to emit its native progress lines even when it is not attached
    // to a TTY. If an older caller still passed a custom progress template, drop
    // it so the worker can consistently parse yt-dlp's default output.
    int pt_index = m_args.indexOf("--progress-template");
    if (pt_index != -1) {
        m_args.removeAt(pt_index); // remove flag
        if (pt_index < m_args.size()) {
            m_args.removeAt(pt_index); // remove value
        }
    }
    if (!m_args.contains("--progress")) {
        m_args.prepend("--progress");
    }

    emitStatusUpdate("Extracting media information...", -1);

    qDebug() << "[YtDlpWorker] Binary path:" << ytDlpPath;
    qDebug() << "[YtDlpWorker] Working directory:" << workingDirPath;
    qDebug() << "[YtDlpWorker] Number of arguments:" << m_args.size();

    qDebug() << "Starting yt-dlp with path:" << ytDlpPath << "source:" << ytDlpBinary.source << "and arguments:" << m_args;
    qDebug() << "Working directory set to:" << workingDirPath;

    // Log full command for debugging
    QString fullCommand = "\"" + ytDlpPath + "\"";
    for (const QString &arg : m_args) {
        if (arg.contains(' ')) {
            fullCommand += " \"" + arg + "\"";
        } else {
            fullCommand += " " + arg;
        }
    }
    qDebug() << "Full yt-dlp command:" << fullCommand;
    
    // Connect state change signals for diagnostics
    connect(m_process, &QProcess::stateChanged, [this](QProcess::ProcessState state) {
        qDebug() << "[YtDlpWorker] Process state changed to:" << state;
    });
    connect(m_process, &QProcess::errorOccurred, [this](QProcess::ProcessError error) {
        qWarning() << "[YtDlpWorker] Process error occurred:" << error << m_process->errorString();
    });
    
    qDebug() << "[YtDlpWorker] Calling m_process->start()...";
    m_process->start(ytDlpPath, m_args);
    qDebug() << "[YtDlpWorker] start() returned. Process state:" << m_process->state() << "Process ID:" << m_process->processId();
    
    // Check if process started successfully
    if (m_process->state() == QProcess::NotRunning) {
        qWarning() << "[YtDlpWorker] ERROR: Process failed to start immediately!";
        qWarning() << "[YtDlpWorker] Process error:" << m_process->error() << m_process->errorString();
    } else if (m_process->state() == QProcess::Starting) {
        qDebug() << "[YtDlpWorker] Process is starting...";
    } else if (m_process->state() == QProcess::Running) {
        qDebug() << "[YtDlpWorker] Process is running. PID:" << m_process->processId();
    }
}

void YtDlpWorker::killProcess() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->disconnect(); // Prevent re-entrant read operations on the dying process buffer
        ProcessUtils::terminateProcessTree(m_process);
        m_process->kill(); // Forcefully kill the QProcess instance as fallback
    }
}

void YtDlpWorker::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (m_finishEmitted) {
        return;
    }

    // Process any remaining output. This is crucial for capturing the final file path.
    parseStandardOutput(m_process->readAllStandardOutput());
    // Also process any remaining stderr output
    parseStandardError(m_process->readAllStandardError());

    if (!m_outputBuffer.isEmpty()) {
        handleOutputLine(QString::fromUtf8(m_outputBuffer).trimmed()); // Trim any remaining buffer content
        m_outputBuffer.clear();
    }


    m_finishEmitted = true;
    const bool normalExit = (exitStatus == QProcess::NormalExit);
    const bool capturedFinalFileExists = !m_finalFilename.isEmpty() && QFileInfo::exists(m_finalFilename);
    const bool recoveredFromPostProcessorFailure = normalExit && exitCode != 0 && capturedFinalFileExists;
    bool success = (normalExit && exitCode == 0 && !m_finalFilename.isEmpty()) || recoveredFromPostProcessorFailure;
    QString message = success ? "Download completed successfully." : "Download failed.";
    QString postprocessorWarning;

    if (recoveredFromPostProcessorFailure) {
        message = "Download completed, but thumbnail/post-processing reported a warning.";
        if (!m_errorLines.isEmpty()) {
            message += "\n" + m_errorLines.join("\n").left(200);
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
            message += "\nCould not determine final filename.";
        }
    }

    QVariantMap metadata;
    if (success) {
        // Ensure metadata is loaded if it hasn't been asynchronously parsed yet
        if (m_fullMetadata.isEmpty() && !m_infoJsonPath.isEmpty() && QFile::exists(m_infoJsonPath)) {
            QFile jsonFile(m_infoJsonPath);
            if (jsonFile.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(jsonFile.readAll());
                if (doc.isObject()) {
                    m_fullMetadata = doc.object().toVariantMap();
                }
            }
        }

        // Use the full metadata that was already parsed from info.json during readInfoJsonWithRetry
        if (!m_fullMetadata.isEmpty()) {
            metadata = m_fullMetadata;
            qDebug() << "onProcessFinished: Using cached metadata with" << metadata.size() << "keys. Keys:" << metadata.keys();
            // Ensure m_videoTitle is consistent with what's in metadata
            if (metadata.contains("title") && m_videoTitle.isEmpty()) {
                m_videoTitle = metadata["title"].toString();
            }
            if (metadata.contains("uploader")) {
                qDebug() << "onProcessFinished: uploader from metadata:" << metadata["uploader"].toString();
            } else {
                qWarning() << "onProcessFinished: uploader NOT found in cached metadata!";
            }
        } else {
            qWarning() << "onProcessFinished: No cached metadata available. Sorting rules may not work.";
        }

        // CRITICAL FIX: Emit 100% progress update before finished signal to ensure
        // the UI progress bar reaches 100% and turns green, not stuck at <100%
        QVariantMap finalProgressData;
        finalProgressData["progress"] = 100;
        finalProgressData["status"] = "Complete";
        if (!m_videoTitle.isEmpty()) {
            finalProgressData["title"] = m_videoTitle;
        }
        if (!m_thumbnailPath.isEmpty()) {
            finalProgressData["thumbnail_path"] = m_thumbnailPath;
        }
        emit progressUpdated(m_id, finalProgressData);
    }

    if (!success) {
        if (!m_errorLines.isEmpty()) {
            message += "\n" + m_errorLines.join("\n").left(200);
        } else {
            // Fallback: Ensure stderr is also read as UTF-8
            QString errorOutput = QString::fromUtf8(m_process->readAllStandardError());
            if (!errorOutput.isEmpty()) {
                message += "\n" + errorOutput.left(200);
            }
        }
    }

    // Ensure m_videoTitle is included in the metadata for the finished signal
    if (!m_videoTitle.isEmpty() && !metadata.contains("title")) {
        metadata["title"] = m_videoTitle;
    }
    if (!m_thumbnailPath.isEmpty() && !metadata.contains("thumbnail_path")) {
        metadata["thumbnail_path"] = m_thumbnailPath;
    }
    if (!postprocessorWarning.isEmpty()) {
        metadata["postprocessor_warning"] = postprocessorWarning;
    }

    emit finished(m_id, success, message, m_finalFilename, m_originalDownloadedFilename, metadata);
}


void YtDlpWorker::onProcessError(QProcess::ProcessError error) {
    if (m_finishEmitted) {
        return;
    }

    if (error == QProcess::FailedToStart || error == QProcess::Crashed ||
        error == QProcess::ReadError || error == QProcess::WriteError) {
        m_finishEmitted = true;
        const QString message = QString("Download failed.\n"
                                        "Failed to start yt-dlp process (%1): %2")
                                    .arg(static_cast<int>(error))
                                    .arg(m_process->errorString());
        qWarning() << message;
        emit finished(m_id, false, message, QString(), QString(), QVariantMap());
    }
}

void YtDlpWorker::onReadyReadStandardOutput() {
    QByteArray data = m_process->readAllStandardOutput();
    qDebug() << "[STDOUT] Received" << data.size() << "bytes:" << QString::fromUtf8(data).left(300);
    parseStandardOutput(data);
}

void YtDlpWorker::onReadyReadStandardError() {
    QByteArray data = m_process->readAllStandardError();
    qDebug() << "[STDERR] Received" << data.size() << "bytes:" << QString::fromUtf8(data).left(300);
    parseStandardError(data);
}

void YtDlpWorker::parseStandardOutput(const QByteArray &output) {
    m_outputBuffer.append(output);

    // Find the last complete line ending
    int lastNewline = m_outputBuffer.lastIndexOf('\n');
    int lastCarriageReturn = m_outputBuffer.lastIndexOf('\r');
    int lastDelimiter = qMax(lastNewline, lastCarriageReturn);

    if (lastDelimiter == -1) {
        // No complete line yet, wait for more data
        return;
    }

    // Extract all complete lines
    QByteArray completeData = m_outputBuffer.left(lastDelimiter + 1);
    m_outputBuffer.remove(0, lastDelimiter + 1);

    // Split and process lines, skipping empty parts
    QStringList lines = QString::fromUtf8(completeData).split(QRegularExpression("[\\r\\n]"), Qt::SkipEmptyParts);

    for (const QString &line : lines) {
        handleOutputLine(line.trimmed()); // Ensure each line is trimmed
    }
}

void YtDlpWorker::parseStandardError(const QByteArray &output) {
    m_errorBuffer.append(output);
    qDebug() << "parseStandardError called. Current buffer size:" << m_errorBuffer.size();

    int lastNewline = m_errorBuffer.lastIndexOf('\n');
    int lastCarriageReturn = m_errorBuffer.lastIndexOf('\r');
    int lastDelimiter = qMax(lastNewline, lastCarriageReturn);

    if (lastDelimiter == -1) {
        return;
    }

    QByteArray completeData = m_errorBuffer.left(lastDelimiter + 1);
    m_errorBuffer.remove(0, lastDelimiter + 1);

    QStringList lines = QString::fromUtf8(completeData).split(QRegularExpression("[\\r\\n]"), Qt::SkipEmptyParts);

    for (const QString &line : lines) {
        qDebug() << "Processing stderr line:" << line.trimmed();
        handleOutputLine(line.trimmed());
    }
}

void YtDlpWorker::readInfoJsonWithRetry() {
    qDebug() << "readInfoJsonWithRetry: Attempting to read info.json. Path:" << m_infoJsonPath << "Retry:" << m_infoJsonRetryCount;

    if (m_infoJsonPath.isEmpty()) {
        qDebug() << "readInfoJsonWithRetry: No info.json path set.";
        return;
    }

    QFile jsonFile(m_infoJsonPath);
    if (!jsonFile.exists()) {
        qDebug() << "readInfoJsonWithRetry: info.json file does not exist yet at:" << m_infoJsonPath;
        if (m_infoJsonRetryCount < 5) { // Retry up to 5 times
            m_infoJsonRetryCount++;
            QTimer::singleShot(500, this, &YtDlpWorker::readInfoJsonWithRetry); // Retry after 500ms
            qDebug() << "readInfoJsonWithRetry: Retrying in 500ms. Attempt:" << m_infoJsonRetryCount;
        } else {
            qWarning() << "readInfoJsonWithRetry: Max retries reached for info.json. File not found at:" << m_infoJsonPath;
            m_infoJsonPath.clear(); // Give up
        }
        return;
    }

    if (!jsonFile.open(QIODevice::ReadOnly)) {
        qWarning() << "readInfoJsonWithRetry: Could not open info.json file at:" << m_infoJsonPath << "Error:" << jsonFile.errorString();
        if (m_infoJsonRetryCount < 5) { // Retry up to 5 times
            m_infoJsonRetryCount++;
            QTimer::singleShot(500, this, &YtDlpWorker::readInfoJsonWithRetry); // Retry after 500ms
            qDebug() << "readInfoJsonWithRetry: Retrying in 500ms. Attempt:" << m_infoJsonRetryCount;
        } else {
            qWarning() << "readInfoJsonWithRetry: Max retries reached for info.json. Could not open file at:" << m_infoJsonPath;
            m_infoJsonPath.clear(); // Give up
        }
        return;
    }

    QByteArray jsonData = jsonFile.readAll();
    qDebug() << "readInfoJsonWithRetry: Successfully opened and read info.json. Data size:" << jsonData.size();
    jsonFile.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);

    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "readInfoJsonWithRetry: Failed to parse info.json as JSON or it's not an object. Error:" << parseError.errorString();
        if (m_infoJsonRetryCount < 5) { // Retry up to 5 times
            m_infoJsonRetryCount++;
            QTimer::singleShot(500, this, &YtDlpWorker::readInfoJsonWithRetry); // Retry after 500ms
            qDebug() << "readInfoJsonWithRetry: Retrying in 500ms. Attempt:" << m_infoJsonRetryCount;
        } else {
            qWarning() << "readInfoJsonWithRetry: Max retries reached for info.json. Invalid JSON at:" << m_infoJsonPath;
            m_infoJsonPath.clear(); // Give up
        }
        return;
    }

    // If we successfully parsed the file, we don't need to retry anymore.
    m_infoJsonPath.clear();
    m_infoJsonRetryCount = 0;

    QJsonObject obj = doc.object();
    QVariantMap updateData;

    // Store the full metadata for use in onProcessFinished.
    // Important: only replace inferred transfer ordering when info.json actually
    // provides requested_downloads. Some yt-dlp runs omit that field entirely,
    // and clearing our earlier stderr-derived mapping causes audio handoff labels
    // to briefly switch correctly and then regress back to "video".
    m_fullMetadata = obj.toVariantMap();
    const QVariantList requestedDownloads = m_fullMetadata.value("requested_downloads").toList();
    if (!requestedDownloads.isEmpty()) {
        m_requestedTransferStatuses.clear();
        m_requestedTransferFormatIds.clear();
        m_requestedTransferSizes.clear();
    }
    for (const QVariant &requestedDownload : requestedDownloads) {
        const QVariantMap requestMap = requestedDownload.toMap();
        const QString vcodec = requestMap.value("vcodec").toString();
        const QString acodec = requestMap.value("acodec").toString();
        const QString formatId = requestMap.value("format_id").toString().trimmed();
        if (!vcodec.isEmpty() && vcodec != "none" && (acodec.isEmpty() || acodec == "none")) {
            m_requestedTransferStatuses.append("Downloading video stream...");
            m_requestedTransferFormatIds.append(formatId);
            m_requestedTransferSizes.append(inferPrimaryStreamSizeBytes(requestMap));
        } else if (!acodec.isEmpty() && acodec != "none" && (vcodec.isEmpty() || vcodec == "none")) {
            m_requestedTransferStatuses.append("Downloading audio stream...");
            m_requestedTransferFormatIds.append(formatId);
            m_requestedTransferSizes.append(inferPrimaryStreamSizeBytes(requestMap));
        } else if ((!vcodec.isEmpty() && vcodec != "none") || (!acodec.isEmpty() && acodec != "none")) {
            m_requestedTransferStatuses.append("Downloading media stream...");
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

    if (m_videoTitle.isEmpty() && obj.contains("title") && obj["title"].isString()) {
        m_videoTitle = obj["title"].toString();
        updateData["title"] = m_videoTitle;
        qDebug() << "Extracted title from info.json:" << m_videoTitle;
    }
    
    // Extract thumbnail path if available from the info.json
    if (m_thumbnailPath.isEmpty() && obj.contains("thumbnails") && obj["thumbnails"].isArray()) {
        QJsonArray thumbnails = obj["thumbnails"].toArray();
        // yt-dlp adds a "filepath" key to the thumbnail entry it downloaded.
        for (const QJsonValue &thumbValue : thumbnails) {
            QJsonObject thumbObj = thumbValue.toObject();
            if (thumbObj.contains("filepath") && thumbObj["filepath"].isString()) {
                m_thumbnailPath = QDir::toNativeSeparators(thumbObj["filepath"].toString());
                updateData["thumbnail_path"] = m_thumbnailPath;
                qDebug() << "Extracted thumbnail path from info.json:" << m_thumbnailPath;
                break; // Found it
            }
        }
    }

    if (!updateData.isEmpty()) {
        emit progressUpdated(m_id, updateData);
    }
}

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

void YtDlpWorker::updateTransferTarget(const QString &path) {
    m_currentTransferTarget = QDir::toNativeSeparators(path);
    m_currentTransferIsAuxiliary = isAuxiliaryTransferTarget(m_currentTransferTarget);

    if (m_currentTransferIsAuxiliary) {
        const QString lowerPath = m_currentTransferTarget.toLower();
        if (lowerPath.endsWith(".info.json")) {
            m_currentTransferStatus = "Downloading metadata...";
        } else if (lowerPath.contains(".jpg") || lowerPath.contains(".jpeg") || lowerPath.contains(".png") || lowerPath.contains(".webp") || lowerPath.contains(".avif")) {
            m_currentTransferStatus = "Downloading thumbnail...";
        } else if (lowerPath.contains(".srt") || lowerPath.contains(".vtt") || lowerPath.contains(".ass") || lowerPath.contains(".lrc") || lowerPath.contains(".sbv")) {
            m_currentTransferStatus = "Downloading subtitles...";
        } else {
            m_currentTransferStatus = "Downloading auxiliary file...";
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
                if (m_currentTransferStatus.contains("video", Qt::CaseInsensitive)) {
                    m_inferredTransferIndex = 0;
                } else if (m_currentTransferStatus.contains("audio", Qt::CaseInsensitive) && m_requestedTransferStatuses.size() > 1) {
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
    const double exactSize = requestMap.value("filesize").toDouble();
    if (exactSize > 0.0) {
        return exactSize;
    }
    const double approxSize = requestMap.value("filesize_approx").toDouble();
    if (approxSize > 0.0) {
        return approxSize;
    }
    return 0.0;
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
            m_requestedTransferStatuses.append(QStringLiteral("Downloading media stream..."));
        } else if (i == 0) {
            m_requestedTransferStatuses.append(QStringLiteral("Downloading video stream..."));
        } else {
            m_requestedTransferStatuses.append(QStringLiteral("Downloading audio stream..."));
        }
        m_requestedTransferSizes.append(0.0);
    }

    qDebug() << "[YtDlpWorker] Seeded requested transfers from format list:" << m_requestedTransferFormatIds << m_requestedTransferStatuses;
}

bool YtDlpWorker::handleAria2CommandLine(const QString &line) {
    if (!line.startsWith("[debug] aria2c.exe command line:", Qt::CaseInsensitive)) {
        return false;
    }

    static const QRegularExpression outRegex(R"(--out\s+"[^"]*\.f([^\."]+)\.[^"]*")", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression itagRegex(R"([?&]itag=(\d+))", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression mimeRegex(R"([?&]mime=([^&"]+))", QRegularExpression::CaseInsensitiveOption);

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
                if (mimeValue.startsWith("audio/")) {
                    m_requestedTransferFormatIds.append(itag);
                    m_requestedTransferStatuses.append(QStringLiteral("Downloading audio stream..."));
                    m_requestedTransferSizes.append(0.0);
                } else if (mimeValue.startsWith("video/")) {
                    m_requestedTransferFormatIds.append(itag);
                    m_requestedTransferStatuses.append(QStringLiteral("Downloading video stream..."));
                    m_requestedTransferSizes.append(0.0);
                }
            }
        }
    }

    if (!formatId.isEmpty()) {
        const int pathIndex = inferPrimaryStreamIndexFromPath(QString("dummy.f%1.part").arg(formatId));
        if (pathIndex >= 0 && pathIndex < m_requestedTransferStatuses.size()) {
            m_inferredTransferIndex = pathIndex;
            m_currentTransferStatus = m_requestedTransferStatuses.at(pathIndex);
        }
    }

    if (mimeValue.startsWith("audio/")) {
        m_currentTransferStatus = QStringLiteral("Downloading audio stream...");
        if (m_requestedTransferStatuses.size() > 1) {
            m_inferredTransferIndex = qMax(1, m_inferredTransferIndex);
        }
    } else if (mimeValue.startsWith("video/")) {
        m_currentTransferStatus = QStringLiteral("Downloading video stream...");
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

    static const QRegularExpression formatIdRegex(R"(\.f([A-Za-z0-9-]+)\.)", QRegularExpression::CaseInsensitiveOption);
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
    static const QStringList audioMarkers = {
        ".m4a", ".mp3", ".aac", ".opus", ".ogg", ".flac", ".wav", ".weba", ".mpga"
    };
    static const QStringList videoMarkers = {
        ".mp4", ".m4v", ".mkv", ".webm", ".mov", ".avi", ".ts", ".m2ts"
    };

    for (const QString &marker : audioMarkers) {
        if (lowerPath.contains(marker)) {
            return QStringLiteral("Downloading audio stream...");
        }
    }
    for (const QString &marker : videoMarkers) {
        if (lowerPath.contains(marker)) {
            if (requestedAudioExtraction()) {
                return QStringLiteral("Downloading audio stream...");
            }
            return QStringLiteral("Downloading video stream...");
        }
    }
    return QString();
}

QString YtDlpWorker::inferPrimaryStreamStatusFromMetadata(int index) const {
    if (m_requestedTransferStatuses.isEmpty()) {
        return QStringLiteral("Downloading...");
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
        if (m_currentTransferStatus.isEmpty() || m_currentTransferStatus == "Downloading...") {
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
    if (lowerPath.endsWith(".info.json") || lowerPath.endsWith(".description")) {
        return true;
    }

    const QString suffix = QFileInfo(path).suffix().toLower();
    static const QSet<QString> auxiliaryExtensions = {
        "json", "jpg", "jpeg", "png", "webp", "avif", "gif", "vtt", "srt", "ass", "lrc", "sbv"
    };
    return auxiliaryExtensions.contains(suffix);
}

QString YtDlpWorker::statusForCurrentTransfer() const {
    return m_currentTransferStatus.isEmpty() ? QStringLiteral("Downloading...") : m_currentTransferStatus;
}

void YtDlpWorker::emitStatusUpdate(const QString &status, int progress) {
    QVariantMap progressData;
    progressData["status"] = status;
    if (progress != -2) {
        progressData["progress"] = progress;
    }
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
}

bool YtDlpWorker::handleLifecycleStatusLine(const QString &line) {
    if (line.startsWith("[Merger]")) {
        emitStatusUpdate("Merging segments with ffmpeg...", 100);
        return true;
    }
    if (line.startsWith("[ExtractAudio]")) {
        emitStatusUpdate("Extracting audio...", 100);
        return true;
    }
    if (line.startsWith("[VideoConvertor]")) {
        emitStatusUpdate("Converting video format...", 100);
        return true;
    }
    if (line.startsWith("[Metadata]")) {
        emitStatusUpdate("Applying metadata...", 100);
        return true;
    }
    if (line.startsWith("[FixupM3u8]")) {
        emitStatusUpdate("Fixing stream timestamps...", 100);
        return true;
    }
    if (line.startsWith("[youtube]") || line.startsWith("[generic]") || line.startsWith("[info]")) {
        if (line.contains("Extracting URL", Qt::CaseInsensitive) ||
            line.contains("Downloading webpage", Qt::CaseInsensitive) ||
            line.contains("Downloading android", Qt::CaseInsensitive) ||
            line.contains("Downloading player", Qt::CaseInsensitive) ||
            line.contains("Downloading m3u8", Qt::CaseInsensitive) ||
            line.contains("Downloading API JSON", Qt::CaseInsensitive) ||
            line.contains("Downloading initial data API JSON", Qt::CaseInsensitive) ||
            line.contains("Downloading tv client config", Qt::CaseInsensitive) ||
            line.contains("Downloading web creator player API JSON", Qt::CaseInsensitive) ||
            line.contains("Downloading ios player API JSON", Qt::CaseInsensitive) ||
            line.contains("Solving JS challenges", Qt::CaseInsensitive)) {
            emitStatusUpdate("Extracting media information...", -1);
            return true;
        }
    }
    return false;
}

QString YtDlpWorker::normalizeConsoleLine(const QString &line) const {
    QString normalized = line;
    static const QRegularExpression ansiRegex(R"(\x1B\[[0-9;]*[A-Za-z])");
    normalized.remove(ansiRegex);
    return normalized.trimmed();
}
