#include "PlaylistExpander.h"
#include "YtDlpArgsBuilder.h"
#include "core/ProcessUtils.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <chrono>

namespace {
QString resolveExpandedItemUrl(const QJsonObject &entry)
{
    const QString webpageUrl = entry.value(QStringLiteral("webpage_url")).toString().trimmed();
    if (!webpageUrl.isEmpty()) {
        return webpageUrl;
    }

    const QString urlValue = entry.value(QStringLiteral("url")).toString().trimmed();
    if (urlValue.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) || urlValue.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        return urlValue;
    }

    const QString entryId = entry.value(QStringLiteral("id")).toString().trimmed();
    const QString extractorKey = entry.value(QStringLiteral("ie_key")).toString().trimmed();
    const QString normalizedExtractor = extractorKey.isEmpty()
        ? entry.value(QStringLiteral("extractor_key")).toString().trimmed()
        : extractorKey;

    const QString videoId = !entryId.isEmpty() ? entryId : urlValue;
    if (normalizedExtractor.compare(QStringLiteral("Youtube"), Qt::CaseInsensitive) == 0 && !videoId.isEmpty()) {
        return QStringLiteral("https://www.youtube.com/watch?v=%1").arg(videoId);
    }

    return urlValue;
}

QString firstStringValue(const QJsonObject &object, const QStringList &keys)
{
    for (const QString &key : keys) {
        const QString value = object.value(key).toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return QString();
}
}

PlaylistExpander::PlaylistExpander(const QString &url, ConfigManager *configManager, QObject *parent)
    : QObject(parent), m_url(url), m_configManager(configManager) {

    m_process = new QProcess(this);
    connect(m_process, &QProcess::finished, this, &PlaylistExpander::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit expansionFinished(m_url, {}, tr("Failed to start yt-dlp process. Please check your configuration."));
        }
    });
}

