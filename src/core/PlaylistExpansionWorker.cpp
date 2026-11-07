#include "PlaylistExpansionWorker.h"

#include "PlaylistExpansionParser.h"
#include "YtDlpArgsBuilder.h"
#include "YtDlpLiveStatus.h"
#include "core/ProcessUtils.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QBuffer>
#include <QUrlQuery>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <array>

#include <chrono>

namespace {
QString cleanUrlForProbe(const QString &urlStr)
{
    QUrl url(urlStr);
    if (!url.isValid()) {
        return urlStr;
    }
    QUrlQuery query(url);
    static const QStringList indexKeys = {
        QStringLiteral("img_index"), QStringLiteral("slide"), QStringLiteral("item"), QStringLiteral("index"), QStringLiteral("playlist_index")
    };
    bool changed = false;
    for (const QString &key : indexKeys) {
        if (query.hasQueryItem(key)) {
            query.removeQueryItem(key);
            changed = true;
        }
    }
    if (changed) {
        url.setQuery(query);
        return url.toString();
    }
    return urlStr;
}

bool looksLikeSearchUrl(const QString &urlString)
{
    const QUrl url(urlString);
    if (!url.isValid()) {
        return false;
    }

    bool hasSearchPath = false;
    const QStringList pathParts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : pathParts) {
        if (part.compare(QStringLiteral("search"), Qt::CaseInsensitive) == 0) {
            hasSearchPath = true;
            break;
        }
    }
    if (!hasSearchPath) {
        return false;
    }

    const QUrlQuery query(url);
    return query.hasQueryItem(QStringLiteral("q"))
        || query.hasQueryItem(QStringLiteral("query"))
        || query.hasQueryItem(QStringLiteral("search"));
}

bool looksLikePlaylistUrl(const QString &urlString)
{
    const QUrl url(urlString);
    if (!url.isValid()) {
        return false;
    }

    static const QStringList playlistPathMarkers = {
        QStringLiteral("playlist"), QStringLiteral("collection"), QStringLiteral("album")
    };
    const QStringList pathParts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : pathParts) {
        if (playlistPathMarkers.contains(part, Qt::CaseInsensitive)) {
            return true;
        }
    }

    static const QStringList playlistQueryKeys = {
        QStringLiteral("list"), QStringLiteral("playlist"), QStringLiteral("collection"), QStringLiteral("album")
    };
    const QUrlQuery query(url);
    for (const QString &key : playlistQueryKeys) {
        if (query.hasQueryItem(key)) {
            return true;
        }
    }
    return false;
}
}

