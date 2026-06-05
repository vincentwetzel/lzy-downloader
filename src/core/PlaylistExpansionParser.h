#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>

class QJsonObject;

struct PlaylistExpansionParseResult {
    QList<QVariantMap> items;
    bool isPlaylist = false;
};

class PlaylistExpansionParser {
public:
    [[nodiscard]] static PlaylistExpansionParseResult parse(const QJsonObject &root, const QString &originalUrl);
};
