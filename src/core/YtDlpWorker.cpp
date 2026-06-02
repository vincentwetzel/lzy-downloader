#include "YtDlpWorker.h"
#include "core/ConfigManager.h"
#include "core/ProcessUtils.h"

#include <QDir>
#include <QFile>
#include <QDebug>
#include <QFileInfo>
#include <QProcess>

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

    const ProcessUtils::FoundBinary ytDlpBinary = ProcessUtils::findBinary(QStringLiteral("yt-dlp"), m_configManager);
    if (ytDlpBinary.source == QStringLiteral("Not Found") || ytDlpBinary.path.isEmpty()) {
        const QString message = tr("Download failed.\n"
                                   "yt-dlp could not be found. Configure it in Advanced Settings -> External Tools.");
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
    int pt_index = m_args.indexOf(QStringLiteral("--progress-template"));
    if (pt_index != -1) {
        m_args.removeAt(pt_index); // remove flag
        if (pt_index < m_args.size()) {
            m_args.removeAt(pt_index); // remove value
        }
    }
    if (!m_args.contains(QStringLiteral("--progress"))) {
        m_args.prepend(QStringLiteral("--progress"));
    }

    emitStatusUpdate(tr("Extracting media information..."), -1);

    qDebug() << "[YtDlpWorker] Binary path:" << ytDlpPath;
    qDebug() << "[YtDlpWorker] Working directory:" << workingDirPath;
    qDebug() << "[YtDlpWorker] Number of arguments:" << m_args.size();

    qDebug() << "Starting yt-dlp with path:" << ytDlpPath << "source:" << ytDlpBinary.source << "and arguments:" << m_args;
    qDebug() << "Working directory set to:" << workingDirPath;

    // Log full command for debugging
    QString fullCommand = QStringLiteral("\"%1\"").arg(ytDlpPath);
    for (const QString &arg : m_args) {
        if (arg.contains(' ')) {
            fullCommand += QStringLiteral(" \"%1\"").arg(arg);
        } else {
            fullCommand += QStringLiteral(" %1").arg(arg);
        }
    }
    qDebug() << "Full yt-dlp command:" << fullCommand;
    
    // Connect state change signals for diagnostics
    connect(m_process, &QProcess::stateChanged, this, [this](QProcess::ProcessState state) {
        qDebug() << "[YtDlpWorker] Process state changed to:" << state;
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
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

    // Clean up orphaned wait thumbnail if the process is killed by the user
    if (!m_thumbnailPath.isEmpty() && QFileInfo(m_thumbnailPath).fileName().startsWith(m_id + QStringLiteral("_wait_thumbnail"))) {
        QFile::remove(m_thumbnailPath);
        m_thumbnailPath.clear();
    }
    
    // Attempt to remove the UUID directory if it's completely empty
    if (m_configManager) {
        QString tempDir = m_configManager->get(QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory")).toString();
        if (tempDir.isEmpty()) {
            QString completedDir = m_configManager->get(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory")).toString();
            if (!completedDir.isEmpty()) {
                tempDir = QDir(completedDir).filePath(QStringLiteral("temp_downloads"));
            }
        }
        if (!tempDir.isEmpty()) {
            QString uuidDirPath = QDir(tempDir).filePath(m_id);
            QDir().rmdir(uuidDirPath);
        }
    }
}

void YtDlpWorker::finishGracefully() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        qDebug() << "[YtDlpWorker] Sending graceful interrupt to finish download early:" << m_id;
        ProcessUtils::sendGracefulInterrupt(m_process->processId());
    }
}
