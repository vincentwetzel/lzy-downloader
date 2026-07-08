#include "YtDlpWorker.h"
#include "core/ConfigManager.h"
#include "core/ProcessUtils.h"

#include <QDir>
#include <QFile>
#include <QDebug>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QRegularExpression>

YtDlpWorker::YtDlpWorker(const QString &id, const QStringList &args, ConfigManager *configManager, QObject *parent)
    : QObject(parent), m_id(id), m_args(args), m_configManager(configManager), m_process(nullptr), m_finishEmitted(false), m_errorEmitted(false), m_videoTitle(QString()),
      m_thumbnailPath(QString()), m_infoJsonPath(QString()), m_infoJsonRetryCount(0) {

    m_process = new QProcess(this);

    // Intercept standard error to check for cookie/API errors for the fallback mechanism
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        if (m_process) {
            QProcess::ProcessChannel oldChannel = m_process->readChannel();
            m_process->setCurrentReadChannel(QProcess::StandardError);
            QByteArray errData = m_process->peek(m_process->bytesAvailable());
            m_process->setCurrentReadChannel(oldChannel);

            QString currentErr = QString::fromUtf8(errData);
            QString accumulated = m_process->property("accumulated_stderr").toString();
            accumulated += currentErr;
            m_process->setProperty("accumulated_stderr", accumulated);
        }
    });

    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (this->property("proactiveCookieRetryActive").toBool()) {
            return;
        }
        QString accumulatedStderr = m_process->property("accumulated_stderr").toString();
        bool cookieRetryAttempted = m_process->property("cookie_retry_attempted").toBool();
        bool waitRetryAttempted = m_process->property("wait_retry_attempted").toBool();

        // Define common errors that might benefit from a cookie retry or a specific livestream retry
        static const QRegularExpression errorRe(
            QStringLiteral("profile is locked|empty media response|not granting access|cookie.*(?:invalid|expired|failed|error|rotate|refresh)|decryption|permission denied|sqlite3.OperationalError|access denied|HTTP Error 400|Bad Request|Unable to download JSON metadata"),
            QRegularExpression::CaseInsensitiveOption
        );

        bool isProactiveWaitRetry = this->property("proactiveWaitRetryActive").toBool();
        if (isProactiveWaitRetry) {
            this->setProperty("proactiveWaitRetryActive", false);
        }

        if ((exitStatus == QProcess::NormalExit && exitCode != 0) || isProactiveWaitRetry) {
            bool hasCookies = m_args.contains(QStringLiteral("--cookies-from-browser")) || m_args.contains(QStringLiteral("--cookies"));
            bool shouldRetry = false;
            bool removeCookies = false;
            bool removeWaitForVideo = false;

            if (hasCookies && !cookieRetryAttempted && errorRe.match(accumulatedStderr).hasMatch() && !isProactiveWaitRetry) {
                shouldRetry = true;
                removeCookies = true;
                qWarning() << "[YtDlpWorker] Cookie-related/API-access failure detected. Retrying download once without browser cookies. Error captured:" << accumulatedStderr;
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
                m_process->setProperty("accumulated_stderr", QString()); // Reset for next run

                if (removeCookies) {
                    // Remove --cookies-from-browser and its argument
                    qsizetype cookiesIndex = m_args.indexOf(QStringLiteral("--cookies-from-browser"));
                    if (cookiesIndex != -1) {
                        m_args.removeAt(cookiesIndex);
                        if (cookiesIndex < m_args.size()) {
                            m_args.removeAt(cookiesIndex);
                        }
                    }

                    // Remove --cookies and its argument
                    qsizetype cookiesFileIndex = m_args.indexOf(QStringLiteral("--cookies"));
                    if (cookiesFileIndex != -1) {
                        m_args.removeAt(cookiesFileIndex);
                        if (cookiesFileIndex < m_args.size()) {
                            m_args.removeAt(cookiesFileIndex);
                        }
                    }
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
    m_finalFilename.clear();
    m_originalDownloadedFilename.clear();
    m_videoTitle.clear();
    m_infoJsonPath.clear();
    m_infoJsonRetryCount = 0;
    m_outputBuffer.clear();
    m_errorBuffer.clear();
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
