#include "MetadataEmbedder.h"
#include "ArtworkNormalizer.h"
#include "core/ConfigManager.h"
#include "core/ProcessUtils.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <chrono>
#include <QTimer>

namespace {
bool isAudioOnlySuffix(const QString &suffix)
{
    return suffix == QLatin1String("mp3") ||
           suffix == QLatin1String("m4a") ||
           suffix == QLatin1String("mka") ||
           suffix == QLatin1String("wav") ||
           suffix == QLatin1String("flac") ||
           suffix == QLatin1String("opus") ||
           suffix == QLatin1String("ogg") ||
           suffix == QLatin1String("aac");
}
}

bool MetadataEmbedder::supportsAttachedPicture(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    return suffix == QLatin1String("mp3") ||
           suffix == QLatin1String("m4a") ||
           suffix == QLatin1String("mka") ||
           suffix == QLatin1String("mkv") ||
           suffix == QLatin1String("flac") ||
           suffix == QLatin1String("mp4") ||
           suffix == QLatin1String("m4v") ||
           suffix == QLatin1String("mov");
}

MetadataEmbedder::MetadataEmbedder(ConfigManager *configManager, QObject *parent)
    : QObject(parent),
      m_process(new QProcess(this)),
      m_configManager(configManager),
      m_pendingTrackNumber(0),
      m_normalizeContainerTimestamps(false),
      m_targetDurationSeconds(0.0),
      m_stage(Stage::Idle) {
    ProcessUtils::setProcessEnvironment(*m_process);
    connect(m_process, &QProcess::started, this, [this]() {
        ProcessUtils::setBackgroundProcessPriority(*m_process);
        m_processTimer.start();
        m_lastProgressLogMs = -1;
        qInfo() << "[MetadataEmbedder][ffmpeg stage] started"
                << "pid=" << m_process->processId()
                << "stage=" << (m_stage == Stage::ProbingDuration ? QStringLiteral("probe_duration") : QStringLiteral("rewrite_file"));
    });
    
    QTimer *watchdog = new QTimer(this);
    watchdog->setInterval(std::chrono::seconds(60)); // 60 seconds of inactivity timeout
    connect(watchdog, &QTimer::timeout, this, [this]() {
        qWarning() << "MetadataEmbedder process timed out. Terminating.";
        ProcessUtils::terminateProcessTree(m_process);
        m_process->kill();
    });
    connect(m_process, &QProcess::started, watchdog, [watchdog]() { watchdog->start(); });
    connect(m_process, &QProcess::finished, watchdog, [watchdog]() { watchdog->stop(); });

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this, watchdog]() {
        watchdog->start(); // Reset timer
        appendProcessOutput(m_process->readAllStandardOutput());
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this, watchdog]() {
        watchdog->start(); // Reset timer
        appendProcessOutput(m_process->readAllStandardError());
    });
    connect(m_process, &QProcess::finished, this, &MetadataEmbedder::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_stage = Stage::Idle;
            emit finished(false, tr("Failed to start ffprobe/ffmpeg process. Please check your configuration."));
        }
    });
}

void MetadataEmbedder::setExtraMetadata(const QVariantMap &metadata) {
    m_extraMetadata = metadata;
}

void MetadataEmbedder::setThumbnailPath(const QString &thumbnailPath) {
    m_thumbnailPath = thumbnailPath.trimmed();
}

void MetadataEmbedder::cancel()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        ProcessUtils::terminateProcessTree(m_process);
        m_process->kill();
    }
    m_stage = Stage::Idle;
}

void MetadataEmbedder::processFile(const QString &filePath, int trackNumber, bool normalizeContainerTimestamps) {
    m_originalFilePath = filePath;
    QFileInfo fileInfo(filePath);
    const QString suffix = fileInfo.suffix().toLower();
    const bool hasThumbnail = !m_thumbnailPath.isEmpty() && QFile::exists(m_thumbnailPath);
    const bool hasEmbeddableThumbnail = hasThumbnail && supportsAttachedPicture(filePath);

    const bool audioOnly = isAudioOnlySuffix(suffix);
    if (hasEmbeddableThumbnail && audioOnly) {
        const bool normalized = ArtworkNormalizer::normalizeFile(m_thumbnailPath);
        qDebug() << "MetadataEmbedder: automatic artwork border normalization"
                 << (normalized ? "cropped detected borders in" : "left unchanged")
                 << m_thumbnailPath;
    } else if (hasThumbnail && !hasEmbeddableThumbnail) {
        qInfo() << "MetadataEmbedder: container does not support attached artwork; leaving thumbnail external"
                << m_thumbnailPath << "for" << filePath;
    }

    if (!normalizeContainerTimestamps && m_extraMetadata.isEmpty() && !hasEmbeddableThumbnail &&
        ((suffix == QStringLiteral("opus") && trackNumber > 0) || trackNumber == 0)) {
        qDebug() << "Skipping metadata embedding because no metadata rewrite or usable thumbnail is needed.";
        emit finished(true, "");
        return;
    }

    if (m_process->state() != QProcess::NotRunning || m_stage != Stage::Idle) {
        emit finished(false, tr("FFmpeg post-processing is already running."));
        return;
    }

    m_tempFilePath = QDir(fileInfo.absolutePath()).filePath(QStringLiteral("temp_%1").arg(fileInfo.fileName()));
    m_pendingTrackNumber = trackNumber;
    m_targetDurationSeconds = 0.0;
    m_processOutputTail.clear();
    m_lastProgressFrame.clear();
    m_lastProgressTime.clear();
    m_lastProgressSpeed.clear();
    m_normalizeContainerTimestamps = normalizeContainerTimestamps &&
        (suffix == QStringLiteral("mp4") || suffix == QStringLiteral("m4v") || suffix == QStringLiteral("mov") || suffix == QStringLiteral("m4a"));

    if (m_normalizeContainerTimestamps) {
        startDurationProbe();
    } else {
        startRewrite();
    }
}

