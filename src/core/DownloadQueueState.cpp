#include "DownloadQueueState.h"
#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <QCoreApplication>

DownloadQueueState::DownloadQueueState(QObject *parent)
    : QObject(parent)
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (QCoreApplication::arguments().contains("--headless") || QCoreApplication::arguments().contains("--server")) {
        configDir = QDir(configDir).filePath("Server");
    }
    QDir().mkpath(configDir);
    m_backupPath = QDir(configDir).filePath("downloads_backup.json");
}

QJsonArray DownloadQueueState::load()
{
    QFile file(m_backupPath);
    if (!file.exists()) {
        return QJsonArray();
    }

    if (file.open(QIODevice::ReadOnly)) {
        QByteArray data = file.readAll();
        file.close();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isArray() && !doc.array().isEmpty()) {
            QJsonArray arr = doc.array();
            emit resumeDownloadsRequested(arr);
            return arr;
        }
    }
    return QJsonArray();
}

void DownloadQueueState::save(const QList<DownloadItem>& activeItems, const QMap<QString, DownloadItem>& pausedItems, const QQueue<DownloadItem>& downloadQueue)
{
    QJsonArray queueArray;
    for (const DownloadItem &item : activeItems) {
        QJsonObject obj;
        obj["id"] = item.id;
        obj["url"] = item.url;
        obj["options"] = QJsonObject::fromVariantMap(item.options);
        obj["metadata"] = QJsonObject::fromVariantMap(item.metadata);
        obj["status"] = "queued"; // Active items revert to queued on app start
        obj["playlistIndex"] = item.playlistIndex;
        obj["tempFilePath"] = item.tempFilePath;
        obj["originalDownloadedFilePath"] = item.originalDownloadedFilePath;
        queueArray.append(obj);
    }
    for (const DownloadItem &item : pausedItems) {
        QJsonObject obj;
        obj["id"] = item.id;
        obj["url"] = item.url;
        obj["options"] = QJsonObject::fromVariantMap(item.options);
        obj["metadata"] = QJsonObject::fromVariantMap(item.metadata);
        if (item.options.value("is_stopped").toBool() || item.options.value("is_failed").toBool()) {
            obj["status"] = "stopped";
        } else {
            obj["status"] = "paused";
        }
        obj["playlistIndex"] = item.playlistIndex;
        obj["tempFilePath"] = item.tempFilePath;
        obj["originalDownloadedFilePath"] = item.originalDownloadedFilePath;
        queueArray.append(obj);
    }
    for (const auto& item : downloadQueue) {
        QJsonObject obj;
        obj["id"] = item.id;
        obj["url"] = item.url;
        obj["options"] = QJsonObject::fromVariantMap(item.options);
        obj["metadata"] = QJsonObject::fromVariantMap(item.metadata);
        obj["status"] = "queued";
        obj["playlistIndex"] = item.playlistIndex;
        obj["tempFilePath"] = item.tempFilePath;
        obj["originalDownloadedFilePath"] = item.originalDownloadedFilePath;
        queueArray.append(obj);
    }

    if (queueArray.isEmpty()) {
        QFile::remove(m_backupPath);
    } else {
        QFile file(m_backupPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(queueArray).toJson());
            file.close();
        } else {
            qWarning() << "Failed to open download queue backup file for writing:" << m_backupPath;
        }
    }
}

void DownloadQueueState::clear()
{
    QFile::remove(m_backupPath);
}
