#include "FfmpegPostProcessor.h"
#include "core/ConfigManager.h"
#include "core/ProcessUtils.h"
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <chrono>
#include <QTimer>

FfmpegPostProcessor::FfmpegPostProcessor(ConfigManager *configManager, QObject *parent)
    : QObject(parent), m_configManager(configManager)
{
    m_process = new QProcess(this);
    ProcessUtils::setProcessEnvironment(*m_process);

    QTimer *watchdog = new QTimer(this);
    watchdog->setInterval(std::chrono::seconds(60)); // 60 seconds of inactivity timeout
    connect(watchdog, &QTimer::timeout, this, [this]() {
        qWarning() << "FfmpegPostProcessor process timed out. Terminating.";
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
    connect(m_process, &QProcess::finished, this, &FfmpegPostProcessor::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &FfmpegPostProcessor::onProcessError);
}

void FfmpegPostProcessor::embedTrackNumber(const QString &filePath, int trackNumber, int totalTracks)
{
    if (m_process->state() != QProcess::NotRunning) {
        emit error(tr("FFmpeg Post-processor is already running."));
        return;
    }

    m_originalFile = filePath;
    m_processOutputTail.clear();
    QFileInfo fileInfo(filePath);
    m_tempFile = QDir(fileInfo.path()).filePath(QStringLiteral("%1_tagged.%2").arg(fileInfo.completeBaseName(), fileInfo.suffix()));

    QStringList args;
    args << QStringLiteral("-nostdin");
    args << QStringLiteral("-i") << m_originalFile;
    args << QStringLiteral("-c") << QStringLiteral("copy"); // Copy all streams
    args << QStringLiteral("-metadata") << QStringLiteral("track=%1/%2").arg(trackNumber).arg(totalTracks);
    args << QStringLiteral("-y") << m_tempFile;

    QString program = ProcessUtils::findBinary(QStringLiteral("ffmpeg"), m_configManager).path;
    m_process->start(program, args);
}

void FfmpegPostProcessor::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
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

    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
        QString stderrOutput = m_processOutputTail;
        qWarning() << "FfmpegPostProcessor failed. Exit code:" << exitCode << "Stderr:" << stderrOutput;
        QFile::remove(m_tempFile); // Clean up temp file
        emit error(tr("FFmpeg post-processing failed: %1").arg(stderrOutput));
        return;
    }

    // Replace original file with the tagged one
    if (!QFile::remove(m_originalFile)) {
        qWarning() << "Could not remove original file:" << m_originalFile;
        emit error(tr("Could not replace original file with tagged version."));
        return;
    }
    if (!QFile::rename(m_tempFile, m_originalFile)) {
        qWarning() << "Could not rename temp file" << m_tempFile << "to" << m_originalFile;
        if (!QFile::copy(m_tempFile, m_originalFile)) {
            emit error(tr("Could not rename or copy temp file to original file. The file is at: %1").arg(m_tempFile));
            return;
        } else {
            QFile::remove(m_tempFile);
        }
    }

    emit finished();
}

void FfmpegPostProcessor::onProcessError(QProcess::ProcessError processError)
{
    if (processError == QProcess::FailedToStart) {
        qWarning() << "FfmpegPostProcessor failed to start process:" << m_process->errorString();
        emit error(tr("Failed to start ffmpeg process. Please check if it's installed and in your PATH, or configure the path in settings."));
    }
}

void FfmpegPostProcessor::appendProcessOutput(const QByteArray &data)
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
