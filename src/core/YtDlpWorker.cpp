#include "YtDlpWorker.h"
#include "core/ConfigManager.h"
#include "core/DownloadTempCleanup.h"
#include "core/ProcessUtils.h"

#include <QDir>
#include <QFile>
#include <QDebug>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QRegularExpression>

namespace {
    bool isMetadataSidecar(const QFileInfo &fileInfo) {
        const QString fileName = fileInfo.fileName();
        return fileName.endsWith(QStringLiteral(".info.json"), Qt::CaseInsensitive)
               || fileName.contains(QStringLiteral(".info.json."), Qt::CaseInsensitive);
    }
}

YtDlpWorker::YtDlpWorker(const QString &id, const QStringList &args, ConfigManager *configManager, QObject *parent)
    : QObject(parent), m_id(id), m_args(args), m_configManager(configManager), m_process(nullptr), m_finishEmitted(false), m_errorEmitted(false), m_videoTitle(QString()),
      m_thumbnailPath(QString()), m_infoJsonPath(QString()), m_infoJsonRetryCount(0) {

    m_process = new QProcess(this);
    m_progressPollTimer = new QTimer(this);
    m_progressPollTimer->setInterval(1000);
    connect(m_progressPollTimer, &QTimer::timeout, this, &YtDlpWorker::pollTransferProgress);

    connect(m_process, &QProcess::started, this, [this]() {
        // yt-dlp launches FFmpeg for merging and post-processing. Lowering the
        // parent priority also makes those child processes background work on
        // Windows, keeping the desktop responsive during accurate cuts.
        ProcessUtils::setBackgroundProcessPriority(*m_process);
    });

    // Intercept standard error to check for cookie/API errors for the fallback mechanism
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        if (m_process) {
            QProcess::ProcessChannel oldChannel = m_process->readChannel();
            m_process->setCurrentReadChannel(QProcess::StandardError);
            QByteArray errData = m_process->peek(m_process->bytesAvailable());
            m_process->setCurrentReadChannel(oldChannel);

            m_accumulatedStderr.appendText(QString::fromUtf8(errData));
            m_process->setProperty("accumulated_stderr", m_accumulatedStderr.join(QLatin1Char('\n')));
        }
    });

    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        QString accumulatedStderr = m_process->property("accumulated_stderr").toString();
        bool cookieRetryAttempted = m_process->property("cookie_retry_attempted").toBool();
        bool waitRetryAttempted = m_process->property("wait_retry_attempted").toBool();

        bool isProactiveWaitRetry = this->property("proactiveWaitRetryActive").toBool();
        if (isProactiveWaitRetry) {
            this->setProperty("proactiveWaitRetryActive", false);
        }

        if ((exitStatus == QProcess::NormalExit && exitCode != 0) || isProactiveWaitRetry) {
            bool hasCookies = m_args.contains(QStringLiteral("--cookies-from-browser")) || m_args.contains(QStringLiteral("--cookies"));
            bool browserCookieFailure = hasBrowserCookieFailureDiagnostic();
            if (!browserCookieFailure && !accumulatedStderr.isEmpty()) {
                const QStringList stderrLines = accumulatedStderr.split(QRegularExpression(QStringLiteral("[\\r\\n]")), Qt::SkipEmptyParts);
                for (const QString &line : stderrLines) {
                    if (isBrowserCookieFailureLine(line)) {
                        browserCookieFailure = true;
                        break;
                    }
                }
            }
            bool shouldRetry = false;
            bool removeCookies = false;
            bool removeWaitForVideo = false;

            if (hasCookies && !cookieRetryAttempted && browserCookieFailure && !isProactiveWaitRetry) {
                shouldRetry = true;
                removeCookies = true;
                qWarning() << "[YtDlpWorker] Browser-cookie extraction failure detected. Retrying once without browser cookies. Error captured:" << accumulatedStderr;
            } else if (!waitRetryAttempted) {
                static const QRegularExpression offlineRe(QStringLiteral("not currently live|live event has ended|offline"), QRegularExpression::CaseInsensitiveOption);
                if (isProactiveWaitRetry || offlineRe.match(accumulatedStderr).hasMatch()) {
                    if (m_args.contains(QStringLiteral("--wait-for-video")) || m_args.contains(QStringLiteral("--live-from-start"))) {
                        shouldRetry = true;
                        removeWaitForVideo = true;
                        qWarning() << "[YtDlpWorker] Livestream offline error detected. Retrying download once without --wait-for-video to prevent false-offline hang.";
                    }
                }
            }

            if (shouldRetry) {
                if (removeCookies) m_process->setProperty("cookie_retry_attempted", true);
                if (removeWaitForVideo) m_process->setProperty("wait_retry_attempted", true);
                m_accumulatedStderr.clear();
                m_process->setProperty("accumulated_stderr", QString()); // Reset for next run

                if (removeCookies) {
                    m_retriedWithoutBrowserCookies = true;
                    removeBrowserCookieArguments();
                }

                if (removeWaitForVideo) {
                    qsizetype waitIndex = m_args.indexOf(QStringLiteral("--wait-for-video"));
                    if (waitIndex != -1) {
                        m_args.removeAt(waitIndex);
                        if (waitIndex < m_args.size()) {
                            m_args.removeAt(waitIndex); // Remove the value (e.g., "30-300")
                        }
                        m_args << QStringLiteral("--no-wait-for-video"); // Explicitly disable waiting
                    }
                    qsizetype liveFromStartIndex = m_args.indexOf(QStringLiteral("--live-from-start"));
                    if (liveFromStartIndex != -1) {
                        m_args.removeAt(liveFromStartIndex);
                        m_args << QStringLiteral("--no-live-from-start");
                    }
                }

                // Restart the download
                QTimer::singleShot(1000, this, [this]() {
                    start();
                });
                return;
            }

            if (retryWithoutAria2cIfTransientFailure(accumulatedStderr)) {
                return;
            }
        }

        // Proceed to the normal process finished slot
        onProcessFinished(exitCode, exitStatus);
    });

    connect(m_process, &QProcess::errorOccurred, this, &YtDlpWorker::onProcessError);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &YtDlpWorker::onReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &YtDlpWorker::onReadyReadStandardError);
}

