#include "PlaylistExpansionParser.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>
#include <array>

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
    if (source.contains(QStringLiteral("live_status"))) {
        const QString liveStatus = source.value(QStringLiteral("live_status")).toString();
        item->insert(QStringLiteral("live_status"), liveStatus);
        if (liveStatus == QStringLiteral("was_live") || liveStatus == QStringLiteral("not_live") || liveStatus == QStringLiteral("post_live")) {
            item->insert(QStringLiteral("is_live"), false);
        } else if (liveStatus == QStringLiteral("is_live") || liveStatus == QStringLiteral("is_upcoming")) {
            item->insert(QStringLiteral("is_live"), true);
        }
    } else if (source.contains(QStringLiteral("is_live")) && source.value(QStringLiteral("is_live")).isBool()) {
        item->insert(QStringLiteral("is_live"), source.value(QStringLiteral("is_live")).toBool());
    }
    const QString thumbnailUrl = thumbnailUrlFrom(source);
    if (!thumbnailUrl.isEmpty()) {
        item->insert(QStringLiteral("thumbnail_url"), thumbnailUrl);
    }
}

int getRequestedIndex(const QString &url)
{
    const QUrl parsedUrl(url);
    if (!parsedUrl.isValid()) {
        return -1;
    }

    const QUrlQuery query(parsedUrl);
    static constexpr std::array<QStringView, 5> indexKeys = {
        u"img_index", u"slide", u"item", u"index", u"playlist_index"
    };

    for (const QStringView key : indexKeys) {
        if (query.hasQueryItem(key.toString())) {
            const QString value = query.queryItemValue(key.toString());
            bool ok = false;
            const int val = value.toInt(&ok);
            if (ok && val > 0) {
                return val;
            }
        }
    }
    return -1;
}

bool matchesTitleIndex(const QString &title, int index)
{
    if (title.isEmpty()) return false;
    const QString lowerTitle = title.toLower();
    const QString numStr = QString::number(index);
    if (lowerTitle == numStr) return true;

    static constexpr std::array<QStringView, 5> prefixes = {
        u"video ", u"slide ", u"image ", u"item ", u"post "
    };
    for (QStringView prefix : prefixes) {
        if (lowerTitle.startsWith(prefix) && lowerTitle.mid(prefix.length()).trimmed().startsWith(numStr)) {
            return true;
        }
        const QString searchPattern = prefix.toString() + numStr;
        if (lowerTitle.contains(searchPattern)) {
            return true;
        }
    }
    return false;
}
}

PlaylistExpansionParseResult PlaylistExpansionParser::parse(const QJsonObject &root, const QString &originalUrl)
{
    PlaylistExpansionParseResult result;
    const int reqIndex = getRequestedIndex(originalUrl);

    if (root.contains(QStringLiteral("entries")) && root.value(QStringLiteral("entries")).isArray()) {
        result.isPlaylist = true;
        const QString playlistTitle = firstStringValue(root, QStringList{
            QStringLiteral("playlist_title"),
            QStringLiteral("playlist"),
            QStringLiteral("album"),
            QStringLiteral("title")
        });
        const QJsonArray entries = root.value(QStringLiteral("entries")).toArray();
        QList<QVariantMap> parsedItems;
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
            parsedItems.append(item);
        }

        if (reqIndex > 0) {
            QVariantMap matchedItem;
            // 1. Try matching the index against the item's title (essential for Instagram carousel items)
            for (const QVariantMap &item : parsedItems) {
                if (matchesTitleIndex(item.value(QStringLiteral("title")).toString(), reqIndex)) {
                    matchedItem = item;
                    break;
                }
            }
            // 2. Try matching direct playlist index (standard video playlists)
            if (matchedItem.isEmpty()) {
                for (const QVariantMap &item : parsedItems) {
                    if (item.value(QStringLiteral("playlist_index")).toInt() == reqIndex) {
                        matchedItem = item;
                        break;
                    }
                }
            }
            // 3. Fallback to list-offset mapping
            if (matchedItem.isEmpty() && reqIndex <= parsedItems.size()) {
                matchedItem = parsedItems.at(reqIndex - 1);
            }
            if (!matchedItem.isEmpty()) {
                result.items.append(matchedItem);
                return result;
            }
        }
        result.items = parsedItems;
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
