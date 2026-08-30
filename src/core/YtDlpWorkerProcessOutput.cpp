#include "YtDlpWorker.h"

#include "core/ConfigManager.h"
#include "core/DownloadTempCleanup.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>
#include <QVariantList>
#include <chrono>
#include <utility>

#include "YtDlpWorkerProcessHelpers.h"

using namespace YtDlpWorkerProcessHelpers;

void YtDlpWorker::onReadyReadStandardOutput() {
    if (!m_process) {
        return;
    }
    const QByteArray data = m_process->readAllStandardOutput();
    parseStandardOutput(data);
}

void YtDlpWorker::onReadyReadStandardError() {
    if (!m_process) {
        return;
    }
    const QByteArray data = m_process->readAllStandardError();
    parseStandardError(data);
}

void YtDlpWorker::parseProcessBuffer(QByteArray &buffer, const QByteArray &newData) {
    buffer.append(newData);

    const qsizetype lastDelimiter = qMax(buffer.lastIndexOf('\n'), buffer.lastIndexOf('\r'));
    if (lastDelimiter == -1) {
        return;
    }

    qsizetype start = 0;
    for (qsizetype i = 0; i <= lastDelimiter; ++i) {
        const char c = buffer.at(i);
        if (c == '\n' || c == '\r') {
            if (i > start) {
                const QByteArrayView chunk(buffer.constData() + start, i - start);
                const QString trimmedLine = QString::fromUtf8(chunk).trimmed();
                if (!trimmedLine.isEmpty()) {
                    handleOutputLine(trimmedLine);
                }
            }
            start = i + 1;
        }
    }

    buffer.remove(0, lastDelimiter + 1);
}

void YtDlpWorker::parseStandardOutput(const QByteArray &output) {
    parseProcessBuffer(m_outputBuffer, output);
}

void YtDlpWorker::parseStandardError(const QByteArray &output) {
    parseProcessBuffer(m_errorBuffer, output);
}


