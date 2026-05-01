#include "PlaylistExpander.h"
#include "YtDlpArgsBuilder.h"
#include "core/ProcessUtils.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QStringList>

namespace {
QString resolveExpandedItemUrl(const QJsonObject &entry)
{
    const QString webpageUrl = entry.value("webpage_url").toString().trimmed();
    if (!webpageUrl.isEmpty()) {
        return webpageUrl;
    }

    const QString urlValue = entry.value("url").toString().trimmed();
    if (urlValue.startsWith("http://", Qt::CaseInsensitive) || urlValue.startsWith("https://", Qt::CaseInsensitive)) {
        return urlValue;
    }

    const QString entryId = entry.value("id").toString().trimmed();
    const QString extractorKey = entry.value("ie_key").toString().trimmed();
    const QString normalizedExtractor = extractorKey.isEmpty()
        ? entry.value("extractor_key").toString().trimmed()
        : extractorKey;

    const QString videoId = !entryId.isEmpty() ? entryId : urlValue;
    if (normalizedExtractor.compare("Youtube", Qt::CaseInsensitive) == 0 && !videoId.isEmpty()) {
        return "https://www.youtube.com/watch?v=" + videoId;
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
}

void PlaylistExpander::startExpansion(const QString &playlistLogic) {
    m_currentPlaylistLogic = playlistLogic;
    m_options = property("options").toMap(); // Retrieve options stored earlier

    // Build full yt-dlp command using YtDlpArgsBuilder to match the actual download command
    YtDlpArgsBuilder builder;
    
    // Create a minimal options map for playlist expansion
    // We need playlist_logic to be set correctly
    m_options["playlist_logic"] = playlistLogic;
    
    QStringList args = builder.build(m_configManager, m_url, m_options);
    
    // Helper lambda to remove an argument and its value
    auto removeArgWithValue = [&args](const QString &flag) {
        int index = args.indexOf(flag);
        if (index != -1) {
            args.removeAt(index); // Remove the flag
            if (index < args.size()) {
                args.removeAt(index); // Remove the value
            }
        }
    };
    
    // Override format selection for playlist expansion - we just need URLs, not actual downloads
    // Remove the -f and related download-specific args, add --flat-playlist and --dump-single-json
    int formatIndex = args.indexOf("-f");
    if (formatIndex != -1) {
        args.removeAt(formatIndex); // Remove -f
        if (formatIndex < args.size()) {
            args.removeAt(formatIndex); // Remove the format string
        }
    }
    
    // Remove download-specific options that aren't needed for playlist expansion
    args.removeAll("--write-info-json");
    removeArgWithValue("--progress-template");
    args.removeAll("--newline");
    args.removeAll("--ignore-errors");
    removeArgWithValue("--external-downloader");
    removeArgWithValue("--external-downloader-args");
    removeArgWithValue("--merge-output-format");
    removeArgWithValue("--ppa");
    removeArgWithValue("--convert-thumbnails");
    removeArgWithValue("--sub-langs");
    args.removeAll("--write-auto-subs");
    args.removeAll("--embed-subs");
    args.removeAll("--embed-chapters");
    args.removeAll("--embed-metadata");
    args.removeAll("--embed-thumbnail");
    removeArgWithValue("-o"); // Remove output template
    removeArgWithValue("--ffmpeg-location"); // Not needed for playlist expansion
    
    // Add playlist expansion specific options
    args << "--flat-playlist";
    args << "--dump-single-json";
    args << "--no-download"; // Don't download anything, just extract info

    // Remove the --print after_move:filepath since we're not downloading
    int printIndex = args.indexOf("--print");
    if (printIndex != -1) {
        args.removeAt(printIndex); // Remove --print
        if (printIndex < args.size()) {
            args.removeAt(printIndex); // Remove the print argument
        }
    }

    qDebug() << "PlaylistExpander executing command: yt-dlp" << args;
    
    // Find yt-dlp binary
    ProcessUtils::FoundBinary ytDlpBinary = ProcessUtils::findBinary("yt-dlp", m_configManager);
    if (ytDlpBinary.source == "Not Found" || ytDlpBinary.path.isEmpty()) {
        emit expansionFinished(m_url, {}, "yt-dlp could not be found. Configure it in Advanced Settings -> External Tools.");
        return;
    }
    
    m_process->start(ytDlpBinary.path, args);
}

void PlaylistExpander::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        QString stderrOutput = m_process->readAllStandardError();
        QString errorMessage = "Failed to expand playlist.";
        
        // Try to extract a clean error message line instead of the whole stack trace
        QStringList lines = stderrOutput.split('\n');
        for (const QString &line : lines) {
            if (line.startsWith("ERROR:")) {
                errorMessage += "\n" + line.trimmed();
                break;
            }
        }
        
        emit expansionFinished(m_url, {}, errorMessage);
        return;
    }

    QByteArray jsonData = m_process->readAllStandardOutput();
    QJsonDocument doc = QJsonDocument::fromJson(jsonData);

    if (doc.isNull() || !doc.isObject()) {
        emit expansionFinished(m_url, {}, "Failed to parse playlist JSON.");
        return;
    }

    QJsonObject root = doc.object();
    QList<QVariantMap> expandedItems;
    bool isPlaylist = false;

    if (root.contains("entries") && root["entries"].isArray()) {
        // It's a playlist
        isPlaylist = true;
        
        QString playlistTitle = firstStringValue(root, QStringList{"playlist_title", "playlist", "album", "title"});
        QJsonArray entries = root["entries"].toArray();
        for (const QJsonValue &value : entries) {
            QJsonObject entry = value.toObject();
            const QString resolvedUrl = resolveExpandedItemUrl(entry);
            if (!resolvedUrl.isEmpty()) {
                QVariantMap item;
                item["url"] = resolvedUrl;
                item["is_playlist"] = true;
                item["playlist_index"] = entry.value("playlist_index").toInt(-1);
                if (entry.contains("title") && entry["title"].isString()) {
                    item["title"] = entry["title"].toString();
                }
                QString entryPlaylistTitle = firstStringValue(entry, QStringList{"playlist_title", "playlist", "album"});
                if (entryPlaylistTitle.isEmpty()) {
                    entryPlaylistTitle = playlistTitle;
                }
                if (!entryPlaylistTitle.isEmpty()) {
                    item["playlist_title"] = entryPlaylistTitle;
                }
                expandedItems.append(item);
            }
        }
    } else {
        // It's a single video
        QVariantMap item;
        item["url"] = m_url;
        item["is_playlist"] = false;
        item["playlist_index"] = -1;
        if (root.contains("title") && root["title"].isString()) {
            item["title"] = root["title"].toString();
        }
        const QString playlistTitle = firstStringValue(root, QStringList{"playlist_title", "playlist", "album"});
        if (!playlistTitle.isEmpty()) {
            item["playlist_title"] = playlistTitle;
        }
        expandedItems.append(item);
    }

    if (isPlaylist && m_currentPlaylistLogic == "Ask") {
        emit playlistDetected(m_url, expandedItems.count(), m_options, expandedItems); // Emit expandedItems
    } else {
        emit expansionFinished(m_url, expandedItems, "");
    }
}
