#include "DownloadHistoryStorage.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QSaveFile>

QList<HistoryItemData> DownloadHistoryStorage::loadHistoryFromPath(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Failed to parse download history JSON from" << path << "Error:" << parseError.errorString();
        return {};
    }

    QList<HistoryItemData> items;
    if (!doc.isArray()) {
        return items;
    }

    const QJsonArray arr = doc.array();
    items.reserve(arr.size());
    for (const QJsonValue &value : arr) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject obj = value.toObject();
        HistoryItemData data;
        data.id = obj.value(QStringLiteral("id")).toString();
        data.title = obj.value(QStringLiteral("title")).toString();
        data.url = obj.value(QStringLiteral("url")).toString();
        data.filePath = obj.value(QStringLiteral("filePath")).toString();
        data.timestamp = obj.value(QStringLiteral("timestamp")).toString();
        data.thumbnailPath = obj.value(QStringLiteral("thumbnailPath")).toString();
        const QJsonValue totalBytes = obj.value(QStringLiteral("totalBytes"));
        if (totalBytes.isDouble() || totalBytes.isNull()) {
            data.totalBytes = totalBytes.toVariant().toLongLong();
        }
        data.duration = obj.value(QStringLiteral("duration")).toString();
        items.append(std::move(data));
    }
    return items;
}

void DownloadHistoryStorage::saveHistoryToPath(const QString &path, const QList<HistoryItemData> &items)
{
    if (path.isEmpty()) {
        return;
    }

    QJsonArray arr;
    for (const HistoryItemData &data : items) {
        QJsonObject obj;
        obj[QStringLiteral("id")] = data.id;
        obj[QStringLiteral("title")] = data.title;
        obj[QStringLiteral("url")] = data.url;
        obj[QStringLiteral("filePath")] = data.filePath;
        obj[QStringLiteral("timestamp")] = data.timestamp;
        obj[QStringLiteral("thumbnailPath")] = data.thumbnailPath;
        obj[QStringLiteral("totalBytes")] = data.totalBytes;
        obj[QStringLiteral("duration")] = data.duration;
        arr.append(obj);
    }

    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(arr).toJson());
        if (!file.commit()) {
            qWarning() << "Failed to commit download history to" << path;
        }
    }
}