void MetadataEmbedder::startDurationProbe() {
    const QString ffprobePath = ProcessUtils::findBinary(QStringLiteral("ffprobe"), m_configManager).path;
    if (ffprobePath.isEmpty() || ffprobePath == QStringLiteral("ffprobe")) {
        qWarning() << "MetadataEmbedder: ffprobe not found; continuing without hard clip-duration trim.";
        startRewrite();
        return;
    }

    QStringList args;
    args << QStringLiteral("-v") << QStringLiteral("error")
         << QStringLiteral("-show_entries") << QStringLiteral("format=duration")
         << QStringLiteral("-of") << QStringLiteral("default=noprint_wrappers=1:nokey=1")
         << m_originalFilePath;

    m_stage = Stage::ProbingDuration;
    qDebug() << "MetadataEmbedder: probing clip duration with ffprobe" << args;
    m_process->start(ffprobePath, args);
}

void MetadataEmbedder::startRewrite() {
    QStringList args;
    if (m_normalizeContainerTimestamps) {
        args << QStringLiteral("-fflags") << QStringLiteral("+genpts");
        args << QStringLiteral("-ignore_editlist") << QStringLiteral("1");
        args << QStringLiteral("-fix_sub_duration");
    }

    args << QStringLiteral("-nostdin")
         << QStringLiteral("-progress") << QStringLiteral("pipe:2")
         << QStringLiteral("-stats_period") << QStringLiteral("1");
    args << QStringLiteral("-i") << m_originalFilePath;
    const bool hasThumbnail = !m_thumbnailPath.isEmpty()
        && QFile::exists(m_thumbnailPath)
        && supportsAttachedPicture(m_originalFilePath);
    if (hasThumbnail) {
        args << QStringLiteral("-i") << m_thumbnailPath;
    }
    const QString extension = QFileInfo(m_originalFilePath).suffix().toLower();
    const bool audioOnly = isAudioOnlySuffix(extension);
    // For audio, replace any picture stream yt-dlp may already have embedded
    // rather than leaving the old artwork alongside the normalized sidecar.
    // Other media keeps the existing all-stream mapping behavior.
    args << QStringLiteral("-map") << (hasThumbnail && audioOnly ? QStringLiteral("0:a?") : QStringLiteral("0"));
    if (hasThumbnail) {
        args << QStringLiteral("-map") << QStringLiteral("1:0");
    }
    args << QStringLiteral("-c") << QStringLiteral("copy");

    if (hasThumbnail) {
        const int artworkVideoIndex = audioOnly ? 0 : 1;
        args << QStringLiteral("-disposition:v:%1").arg(artworkVideoIndex) << QStringLiteral("attached_pic");
        qDebug() << "MetadataEmbedder: attaching abandoned thumbnail" << m_thumbnailPath
                 << "as video stream" << artworkVideoIndex;
    }

    if (m_normalizeContainerTimestamps) {
        // Regenerate subtitle packets against the clipped container timeline.
        args << QStringLiteral("-c:s") << QStringLiteral("mov_text");
    }

    if (m_pendingTrackNumber > 0) {
        args << QStringLiteral("-metadata") << QStringLiteral("track=%1").arg(m_pendingTrackNumber);
    }

    for (auto it = m_extraMetadata.constBegin(); it != m_extraMetadata.constEnd(); ++it) {
        const QString value = it.value().toString().trimmed();
        if (!it.key().trimmed().isEmpty() && !value.isEmpty()) {
            args << QStringLiteral("-metadata") << QStringLiteral("%1=%2").arg(it.key(), value);
        }
    }

    if (m_normalizeContainerTimestamps) {
        if (m_targetDurationSeconds > 0.0) {
            args << QStringLiteral("-t") << QString::number(m_targetDurationSeconds, 'f', 3);
        }
        args << QStringLiteral("-shortest");
        qDebug() << "MetadataEmbedder: normalizing section clip metadata with hard duration limit" << m_targetDurationSeconds << "for" << m_originalFilePath;
    }

    args << QStringLiteral("-y");
    args << m_tempFilePath;

    const QString ffmpegPath = ProcessUtils::findBinary(QStringLiteral("ffmpeg"), m_configManager).path;
    m_stage = Stage::RewritingFile;
    qDebug() << "MetadataEmbedder: starting ffmpeg with args" << args;
    m_process->start(ffmpegPath, args);
}

