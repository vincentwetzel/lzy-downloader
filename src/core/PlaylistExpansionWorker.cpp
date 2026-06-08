#include "PlaylistExpansionWorker.h"

#include "PlaylistExpansionParser.h"
#include "YtDlpArgsBuilder.h"
#include "core/ProcessUtils.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QTimer>

#include <chrono>

PlaylistExpansionWorker::PlaylistExpansionWorker(const QString &url, ConfigManager *configManager, QObject *parent)
    : QObject(parent)
    , m_url(url)
    , m_configManager(configManager)
    , m_process(new QProcess(this))
{
    connect(m_process, &QProcess::finished, this, &PlaylistExpansionWorker::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit expansionFinished(m_url, {}, tr("Failed to start yt-dlp process. Please check your configuration."));
        }
    });
}

void PlaylistExpansionWorker::startExpansion(const QString &playlistLogic)
{
    m_currentPlaylistLogic = playlistLogic;
    m_options = property("options").toMap();

    if (m_options.value(QStringLiteral("type")).toString() == QStringLiteral("gallery")) {
        QVariantMap item;
        item.insert(QStringLiteral("url"), m_url);
        item.insert(QStringLiteral("is_playlist"), false);
        item.insert(QStringLiteral("playlist_index"), -1);
        emit expansionFinished(m_url, {item}, QString());
        return;
    }

    const ProcessUtils::FoundBinary ytDlpBinary = ProcessUtils::findBinary(QStringLiteral("yt-dlp"), m_configManager);
    if (ytDlpBinary.source == QStringLiteral("Not Found") || ytDlpBinary.path.isEmpty()) {
        emit expansionFinished(m_url, {}, tr("yt-dlp could not be found. Configure it in Advanced Settings -> External Tools."));
        return;
    }

    const QStringList args = buildProbeArguments(playlistLogic);
    qDebug() << "PlaylistExpansionWorker executing command: yt-dlp" << args;
    m_process->start(ytDlpBinary.path, args);

    QTimer::singleShot(std::chrono::seconds(45), this, [this]() {
        if (m_process->state() != QProcess::NotRunning) {
            m_process->setProperty("timed_out", true);
            ProcessUtils::terminateProcessTree(m_process);
            m_process->kill();
            emit expansionFinished(m_url, {}, tr("Playlist expansion timed out after 45 seconds."));
        }
    });
}

QStringList PlaylistExpansionWorker::buildProbeArguments(const QString &playlistLogic)
{
    YtDlpArgsBuilder builder;
    m_options.insert(QStringLiteral("playlist_logic"), playlistLogic);
    m_options.insert(QStringLiteral("skip_dir_creation"), true);

    QStringList args = builder.build(m_configManager, m_url, m_options);

    const auto removeArgWithValue = [&args](const QString &flag) {
        qsizetype index = args.indexOf(flag);
        while (index != -1) {
            args.removeAt(index);
            if (index < args.size()) {
                args.removeAt(index);
            }
            index = args.indexOf(flag);
        }
    };

    removeArgWithValue(QStringLiteral("-f"));
    args.removeAll(QStringLiteral("--write-info-json"));
    removeArgWithValue(QStringLiteral("--progress-template"));
    args.removeAll(QStringLiteral("--newline"));
    args.removeAll(QStringLiteral("--ignore-errors"));
    removeArgWithValue(QStringLiteral("--external-downloader"));
    removeArgWithValue(QStringLiteral("--external-downloader-args"));
    removeArgWithValue(QStringLiteral("--merge-output-format"));
    removeArgWithValue(QStringLiteral("--ppa"));
    removeArgWithValue(QStringLiteral("--convert-thumbnails"));
    removeArgWithValue(QStringLiteral("--sub-langs"));
    args.removeAll(QStringLiteral("--write-auto-subs"));
    args.removeAll(QStringLiteral("--embed-subs"));
    args.removeAll(QStringLiteral("--embed-chapters"));
    args.removeAll(QStringLiteral("--embed-metadata"));
    args.removeAll(QStringLiteral("--embed-thumbnail"));
    args.removeAll(QStringLiteral("--live-from-start"));
    args.removeAll(QStringLiteral("--no-live-from-start"));

    auto it = args.begin();
    while (it != args.end()) {
        if (it->startsWith(QStringLiteral("--wait-for-video"))) {
            it = args.erase(it);
        } else {
            ++it;
        }
    }

    removeArgWithValue(QStringLiteral("-o"));
    removeArgWithValue(QStringLiteral("--ffmpeg-location"));
    removeArgWithValue(QStringLiteral("--print"));

    args << QStringLiteral("--flat-playlist")
         << QStringLiteral("--dump-single-json")
         << QStringLiteral("--no-download");

    return args;
}

