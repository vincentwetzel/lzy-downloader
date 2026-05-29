#include "YtDlpJsonExtractor.h"
#include "core/ConfigManager.h"
#include "core/ProcessUtils.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDebug>

YtDlpJsonExtractor::YtDlpJsonExtractor(ConfigManager *configManager, QObject *parent)
    : QObject(parent), m_configManager(configManager)
{
    m_process = new QProcess(this);
    ProcessUtils::setProcessEnvironment(*m_process);

    connect(m_process, &QProcess::finished, this, &YtDlpJsonExtractor::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &YtDlpJsonExtractor::onProcessError);
}

void YtDlpJsonExtractor::getInfo(const QString &url)
{
    if (m_process->state() != QProcess::NotRunning) {
        emit error("Extractor is already running.");
        return;
    }

    QStringList args;
    args << "--dump-json" << "--no-playlist" << url;

    QString program = ProcessUtils::findBinary("yt-dlp", m_configManager).path;
    
    qDebug() << "YtDlpJsonExtractor executing command:" << program << args;
    m_process->start(program, args);
}

void YtDlpJsonExtractor::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
        QString stderrOutput = m_process->readAllStandardError();
        qWarning() << "YtDlpJsonExtractor failed. Exit code:" << exitCode << "Stderr:" << stderrOutput;
        QString cleanError = "Unknown error";
        int errIdx = stderrOutput.indexOf("ERROR:");
        if (errIdx != -1) {
            int endIdx = stderrOutput.indexOf('\n', errIdx);
            cleanError = stderrOutput.mid(errIdx, endIdx == -1 ? -1 : endIdx - errIdx).trimmed();
        }
        emit error("Failed to extract information.\n" + cleanError);
        return;
    }

    QByteArray jsonData = m_process->readAllStandardOutput();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "YtDlpJsonExtractor JSON parse error:" << parseError.errorString();
        emit error("Failed to parse information JSON.");
        return;
    }

    emit infoReady(doc.object().toVariantMap());
}

void YtDlpJsonExtractor::onProcessError(QProcess::ProcessError processError)
{
    if (processError == QProcess::FailedToStart) {
        qWarning() << "YtDlpJsonExtractor failed to start process:" << m_process->errorString();
        emit error("Failed to start yt-dlp process. Please check if it's installed and in your PATH, or configure the path in settings.");
    } else {
        qWarning() << "YtDlpJsonExtractor process error:" << m_process->errorString();
        emit error("An error occurred with the yt-dlp process: " + m_process->errorString());
    }
}