void PlaylistExpander::startExpansion(const QString &playlistLogic) {
    m_currentPlaylistLogic = playlistLogic;
    m_options = property("options").toMap(); // Retrieve options stored earlier

    // gallery-dl handles its own galleries and pagination; it does not need yt-dlp to expand it.
    // Bypass the yt-dlp playlist check entirely to prevent hanging on JS challenges!
    if (m_options.value(QStringLiteral("type")).toString() == QStringLiteral("gallery")) {
        QVariantMap item;
        item[QStringLiteral("url")] = m_url;
        item[QStringLiteral("is_playlist")] = false;
        item[QStringLiteral("playlist_index")] = -1;
        emit expansionFinished(m_url, {item}, QString());
        return;
    }

    // Build full yt-dlp command using YtDlpArgsBuilder to match the actual download command
    YtDlpArgsBuilder builder;
    
    // Create a minimal options map for playlist expansion
    // We need playlist_logic to be set correctly
    m_options[QStringLiteral("playlist_logic")] = playlistLogic;
    m_options[QStringLiteral("skip_dir_creation")] = true; // Prevent creating a stranded UUID temp folder
    
    QStringList args = builder.build(m_configManager, m_url, m_options);
    
    // Helper lambda to remove an argument and its value
    auto removeArgWithValue = [&args](const QString &flag) {
        int index = args.indexOf(flag);
        while (index != -1) {
            args.removeAt(index); // Remove the flag
            if (index < args.size()) {
                args.removeAt(index); // Remove the value
            }
            index = args.indexOf(flag);
        }
    };
    
    // Override format selection for playlist expansion - we just need URLs, not actual downloads
    // Remove the -f and related download-specific args, add --flat-playlist and --dump-single-json
    removeArgWithValue(QStringLiteral("-f"));
    
    // Remove download-specific options that aren't needed for playlist expansion
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
    removeArgWithValue(QStringLiteral("-o")); // Remove output template
    removeArgWithValue(QStringLiteral("--ffmpeg-location")); // Not needed for playlist expansion
    
    // Add playlist expansion specific options
    args << QStringLiteral("--flat-playlist");
    args << QStringLiteral("--dump-single-json");
    args << QStringLiteral("--no-download"); // Don't download anything, just extract info

    // Remove the --print after_move:filepath since we're not downloading
    removeArgWithValue(QStringLiteral("--print"));

    qDebug() << "PlaylistExpander executing command: yt-dlp" << args;
    
    // Find yt-dlp binary
    ProcessUtils::FoundBinary ytDlpBinary = ProcessUtils::findBinary(QStringLiteral("yt-dlp"), m_configManager);
    if (ytDlpBinary.source == QStringLiteral("Not Found") || ytDlpBinary.path.isEmpty()) {
        emit expansionFinished(m_url, {}, tr("yt-dlp could not be found. Configure it in Advanced Settings -> External Tools."));
        return;
    }
    
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

void PlaylistExpander::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (m_process->property("timed_out").toBool()) {
        m_process->setProperty("timed_out", false);
        return;
    }
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        QString stderrOutput = m_process->readAllStandardError();
        QString errorMessage = tr("Failed to expand playlist.");
        
        // Try to extract a clean error message line instead of the whole stack trace
        int errIdx = stderrOutput.indexOf(QStringLiteral("ERROR:"));
        if (errIdx != -1) {
            int endIdx = stderrOutput.indexOf('\n', errIdx);
            errorMessage = QStringLiteral("%1\n%2").arg(errorMessage, stderrOutput.mid(errIdx, endIdx == -1 ? -1 : endIdx - errIdx).trimmed());
        }
        
        emit expansionFinished(m_url, {}, errorMessage);
        return;
    }

    QByteArray jsonData = m_process->readAllStandardOutput();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        emit expansionFinished(m_url, {}, tr("Failed to parse playlist JSON: %1").arg(parseError.errorString()));
        return;
    }

    QJsonObject root = doc.object();
    QList<QVariantMap> expandedItems;
    bool isPlaylist = false;

    if (root.contains(QStringLiteral("entries")) && root.value(QStringLiteral("entries")).isArray()) {
        // It's a playlist
        isPlaylist = true;
        
        QString playlistTitle = firstStringValue(root, QStringList{QStringLiteral("playlist_title"), QStringLiteral("playlist"), QStringLiteral("album"), QStringLiteral("title")});
        QJsonArray entries = root.value(QStringLiteral("entries")).toArray();
        for (const QJsonValue &value : entries) {
            QJsonObject entry = value.toObject();
            const QString resolvedUrl = resolveExpandedItemUrl(entry);
            if (!resolvedUrl.isEmpty()) {
                QVariantMap item;
                item[QStringLiteral("url")] = resolvedUrl;
                item[QStringLiteral("is_playlist")] = true;
                item[QStringLiteral("playlist_index")] = entry.value(QStringLiteral("playlist_index")).toInt(-1);
                if (entry.contains(QStringLiteral("title")) && entry.value(QStringLiteral("title")).isString()) {
                    item[QStringLiteral("title")] = entry.value(QStringLiteral("title")).toString();
                }
                QString thumbnailUrl;
                if (entry.contains(QStringLiteral("thumbnails")) && entry.value(QStringLiteral("thumbnails")).isArray()) {
                    QJsonArray thumbs = entry.value(QStringLiteral("thumbnails")).toArray();
                    if (!thumbs.isEmpty()) {
                        thumbnailUrl = thumbs.last().toObject().value(QStringLiteral("url")).toString();
                    }
                }
                if (thumbnailUrl.isEmpty() && entry.contains(QStringLiteral("thumbnail")) && entry.value(QStringLiteral("thumbnail")).isString()) {
                    thumbnailUrl = entry.value(QStringLiteral("thumbnail")).toString();
                }
                if (!thumbnailUrl.isEmpty()) {
                    item[QStringLiteral("thumbnail_url")] = thumbnailUrl;
                }
                QString entryPlaylistTitle = firstStringValue(entry, QStringList{QStringLiteral("playlist_title"), QStringLiteral("playlist"), QStringLiteral("album")});
                if (entryPlaylistTitle.isEmpty()) {
                    entryPlaylistTitle = playlistTitle;
                }
                if (!entryPlaylistTitle.isEmpty()) {
                    item[QStringLiteral("playlist_title")] = entryPlaylistTitle;
                }
                expandedItems.append(item);
            }
        }
    } else {
        // It's a single video
        QVariantMap item;
        item[QStringLiteral("url")] = m_url;
        item[QStringLiteral("is_playlist")] = false;
        item[QStringLiteral("playlist_index")] = -1;
        if (root.contains(QStringLiteral("title")) && root.value(QStringLiteral("title")).isString()) {
            item[QStringLiteral("title")] = root.value(QStringLiteral("title")).toString();
        }
        QString thumbnailUrl;
        if (root.contains(QStringLiteral("thumbnails")) && root.value(QStringLiteral("thumbnails")).isArray()) {
            QJsonArray thumbs = root.value(QStringLiteral("thumbnails")).toArray();
            if (!thumbs.isEmpty()) {
                thumbnailUrl = thumbs.last().toObject().value(QStringLiteral("url")).toString();
            }
        }
        if (thumbnailUrl.isEmpty() && root.contains(QStringLiteral("thumbnail")) && root.value(QStringLiteral("thumbnail")).isString()) {
            thumbnailUrl = root.value(QStringLiteral("thumbnail")).toString();
        }
        if (!thumbnailUrl.isEmpty()) {
            item[QStringLiteral("thumbnail_url")] = thumbnailUrl;
        }
        const QString playlistTitle = firstStringValue(root, QStringList{QStringLiteral("playlist_title"), QStringLiteral("playlist"), QStringLiteral("album")});
        if (!playlistTitle.isEmpty()) {
            item[QStringLiteral("playlist_title")] = playlistTitle;
        }
        expandedItems.append(item);
    }

    if (isPlaylist && m_currentPlaylistLogic == QStringLiteral("Ask")) {
        emit playlistDetected(m_url, expandedItems.count(), m_options, expandedItems); // Emit expandedItems
    } else {
        emit expansionFinished(m_url, expandedItems, QString());
    }
}