void PlaylistExpansionWorker::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_process->property("timed_out").toBool()) {
        m_process->setProperty("timed_out", false);
        return;
    }
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        const QString stderrOutput = QString::fromUtf8(m_process->readAllStandardError());
        QString errorMessage = tr("Failed to expand playlist.");

        const qsizetype errIdx = stderrOutput.indexOf(QStringLiteral("ERROR:"));
        if (errIdx != -1) {
            const qsizetype endIdx = stderrOutput.indexOf(QLatin1Char('\n'), errIdx);
            errorMessage = QStringLiteral("%1\n%2").arg(errorMessage, stderrOutput.mid(errIdx, endIdx == -1 ? -1 : endIdx - errIdx).trimmed());
        }

        static const QRegularExpression bypassRe(
            QStringLiteral("Premieres in|Premiering in|Premiere will begin|live event will begin|is upcoming|Offline \\(expected\\)|Offline expected|waiting for premiere|waiting for livestream|Live in |Starting in |Private video|Sign in to confirm|Video unavailable|This live event has ended"),
            QRegularExpression::CaseInsensitiveOption
        );

        if (bypassRe.match(errorMessage).hasMatch() && !m_url.contains(QStringLiteral("playlist"), Qt::CaseInsensitive)) {
            qDebug() << "Playlist expansion hit a known video-level error. Bypassing to let YtDlpWorker handle it. Error:" << errorMessage;
            QVariantMap item;
            item.insert(QStringLiteral("url"), m_url);
            item.insert(QStringLiteral("is_playlist"), false);
            item.insert(QStringLiteral("playlist_index"), -1);
            emit expansionFinished(m_url, {item}, QString());
            return;
        }

        emit expansionFinished(m_url, {}, errorMessage);
        return;
    }

    const QByteArray jsonData = m_process->readAllStandardOutput();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        emit expansionFinished(m_url, {}, tr("Failed to parse playlist JSON: %1").arg(parseError.errorString()));
        return;
    }

    QJsonObject jsonObj = doc.object();

    // Pre-process JSON to ensure is_live is strictly true only for active/upcoming livestreams
    auto fixLiveStatus = [](QJsonObject& obj) {
        if (obj.contains(QStringLiteral("live_status"))) {
            const QString liveStatus = obj.value(QStringLiteral("live_status")).toString();
            if (liveStatus == QStringLiteral("was_live") || liveStatus == QStringLiteral("not_live") || liveStatus == QStringLiteral("post_live")) {
                obj.insert(QStringLiteral("is_live"), false);
            } else if (liveStatus == QStringLiteral("is_live") || liveStatus == QStringLiteral("is_upcoming")) {
                obj.insert(QStringLiteral("is_live"), true);
            }
        }
    };

    fixLiveStatus(jsonObj);
    if (jsonObj.contains(QStringLiteral("entries")) && jsonObj.value(QStringLiteral("entries")).isArray()) {
        QJsonArray entries = jsonObj.value(QStringLiteral("entries")).toArray();
        for (int i = 0; i < entries.size(); ++i) {
            if (entries[i].isObject()) {
                QJsonObject entry = entries[i].toObject();
                fixLiveStatus(entry);
                entries[i] = entry;
            }
        }
        jsonObj.insert(QStringLiteral("entries"), entries);
    }

    const PlaylistExpansionParseResult result = PlaylistExpansionParser::parse(jsonObj, m_url);
    if (result.isPlaylist && m_currentPlaylistLogic == QStringLiteral("Ask")) {
        emit playlistDetected(m_url, result.items.count(), m_options, result.items);
    } else {
        emit expansionFinished(m_url, result.items, QString());
    }
}
