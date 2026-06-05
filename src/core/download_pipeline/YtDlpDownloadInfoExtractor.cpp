#include "YtDlpDownloadInfoExtractor.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

YtDlpDownloadInfoExtractor::YtDlpDownloadInfoExtractor(QObject *parent)
    : QObject(parent), m_process(new QProcess(this))
{
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit extractionFailed(tr("Failed to start yt-dlp. Is the executable missing?"));
        }
    });

    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus == QProcess::CrashExit || exitCode != 0) {
            emit extractionFailed(tr("yt-dlp process failed. Error: %1\n%2").arg(m_process->errorString(), QString::fromUtf8(m_process->readAllStandardError())));
            return;
        }

        QByteArray jsonData = m_process->readAllStandardOutput();
        QJsonDocument doc = QJsonDocument::fromJson(jsonData);

        if (!doc.isObject()) {
            emit extractionFailed(tr("Failed to parse yt-dlp JSON output."));
            return;
        }

        QJsonObject root = doc.object();
        QString title = root.value(QStringLiteral("title")).toString();
        QString thumbnailUrl = root.value(QStringLiteral("thumbnail")).toString();
        QString finalFilename = root.value(QStringLiteral("_filename")).toString();
        QList<DownloadTarget> targets;
        QMap<QString, QString> httpHeaders;

        if (root.contains(QStringLiteral("http_headers"))) {
            QJsonObject headersObj = root.value(QStringLiteral("http_headers")).toObject();
            for (auto it = headersObj.constBegin(); it != headersObj.constEnd(); ++it) {
                httpHeaders.insert(it.key(), it.value().toString());
            }
        }

        if (root.contains(QStringLiteral("requested_downloads"))) {
            // Case for separate video and audio files that need merging
            QJsonArray downloads = root.value(QStringLiteral("requested_downloads")).toArray();
            for (const QJsonValue &val : downloads) {
                QJsonObject obj = val.toObject();
                if (obj.contains(QStringLiteral("url"))) {
                    DownloadTarget target;
                    target.url = obj.value(QStringLiteral("url")).toString();
                    target.filename = QFileInfo(obj.value(QStringLiteral("filename")).toString()).fileName();
                    if (obj.contains(QStringLiteral("vcodec")) && obj.value(QStringLiteral("vcodec")) != QStringLiteral("none")) target.type = DownloadTarget::Type::Video;
                    if (obj.contains(QStringLiteral("acodec")) && obj.value(QStringLiteral("acodec")) != QStringLiteral("none")) target.type = DownloadTarget::Type::Audio;
                    targets.append(target);
                }
            }
        } else if (root.contains(QStringLiteral("url"))) {
            // Case for a single file download (e.g., audio-only format)
            DownloadTarget target;
            target.url = root.value(QStringLiteral("url")).toString();
            target.filename = root.value(QStringLiteral("_filename")).toString();
            if (root.contains(QStringLiteral("vcodec")) && root.value(QStringLiteral("vcodec")) != QStringLiteral("none")) target.type = DownloadTarget::Type::Video;
            if (root.contains(QStringLiteral("acodec")) && root.value(QStringLiteral("acodec")) != QStringLiteral("none")) target.type = DownloadTarget::Type::Audio;
            targets.append(target);
        }

        if (root.contains(QStringLiteral("requested_subtitles"))) {
            QJsonObject subs = root.value(QStringLiteral("requested_subtitles")).toObject();
            QString baseName = QFileInfo(root.value(QStringLiteral("_filename")).toString()).completeBaseName();
            for (auto it = subs.constBegin(); it != subs.constEnd(); ++it) {
                DownloadTarget target;
                target.type = DownloadTarget::Type::Subtitle;
                target.url = it.value().toObject().value(QStringLiteral("url")).toString();
                target.ext = it.value().toObject().value(QStringLiteral("ext")).toString();
                target.lang = it.key();
                target.filename = QStringLiteral("%1.%2.%3").arg(baseName, target.lang, target.ext);
                targets.append(target);
            }
        }

        if (targets.isEmpty()) {
            emit extractionFailed(tr("No downloadable URLs found in yt-dlp JSON output."));
            return;
        }

        emit extractionSuccess(title, thumbnailUrl, targets, finalFilename, httpHeaders, root.toVariantMap());
    });
}

void YtDlpDownloadInfoExtractor::extract(const QString &ytDlpPath, const QStringList &args)
{
    if (m_process->state() != QProcess::NotRunning) {
        emit extractionFailed(tr("Extractor is already running."));
        return;
    }

    // Log full command for debugging
    QString fullCommand = QStringLiteral("\"") + ytDlpPath + QStringLiteral("\"");
    for (const QString &arg : args) {
        if (arg.contains(QLatin1Char(' '))) {
            fullCommand += QStringLiteral(" \"") + arg + QStringLiteral("\"");
        } else {
            fullCommand += QLatin1Char(' ') + arg;
        }
    }
    qDebug() << "YtDlpDownloadInfoExtractor full command:" << fullCommand;

    m_process->start(ytDlpPath, args);
}

void YtDlpDownloadInfoExtractor::cancel() {
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
    }
}
