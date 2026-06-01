#include "Aria2Daemon.h"
#include "core/ConfigManager.h"
#include "core/ProcessUtils.h"
#include <QDebug>

Aria2Daemon::Aria2Daemon(ConfigManager *configManager, QObject *parent)
    : QObject(parent), m_configManager(configManager)
{
    m_process = new QProcess(this);
    ProcessUtils::setProcessEnvironment(*m_process);

    connect(m_process, &QProcess::errorOccurred, this, &Aria2Daemon::onDaemonError);
    connect(m_process, &QProcess::finished, this, &Aria2Daemon::onDaemonFinished);
}

Aria2Daemon::~Aria2Daemon()
{
    stop();
}

bool Aria2Daemon::start()
{
    if (isRunning()) {
        return true;
    }

    QStringList args;
    args << QStringLiteral("--enable-rpc")
         << QStringLiteral("--rpc-listen-all=false")
         << QStringLiteral("--rpc-listen-port=6800")
         << QStringLiteral("--rpc-secret=media-downloader")
         << QStringLiteral("--daemon=false") // We manage it as a child process
         << QStringLiteral("--summary-interval=0")
         << QStringLiteral("--log-level=warn");

    QString program = ProcessUtils::findBinary(QStringLiteral("aria2c"), m_configManager).path;
    m_process->start(program, args);

    qInfo() << "Aria2c daemon starting...";
    return true;
}

void Aria2Daemon::stop()
{
    if (isRunning()) {
        m_process->disconnect(); // Prevent re-entrant read operations on the dying process buffer
        ProcessUtils::terminateProcessTree(m_process);
        qInfo() << "Aria2c daemon stopped.";
    }
}

bool Aria2Daemon::isRunning() const
{
    return m_process->state() != QProcess::NotRunning;
}

void Aria2Daemon::onDaemonError(QProcess::ProcessError processError)
{
    if (processError == QProcess::FailedToStart) {
        qWarning() << "Aria2Daemon failed to start process:" << m_process->errorString();
        emit error(tr("Failed to start aria2c process. Please check if it's installed and in your PATH, or configure the path in settings."));
    } else {
        qWarning() << "Aria2Daemon process error:" << m_process->errorString();
        emit error(tr("An error occurred with the aria2c process: %1").arg(m_process->errorString()));
    }
}

void Aria2Daemon::onDaemonFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    qInfo() << "Aria2c daemon process finished with exit code" << exitCode << "and status" << exitStatus;
}