#include "PlaylistExpansionParser.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

namespace {
QString resolveExpandedItemUrl(const QJsonObject &entry)
{
    const QString webpageUrl = entry.value(QStringLiteral("webpage_url")).toString().trimmed();
    if (!webpageUrl.isEmpty()) {
        return webpageUrl;
    }

    const QString urlValue = entry.value(QStringLiteral("url")).toString().trimmed();
    if (urlValue.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
            || urlValue.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        return urlValue;
    }

    const QString entryId = entry.value(QStringLiteral("id")).toString().trimmed();
    const QString extractorKey = entry.value(QStringLiteral("ie_key")).toString().trimmed();
    const QString normalizedExtractor = extractorKey.isEmpty()
        ? entry.value(QStringLiteral("extractor_key")).toString().trimmed()
        : extractorKey;

    if (normalizedExtractor.compare(QStringLiteral("Youtube"), Qt::CaseInsensitive) == 0 && !entryId.isEmpty()) {
        return QStringLiteral("https://www.youtube.com/watch?v=%1").arg(entryId);
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

QString thumbnailUrlFrom(const QJsonObject &object)
{
    QString thumbnailUrl;
    if (object.contains(QStringLiteral("thumbnails")) && object.value(QStringLiteral("thumbnails")).isArray()) {
        const QJsonArray thumbs = object.value(QStringLiteral("thumbnails")).toArray();
        if (!thumbs.isEmpty()) {
            thumbnailUrl = thumbs.last().toObject().value(QStringLiteral("url")).toString();
        }
    }
    if (thumbnailUrl.isEmpty() && object.contains(QStringLiteral("thumbnail")) && object.value(QStringLiteral("thumbnail")).isString()) {
        thumbnailUrl = object.value(QStringLiteral("thumbnail")).toString();
    }
    return thumbnailUrl;
}

void copyCommonMetadata(const QJsonObject &source, QVariantMap *item)
{
    if (!item) {
        return;
    }
    if (source.contains(QStringLiteral("title")) && source.value(QStringLiteral("title")).isString()) {
        item->insert(QStringLiteral("title"), source.value(QStringLiteral("title")).toString());
    }
    if (source.contains(QStringLiteral("is_live")) && source.value(QStringLiteral("is_live")).isBool()) {
        item->insert(QStringLiteral("is_live"), source.value(QStringLiteral("is_live")).toBool());
    }
    const QString thumbnailUrl = thumbnailUrlFrom(source);
    if (!thumbnailUrl.isEmpty()) {
        item->insert(QStringLiteral("thumbnail_url"), thumbnailUrl);
    }
}
}

PlaylistExpansionParseResult PlaylistExpansionParser::parse(const QJsonObject &root, const QString &originalUrl)
{
    PlaylistExpansionParseResult result;

    if (root.contains(QStringLiteral("entries")) && root.value(QStringLiteral("entries")).isArray()) {
        result.isPlaylist = true;
        const QString playlistTitle = firstStringValue(root, QStringList{
            QStringLiteral("playlist_title"),
            QStringLiteral("playlist"),
            QStringLiteral("album"),
            QStringLiteral("title")
        });
        const QJsonArray entries = root.value(QStringLiteral("entries")).toArray();
        for (const QJsonValue &value : entries) {
            const QJsonObject entry = value.toObject();
            const QString resolvedUrl = resolveExpandedItemUrl(entry);
            if (resolvedUrl.isEmpty()) {
                continue;
            }

            QVariantMap item;
            item.insert(QStringLiteral("url"), resolvedUrl);
            item.insert(QStringLiteral("is_playlist"), true);
            item.insert(QStringLiteral("playlist_index"), entry.value(QStringLiteral("playlist_index")).toInt(-1));
            copyCommonMetadata(entry, &item);

            QString entryPlaylistTitle = firstStringValue(entry, QStringList{
                QStringLiteral("playlist_title"),
                QStringLiteral("playlist"),
                QStringLiteral("album")
            });
            if (entryPlaylistTitle.isEmpty()) {
                entryPlaylistTitle = playlistTitle;
            }
            if (!entryPlaylistTitle.isEmpty()) {
                item.insert(QStringLiteral("playlist_title"), entryPlaylistTitle);
            }
            result.items.append(item);
        }
        return result;
    }

    QVariantMap item;
    item.insert(QStringLiteral("url"), originalUrl);
    item.insert(QStringLiteral("is_playlist"), false);
    item.insert(QStringLiteral("playlist_index"), -1);
    copyCommonMetadata(root, &item);
    const QString playlistTitle = firstStringValue(root, QStringList{
        QStringLiteral("playlist_title"),
        QStringLiteral("playlist"),
        QStringLiteral("album")
    });
    if (!playlistTitle.isEmpty()) {
        item.insert(QStringLiteral("playlist_title"), playlistTitle);
    }
    result.items.append(item);
    return result;
}