void MetadataEmbedder::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    appendProcessOutput(m_process->readAllStandardOutput());
    appendProcessOutput(m_process->readAllStandardError());

    QByteArray buffer = m_process->property("lzy_utf8_buffer").toByteArray();
    if (!buffer.isEmpty()) {
        m_processOutputTail += QString::fromUtf8(buffer);
        m_process->setProperty("lzy_utf8_buffer", QByteArray());
        constexpr qsizetype maxTailLength = 12000;
        if (m_processOutputTail.size() > maxTailLength) {
            m_processOutputTail = m_processOutputTail.right(maxTailLength);
        }
    }

    if (m_stage == Stage::ProbingDuration) {
        const QString output = m_processOutputTail.trimmed();
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            bool ok = false;
            const double duration = output.toDouble(&ok);
            if (ok && duration > 0.0) {
                m_targetDurationSeconds = duration;
                qDebug() << "MetadataEmbedder: ffprobe clip duration =" << m_targetDurationSeconds;
            } else {
                qWarning() << "MetadataEmbedder: could not parse ffprobe duration output:" << output;
            }
        } else {
            qWarning() << "MetadataEmbedder: ffprobe failed, continuing without hard trim:" << m_processOutputTail;
        }

        m_processOutputTail.clear();
        startRewrite();
        return;
    }

    const bool success = (exitStatus == QProcess::NormalExit && exitCode == 0);
    qInfo() << "[MetadataEmbedder][ffmpeg stage] finished"
            << "elapsed_ms=" << (m_processTimer.isValid() ? m_processTimer.elapsed() : -1)
            << "exit_code=" << exitCode << "success=" << success
            << "frame=" << m_lastProgressFrame
            << "out_time=" << m_lastProgressTime
            << "speed=" << m_lastProgressSpeed;
    QString error;
    m_stage = Stage::Idle;

    if (success) {
        if (QFile::remove(m_originalFilePath)) {
            if (!QFile::rename(m_tempFilePath, m_originalFilePath)) {
                if (!QFile::copy(m_tempFilePath, m_originalFilePath)) {
                    error = tr("Failed to rename or copy temp file to original file. The file is at: %1").arg(m_tempFilePath);
                } else {
                    QFile::remove(m_tempFilePath);
                }
            }
        } else {
            error = tr("Failed to remove original file to replace it.");
            QFile::remove(m_tempFilePath);
        }
    } else {
        error = m_processOutputTail;
        QFile::remove(m_tempFilePath);
    }

    emit finished(error.isEmpty(), error);
}

void MetadataEmbedder::appendProcessOutput(const QByteArray &data)
{
    if (data.isEmpty()) {
        return;
    }

    QByteArray buffer = m_process->property("lzy_utf8_buffer").toByteArray();
    buffer.append(data);

    qsizetype lastDelimiter = qMax(buffer.lastIndexOf('\n'), buffer.lastIndexOf('\r'));
    if (lastDelimiter != -1) {
        const QByteArray completeLines = buffer.left(lastDelimiter + 1);
        QByteArray normalizedLines = completeLines;
        normalizedLines.replace('\r', '\n');
        const QList<QByteArray> lines = normalizedLines.split('\n');
        for (QByteArray line : lines) {
            line = line.trimmed();
            const qsizetype separator = line.indexOf('=');
            if (separator <= 0) {
                continue;
            }
            const QString key = QString::fromLatin1(line.left(separator));
            const QString value = QString::fromUtf8(line.mid(separator + 1));
            if (key == QLatin1String("frame")) m_lastProgressFrame = value;
            else if (key == QLatin1String("out_time_ms")) m_lastProgressTime = value;
            else if (key == QLatin1String("speed")) m_lastProgressSpeed = value;
            if (m_processTimer.isValid() && m_processTimer.elapsed() - m_lastProgressLogMs >= 5000) {
                qDebug() << "[MetadataEmbedder][ffmpeg progress]"
                         << "elapsed_ms=" << m_processTimer.elapsed()
                         << "frame=" << m_lastProgressFrame
                         << "out_time_ms=" << m_lastProgressTime
                         << "speed=" << m_lastProgressSpeed;
                m_lastProgressLogMs = m_processTimer.elapsed();
            }
        }
        m_processOutputTail += QString::fromUtf8(completeLines);
        buffer.remove(0, lastDelimiter + 1);
        constexpr qsizetype maxTailLength = 12000;
        if (m_processOutputTail.size() > maxTailLength) {
            m_processOutputTail = m_processOutputTail.right(maxTailLength);
        }
    }
    m_process->setProperty("lzy_utf8_buffer", buffer);
}
