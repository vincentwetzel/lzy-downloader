#include "MetadataEmbedder.h"
#include "core/ConfigManager.h"
#include "core/ProcessUtils.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <chrono>
#include <QTimer>

MetadataEmbedder::MetadataEmbedder(ConfigManager *configManager, QObject *parent)
    : QObject(parent),
      m_process(new QProcess(this)),
      m_configManager(configManager),
      m_pendingTrackNumber(0),
      m_normalizeContainerTimestamps(false),
      m_targetDurationSeconds(0.0),
      m_stage(Stage::Idle) {
    ProcessUtils::setProcessEnvironment(*m_process);
    
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

void MetadataEmbedder::processFile(const QString &filePath, int trackNumber, bool normalizeContainerTimestamps) {
    m_originalFilePath = filePath;
    QFileInfo fileInfo(filePath);
    const QString suffix = fileInfo.suffix().toLower();

    if (!normalizeContainerTimestamps && suffix == QStringLiteral("opus") && trackNumber > 0 && m_extraMetadata.isEmpty()) {
        qDebug() << "Skipping metadata embedding for .opus file to preserve album art.";
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

    args << QStringLiteral("-nostdin");
    args << QStringLiteral("-i") << m_originalFilePath;
    args << QStringLiteral("-map") << QStringLiteral("0");
    args << QStringLiteral("-c") << QStringLiteral("copy");

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

    int lastDelimiter = qMax(buffer.lastIndexOf('\n'), buffer.lastIndexOf('\r'));
    if (lastDelimiter != -1) {
        m_processOutputTail += QString::fromUtf8(buffer.left(lastDelimiter + 1));
        buffer.remove(0, lastDelimiter + 1);
        constexpr qsizetype maxTailLength = 12000;
        if (m_processOutputTail.size() > maxTailLength) {
            m_processOutputTail = m_processOutputTail.right(maxTailLength);
        }
    }
    m_process->setProperty("lzy_utf8_buffer", buffer);
}