PlaylistExpansionWorker::PlaylistExpansionWorker(const QString &url, ConfigManager *configManager, QObject *parent)
    : QObject(parent)
    , m_url(url)
    , m_configManager(configManager)
    , m_process(new QProcess(this))
{
    ProcessUtils::setProcessEnvironment(*m_process);
    connect(m_process, &QProcess::finished, this, &PlaylistExpansionWorker::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit expansionFinished(m_url, {}, tr("Failed to start yt-dlp process. Please check your configuration."));
        }
    });

    QBuffer *stdoutBuffer = new QBuffer(m_process);
    stdoutBuffer->open(QIODevice::ReadWrite);
    m_process->setProperty("stdoutBuffer", QVariant::fromValue<QObject*>(stdoutBuffer));
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this, stdoutBuffer]() {
        stdoutBuffer->write(m_process->readAllStandardOutput());
    });

    QBuffer *stderrBuffer = new QBuffer(m_process);
    stderrBuffer->open(QIODevice::ReadWrite);
    m_process->setProperty("stderrBuffer", QVariant::fromValue<QObject*>(stderrBuffer));
    connect(m_process, &QProcess::readyReadStandardError, this, [this, stderrBuffer]() {
        stderrBuffer->write(m_process->readAllStandardError());
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
    m_options.insert(QStringLiteral("is_playlist_expansion"), true);

    const QString cleanUrl = cleanUrlForProbe(m_url);
    QStringList args = builder.build(m_configManager, cleanUrl, m_options);

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

    args.removeIf([](const QString &arg) {
        return arg.startsWith(QStringLiteral("--wait-for-video"));
    });

    removeArgWithValue(QStringLiteral("-o"));
    removeArgWithValue(QStringLiteral("--ffmpeg-location"));
    removeArgWithValue(QStringLiteral("--print"));

    // Playlist probes only need entry URLs and lightweight display metadata.
    // Resolving every video recursively here duplicates work done by each
    // download worker and can make a second playlist probe time out while the
    // first playlist is already downloading. Flat extraction keeps the probe
    // bounded; each queued item obtains full metadata during its own download.
    if (looksLikePlaylistUrl(cleanUrl) || looksLikeSearchUrl(cleanUrl)) {
        args << QStringLiteral("--flat-playlist");
    }

    // Playlist expansion probes must apply the selected playlist policy
    // themselves: YtDlpArgsBuilder intentionally omits these flags for all
    // metadata-only expansion probes. In particular, video URLs carrying a
    // playlist identifier are otherwise often reduced to the selected video.
    args.removeAll(QStringLiteral("--yes-playlist"));
    args.removeAll(QStringLiteral("--no-playlist"));
    const bool alwaysExpandPlaylist = playlistLogic == QStringLiteral("Download All (no prompt)")
        || playlistLogic == QStringLiteral("Download Single (ignore playlist)");
    const bool askForPlaylistSelection = playlistLogic == QStringLiteral("Ask")
        && looksLikePlaylistUrl(cleanUrl);
    if (alwaysExpandPlaylist || askForPlaylistSelection) {
        // Single mode needs entries for playlist-only URLs; DownloadManager
        // reduces them to the first item before queueing. Each real track
        // download receives --no-playlist from its per-item options.
        args << QStringLiteral("--yes-playlist");
    }

    args << QStringLiteral("--dump-single-json")
         << QStringLiteral("--no-download");

    return args;
}

void PlaylistExpansionWorker::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_process->property("timed_out").toBool()) {
        m_process->setProperty("timed_out", false);
        return;
    }

    QBuffer *stderrBuffer = qobject_cast<QBuffer*>(m_process->property("stderrBuffer").value<QObject*>());
    if (stderrBuffer) stderrBuffer->write(m_process->readAllStandardError());

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        const QString stderrOutput = stderrBuffer ? QString::fromUtf8(stderrBuffer->data()) : QString::fromUtf8(m_process->readAllStandardError());
        QString errorMessage = tr("Failed to expand playlist.");

        const qsizetype errIdx = stderrOutput.indexOf(QStringLiteral("ERROR:"));
        if (errIdx != -1) {
            const qsizetype endIdx = stderrOutput.indexOf(QLatin1Char('\n'), errIdx);
            errorMessage = QStringLiteral("%1\n%2").arg(errorMessage, stderrOutput.mid(errIdx, endIdx == -1 ? -1 : endIdx - errIdx).trimmed());
        }

        static const QRegularExpression bypassRe(
            QStringLiteral("Premieres in|Premiering in|Premiere will begin|live event will begin|is upcoming|Offline \\(expected\\)|Offline expected|waiting for premiere|waiting for livestream|Live in |Starting in |Private video|Sign in to confirm|Video unavailable|This live event has ended|not currently live|empty media response|No video formats found"),
            QRegularExpression::CaseInsensitiveOption
        );

        if (bypassRe.match(errorMessage).hasMatch()
                && !m_url.contains(QStringLiteral("playlist"), Qt::CaseInsensitive)
                && !looksLikeSearchUrl(m_url)) {
            qDebug() << "Playlist expansion hit a known video-level error. Bypassing to let YtDlpWorker handle it. Error:" << errorMessage;
            QVariantMap item;
            item.insert(QStringLiteral("url"), m_url);
            item.insert(QStringLiteral("is_playlist"), false);
            item.insert(QStringLiteral("playlist_index"), -1);
            if (YtDlpLiveStatus::isExplicitUpcomingDiagnostic(errorMessage)) {
                YtDlpLiveStatus::markUpcoming(&item);
                qInfo() << "Playlist expansion marked fallback item as an upcoming livestream; native yt-dlp downloader will be used.";
            }
            emit expansionFinished(m_url, {item}, QString());
            return;
        }

        emit expansionFinished(m_url, {}, errorMessage);
        return;
    }

    QBuffer *stdoutBuffer = qobject_cast<QBuffer*>(m_process->property("stdoutBuffer").value<QObject*>());
    if (stdoutBuffer) stdoutBuffer->write(m_process->readAllStandardOutput());
    const QByteArray jsonData = stdoutBuffer ? stdoutBuffer->data() : m_process->readAllStandardOutput();

    const QString originalUrl = m_url;
    const QString playlistLogic = m_currentPlaylistLogic;
    const QVariantMap options = m_options;
    auto *watcher = new QFutureWatcher<QVariantMap>(this);
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, originalUrl, playlistLogic, options]() {
        const QVariantMap outcome = watcher->result();
        watcher->deleteLater();
        const QString error = outcome.value(QStringLiteral("error")).toString();
        if (!error.isEmpty()) {
            emit expansionFinished(originalUrl, {}, tr("Failed to parse playlist JSON: %1").arg(error));
            return;
        }

        const QList<QVariantMap> items = outcome.value(QStringLiteral("items")).value<QList<QVariantMap>>();
        if (outcome.value(QStringLiteral("is_playlist")).toBool()
                && playlistLogic == QStringLiteral("Ask")) {
            emit playlistDetected(originalUrl, items.count(), options, items);
        } else {
            emit expansionFinished(originalUrl, items, QString());
        }
    });
    watcher->setFuture(QtConcurrent::run([jsonData, originalUrl]() {
        QVariantMap outcome;
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            outcome.insert(QStringLiteral("error"), parseError.errorString());
            return outcome;
        }

        QJsonObject jsonObj = doc.object();
        const auto fixLiveStatus = [](QJsonObject &object) {
            if (!object.contains(QStringLiteral("live_status"))) {
                return;
            }
            const QString status = object.value(QStringLiteral("live_status")).toString();
            if (status == QStringLiteral("was_live") || status == QStringLiteral("not_live")
                    || status == QStringLiteral("post_live")) {
                object.insert(QStringLiteral("is_live"), false);
            } else if (status == QStringLiteral("is_live") || status == QStringLiteral("is_upcoming")) {
                object.insert(QStringLiteral("is_live"), true);
            }
        };

        fixLiveStatus(jsonObj);
        if (jsonObj.value(QStringLiteral("entries")).isArray()) {
            QJsonArray entries = jsonObj.value(QStringLiteral("entries")).toArray();
            for (qsizetype i = 0; i < entries.size(); ++i) {
                const QJsonValue value = entries.at(i);
                if (value.isObject()) {
                    QJsonObject entry = value.toObject();
                    fixLiveStatus(entry);
                    entries.replace(i, entry);
                }
            }
            jsonObj.insert(QStringLiteral("entries"), entries);
        }

        const PlaylistExpansionParseResult parsed = PlaylistExpansionParser::parse(jsonObj, originalUrl);
        outcome.insert(QStringLiteral("is_playlist"), parsed.isPlaylist);
        outcome.insert(QStringLiteral("items"), QVariant::fromValue(parsed.items));
        return outcome;
    }));
}
