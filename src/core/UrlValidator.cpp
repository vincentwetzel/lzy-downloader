#include "UrlValidator.h"
#include "YtDlpArgsBuilder.h"
#include "core/ProcessUtils.h"
#include <QDebug>

UrlValidator::UrlValidator(ConfigManager *configManager, QObject *parent)
    : QObject(parent), m_configManager(configManager) {

    m_process = new QProcess(this);
    connect(m_process, &QProcess::finished, this, &UrlValidator::onProcessFinished);
}

void UrlValidator::validate(const QString &url) {
    YtDlpArgsBuilder argsBuilder;
    const QStringList args = argsBuilder.buildValidationArgs(m_configManager, url);
    if (args.isEmpty()) {
        emit validationFinished(false, "Unable to build yt-dlp validation command.");
        return;
    }

    const ProcessUtils::FoundBinary ytDlpBinary = ProcessUtils::findBinary("yt-dlp", m_configManager);
    if (ytDlpBinary.source == "Not Found" || ytDlpBinary.path.isEmpty()) {
        emit validationFinished(false, "yt-dlp could not be found. Configure it in Advanced Settings -> External Tools.");
        return;
    }

    ProcessUtils::setProcessEnvironment(*m_process);
    qDebug() << "UrlValidator executing command:" << ytDlpBinary.path << args;
    m_process->start(ytDlpBinary.path, args);
}

void UrlValidator::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    bool isValid = (exitStatus == QProcess::NormalExit && exitCode == 0);
    QString error;
    if (!isValid) {
        QString stderrOutput = m_process->readAllStandardError();
        qWarning().noquote() << "UrlValidator yt-dlp stderr:" << stderrOutput.trimmed();
        error = "yt-dlp encountered an error.";
        int errIdx = stderrOutput.indexOf("ERROR:");
        if (errIdx != -1) {
            int endIdx = stderrOutput.indexOf('\n', errIdx);
            error = stderrOutput.mid(errIdx, endIdx == -1 ? -1 : endIdx - errIdx).trimmed();
        }
    }
    emit validationFinished(isValid, error);
}
