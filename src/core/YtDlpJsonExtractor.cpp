#include "YtDlpJsonExtractor.h"
#include "core/ConfigManager.h"
#include "core/ProcessUtils.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDebug>
#include <QTimer>
#include <chrono>

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
        emit error(tr("Extractor is already running."));
        return;
    }

    QStringList args;
    args << QStringLiteral("--dump-json") << QStringLiteral("--no-playlist") << url;

    const ProcessUtils::FoundBinary ytDlpBinary = ProcessUtils::findBinary(QStringLiteral("yt-dlp"), m_configManager);
    if (ytDlpBinary.source == QStringLiteral("Not Found") || ytDlpBinary.path.isEmpty()) {
        emit error(tr("yt-dlp could not be found. Configure it in Advanced Settings -> External Tools."));
        return;
    }

    qDebug() << "YtDlpJsonExtractor executing command:" << ytDlpBinary.path << args;
    m_process->start(ytDlpBinary.path, args);

    const int runId = m_process->property("run_id").toInt() + 1;
    m_process->setProperty("run_id", runId);

    QTimer::singleShot(std::chrono::seconds(30), this, [this, runId]() {
        if (m_process->property("run_id").toInt() == runId && m_process->state() != QProcess::NotRunning) {
            m_process->setProperty("timed_out", true);
            ProcessUtils::terminateProcessTree(m_process);
            m_process->kill();
            emit error(tr("Extraction timed out after 30 seconds."));
        }
    });
}

void YtDlpJsonExtractor::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_process->property("timed_out").toBool()) {
        m_process->setProperty("timed_out", false);
        return;
    }
    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
        QString stderrOutput = QString::fromUtf8(m_process->readAllStandardError());
        qWarning() << "YtDlpJsonExtractor failed. Exit code:" << exitCode << "Stderr:" << stderrOutput;
        QString cleanError = QStringLiteral("Unknown error");
        int errIdx = stderrOutput.indexOf(QStringLiteral("ERROR:"));
        if (errIdx != -1) {
            int endIdx = stderrOutput.indexOf('\n', errIdx);
            cleanError = stderrOutput.mid(errIdx, endIdx == -1 ? -1 : endIdx - errIdx).trimmed();
        }
        emit error(tr("Failed to extract information.\n%1").arg(cleanError));
        return;
    }

    QByteArray jsonData = m_process->readAllStandardOutput();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "YtDlpJsonExtractor JSON parse error:" << parseError.errorString();
        emit error(tr("Failed to parse information JSON."));
        return;
    }

    emit infoReady(doc.object().toVariantMap());
}

void YtDlpJsonExtractor::onProcessError(QProcess::ProcessError processError)
{
    if (processError == QProcess::FailedToStart) {
        qWarning() << "YtDlpJsonExtractor failed to start process:" << m_process->errorString();
        emit error(tr("Failed to start yt-dlp process. Please check if it's installed and in your PATH, or configure the path in settings."));
    }
}