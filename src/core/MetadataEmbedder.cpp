#include "MetadataEmbedder.h"
#include "core/ConfigManager.h"
#include "core/ProcessUtils.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

MetadataEmbedder::MetadataEmbedder(ConfigManager *configManager, QObject *parent)
    : QObject(parent),
      m_process(new QProcess(this)),
      m_configManager(configManager),
      m_pendingTrackNumber(0),
      m_normalizeContainerTimestamps(false),
      m_targetDurationSeconds(0.0),
      m_stage(Stage::Idle) {
    ProcessUtils::setProcessEnvironment(*m_process);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        appendProcessOutput(m_process->readAllStandardOutput());
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        appendProcessOutput(m_process->readAllStandardError());
    });
    connect(m_process, &QProcess::finished, this, &MetadataEmbedder::onProcessFinished);
}

void MetadataEmbedder::setExtraMetadata(const QVariantMap &metadata) {
    m_extraMetadata = metadata;
}

void MetadataEmbedder::processFile(const QString &filePath, int trackNumber, bool normalizeContainerTimestamps) {
    m_originalFilePath = filePath;
    QFileInfo fileInfo(filePath);
    const QString suffix = fileInfo.suffix().toLower();

    if (!normalizeContainerTimestamps && suffix == "opus" && trackNumber > 0 && m_extraMetadata.isEmpty()) {
        qDebug() << "Skipping metadata embedding for .opus file to preserve album art.";
        emit finished(true, "");
        return;
    }

    if (m_process->state() != QProcess::NotRunning || m_stage != Stage::Idle) {
        emit finished(false, "FFmpeg post-processing is already running.");
        return;
    }

    m_tempFilePath = fileInfo.absolutePath() + "/temp_" + fileInfo.fileName();
    m_pendingTrackNumber = trackNumber;
    m_targetDurationSeconds = 0.0;
    m_processOutputTail.clear();
    m_normalizeContainerTimestamps = normalizeContainerTimestamps &&
        (suffix == "mp4" || suffix == "m4v" || suffix == "mov" || suffix == "m4a");

    if (m_normalizeContainerTimestamps) {
        startDurationProbe();
    } else {
        startRewrite();
    }
}

void MetadataEmbedder::startDurationProbe() {
    const QString ffprobePath = ProcessUtils::findBinary("ffprobe", m_configManager).path;
    if (ffprobePath.isEmpty() || ffprobePath == "ffprobe") {
        qWarning() << "MetadataEmbedder: ffprobe not found; continuing without hard clip-duration trim.";
        startRewrite();
        return;
    }

    QStringList args;
    args << "-v" << "error"
         << "-show_entries" << "format=duration"
         << "-of" << "default=noprint_wrappers=1:nokey=1"
         << m_originalFilePath;

    m_stage = Stage::ProbingDuration;
    qDebug() << "MetadataEmbedder: probing clip duration with ffprobe" << args;
    m_process->start(ffprobePath, args);
}

void MetadataEmbedder::startRewrite() {
    QStringList args;
    if (m_normalizeContainerTimestamps) {
        args << "-fflags" << "+genpts";
        args << "-ignore_editlist" << "1";
        args << "-fix_sub_duration";
    }

    args << "-nostdin";
    args << "-i" << m_originalFilePath;
    args << "-map" << "0";
    args << "-c" << "copy";

    if (m_normalizeContainerTimestamps) {
        // Regenerate subtitle packets against the clipped container timeline.
        args << "-c:s" << "mov_text";
    }

    if (m_pendingTrackNumber > 0) {
        args << "-metadata" << QString("track=%1").arg(m_pendingTrackNumber);
    }

    for (auto it = m_extraMetadata.constBegin(); it != m_extraMetadata.constEnd(); ++it) {
        const QString value = it.value().toString().trimmed();
        if (!it.key().trimmed().isEmpty() && !value.isEmpty()) {
            args << "-metadata" << QString("%1=%2").arg(it.key(), value);
        }
    }

    if (m_normalizeContainerTimestamps) {
        if (m_targetDurationSeconds > 0.0) {
            args << "-t" << QString::number(m_targetDurationSeconds, 'f', 3);
        }
        args << "-shortest";
        qDebug() << "MetadataEmbedder: normalizing section clip metadata with hard duration limit" << m_targetDurationSeconds << "for" << m_originalFilePath;
    }

    args << "-y";
    args << m_tempFilePath;

    const QString ffmpegPath = ProcessUtils::findBinary("ffmpeg", m_configManager).path;
    m_stage = Stage::RewritingFile;
    qDebug() << "MetadataEmbedder: starting ffmpeg with args" << args;
    m_process->start(ffmpegPath, args);
}

void MetadataEmbedder::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    appendProcessOutput(m_process->readAllStandardOutput());
    appendProcessOutput(m_process->readAllStandardError());

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
    QString error;
    m_stage = Stage::Idle;

    if (success) {
        if (QFile::remove(m_originalFilePath)) {
            if (!QFile::rename(m_tempFilePath, m_originalFilePath)) {
                error = "Failed to rename temp file to original file.";
            }
        } else {
            error = "Failed to remove original file.";
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

    m_processOutputTail += QString::fromUtf8(data);
    constexpr qsizetype maxTailLength = 12000;
    if (m_processOutputTail.size() > maxTailLength) {
        m_processOutputTail = m_processOutputTail.right(maxTailLength);
    }
}
