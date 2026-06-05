#include "PlaylistExpansionWorker.h"

#include "PlaylistExpansionParser.h"
#include "YtDlpArgsBuilder.h"
#include "core/ProcessUtils.h"

#include <QDebug>
#include <QJsonDocument>
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

    QTimer::singleShot(std::chrono::seconds(45), m_process, [this]() {
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
        int index = args.indexOf(flag);
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

        const int errIdx = stderrOutput.indexOf(QStringLiteral("ERROR:"));
        if (errIdx != -1) {
            const int endIdx = stderrOutput.indexOf('\n', errIdx);
            errorMessage = QStringLiteral("%1\n%2").arg(errorMessage, stderrOutput.mid(errIdx, endIdx == -1 ? -1 : endIdx - errIdx).trimmed());
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

    const PlaylistExpansionParseResult result = PlaylistExpansionParser::parse(doc.object(), m_url);
    if (result.isPlaylist && m_currentPlaylistLogic == QStringLiteral("Ask")) {
        emit playlistDetected(m_url, result.items.count(), m_options, result.items);
    } else {
        emit expansionFinished(m_url, result.items, QString());
    }
}
