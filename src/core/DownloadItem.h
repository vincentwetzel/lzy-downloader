#pragma once

#include <QString>
#include <QVariantMap>

struct DownloadItem {
    QString id;
    QString url;
    QVariantMap options;
    QString tempFilePath;
    QString originalDownloadedFilePath;
    QVariantMap metadata;
    int playlistIndex = -1;
};