#include "UrlValidator.h"
#include "YtDlpArgsBuilder.h"
#include "core/ProcessUtils.h"
#include <QDebug>
#include <QTimer>
#include <chrono>

UrlValidator::UrlValidator(ConfigManager *configManager, QObject *parent)
    : QObject(parent), m_configManager(configManager) {

    m_process = new QProcess(this);
    connect(m_process, &QProcess::finished, this, &UrlValidator::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit validationFinished(false, tr("Failed to start yt-dlp process."));
        }
    });
}

void UrlValidator::validate(const QString &url) {
    if (m_process->state() != QProcess::NotRunning) {
        emit validationFinished(false, tr("Validator is already running."));
        return;
    }

    YtDlpArgsBuilder argsBuilder;
    const QStringList args = argsBuilder.buildValidationArgs(m_configManager, url);
    if (args.isEmpty()) {
        emit validationFinished(false, tr("Unable to build yt-dlp validation command."));
        return;
    }

    const ProcessUtils::FoundBinary ytDlpBinary = ProcessUtils::findBinary(QStringLiteral("yt-dlp"), m_configManager);
    if (ytDlpBinary.source == QStringLiteral("Not Found") || ytDlpBinary.path.isEmpty()) {
        emit validationFinished(false, tr("yt-dlp could not be found. Configure it in Advanced Settings -> External Tools."));
        return;
    }

    ProcessUtils::setProcessEnvironment(*m_process);
    qDebug() << "UrlValidator executing command:" << ytDlpBinary.path << args;
    m_process->start(ytDlpBinary.path, args);

    // Watchdog to prevent indefinite hangs
    const int runId = m_process->property("run_id").toInt() + 1;
    m_process->setProperty("run_id", runId);

    QTimer::singleShot(std::chrono::seconds(15), this, [this, runId]() {
        if (m_process->property("run_id").toInt() == runId && m_process->state() != QProcess::NotRunning) {
            m_process->setProperty("timed_out", true);
            ProcessUtils::terminateProcessTree(m_process);
            m_process->kill();
            emit validationFinished(false, tr("Validation timed out after 15 seconds."));
        }
    });
}

void UrlValidator::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (m_process->property("timed_out").toBool()) {
        m_process->setProperty("timed_out", false);
        return;
    }
    bool isValid = (exitStatus == QProcess::NormalExit && exitCode == 0);
    QString error;
    if (!isValid) {
        QString stderrOutput = QString::fromUtf8(m_process->readAllStandardError());
        qWarning().noquote() << "UrlValidator yt-dlp stderr:" << stderrOutput.trimmed();
        error = tr("yt-dlp encountered an error.");
        qsizetype errIdx = stderrOutput.indexOf(QStringLiteral("ERROR:"));
        if (errIdx != -1) {
            qsizetype endIdx = stderrOutput.indexOf(QLatin1Char('\n'), errIdx);
            error = stderrOutput.mid(errIdx, endIdx == -1 ? -1 : endIdx - errIdx).trimmed();
        }
    }
    emit validationFinished(isValid, error);
}