void YtDlpWorker::start() {
    qDebug() << "[YtDlpWorker] start() called for ID:" << m_id;
    
    // Clear any leftover state from previous downloads
    m_finishEmitted = false;
    m_errorEmitted = false;
    m_promptDelayActive = false;
    if (!m_retriedWithoutAria2c) {
        m_recoveryDiagnostic.clear();
    }
    m_finalFilename.clear();
    m_originalDownloadedFilename.clear();
    m_videoTitle.clear();
    m_infoJsonPath.clear();
    m_infoJsonRetryCount = 0;
    m_outputBuffer.clear();
    m_errorBuffer.clear();
    m_accumulatedStderr.clear();
    m_allOutputLines.clear();
    m_errorLines.clear();
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
    m_lastPolledTransferBytes = -1;
    m_lastPolledProgress = -1.0;
    m_fileProgressClock.invalidate();

    const ProcessUtils::FoundBinary ytDlpBinary = ProcessUtils::findBinary(QStringLiteral("yt-dlp"), m_configManager);
    if (ytDlpBinary.source == QStringLiteral("Not Found") || ytDlpBinary.path.isEmpty()) {
        const QString message = tr("Download failed.\n"
                                   "yt-dlp could not be found. Configure it in Advanced Settings -> External Tools.");
        qWarning() << message;
        if (!m_finishEmitted) {
            m_finishEmitted = true;
            const QString tempRoot = DownloadTempCleanup::resolveRoot(m_configManager);
            (void)DownloadTempCleanup::removeEmptyOwnedDirectory(m_id, DownloadTempCleanup::pathForId(tempRoot, m_id));
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
    qsizetype pt_index = m_args.indexOf(QStringLiteral("--progress-template"));
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
    QStringList commandParts;
    commandParts << QStringLiteral("\"%1\"").arg(ytDlpPath);
    for (const QString &arg : m_args) {
        if (arg.contains(QLatin1Char(' '))) {
            commandParts << QStringLiteral("\"%1\"").arg(arg);
        } else {
            commandParts << arg;
        }
    }
    qDebug() << "Full yt-dlp command:" << commandParts.join(QLatin1Char(' '));
    
    // Connect state change signals for diagnostics
    connect(m_process, &QProcess::stateChanged, this, [this](QProcess::ProcessState state) {
        qDebug() << "[YtDlpWorker] Process state changed to:" << state;
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        qWarning() << "[YtDlpWorker] Process error occurred:" << error << m_process->errorString();
    });
    
    qDebug() << "[YtDlpWorker] Calling m_process->start()...";
    m_process->start(ytDlpPath, m_args);
    m_progressPollTimer->start();
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
    if (m_progressPollTimer) {
        m_progressPollTimer->stop();
    }
    if (m_process && m_process->state() != QProcess::NotRunning) {
        disconnect(m_process, &QProcess::readyReadStandardOutput, this, &YtDlpWorker::onReadyReadStandardOutput);
        disconnect(m_process, &QProcess::readyReadStandardError, this, &YtDlpWorker::onReadyReadStandardError);
        ProcessUtils::terminateProcessTree(m_process);
        m_process->kill(); // Forcefully kill the QProcess instance as fallback
    }

    // Clean up orphaned wait thumbnail if the process is killed by the user
    if (!m_thumbnailPath.isEmpty() && QFileInfo(m_thumbnailPath).fileName().startsWith(QStringLiteral("%1_wait_thumbnail").arg(m_id))) {
        QFile::remove(m_thumbnailPath);
        m_thumbnailPath.clear();
    }
    
    const QString tempRoot = DownloadTempCleanup::resolveRoot(m_configManager);
    (void)DownloadTempCleanup::removeEmptyOwnedDirectory(m_id, DownloadTempCleanup::pathForId(tempRoot, m_id));
}

void YtDlpWorker::cleanupMetadataSidecarsForRetry() {
    const QString tempRoot = DownloadTempCleanup::resolveRoot(m_configManager);
    const QDir uuidDirectory(DownloadTempCleanup::pathForId(tempRoot, m_id));
    if (!uuidDirectory.exists()) {
        return;
    }

    const QFileInfoList sidecars = uuidDirectory.entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                                                                QDir::Name);
    for (const QFileInfo &sidecar : sidecars) {
        if (!isMetadataSidecar(sidecar)) {
            continue;
        }
        if (!QFile::remove(sidecar.absoluteFilePath())) {
            qWarning() << "[YtDlpWorker] Could not remove stale metadata sidecar before retry:"
                       << sidecar.absoluteFilePath();
        } else {
            qDebug() << "[YtDlpWorker] Removed stale metadata sidecar before retry:"
                     << sidecar.absoluteFilePath();
        }
    }
}

void YtDlpWorker::removeBrowserCookieArguments() {
    const auto removeFlagAndValue = [this](const QString &flag) {
        qsizetype flagIndex = m_args.indexOf(flag);
        while (flagIndex != -1) {
            m_args.removeAt(flagIndex);
            if (flagIndex < m_args.size()) {
                m_args.removeAt(flagIndex);
            }
            flagIndex = m_args.indexOf(flag);
        }
    };

    removeFlagAndValue(QStringLiteral("--cookies-from-browser"));
    removeFlagAndValue(QStringLiteral("--cookies"));
}


void YtDlpWorker::finishGracefully() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        qDebug() << "[YtDlpWorker] Sending graceful interrupt to finish download early:" << m_id;
        ProcessUtils::sendGracefulInterrupt(m_process->processId());
    }
}
