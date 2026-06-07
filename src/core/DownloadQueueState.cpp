#include "DownloadQueueState.h"
#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QDebug>
#include <QCoreApplication>

DownloadQueueState::DownloadQueueState(QObject *parent)
    : QObject(parent)
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (QCoreApplication::arguments().contains(QStringLiteral("--headless")) || QCoreApplication::arguments().contains(QStringLiteral("--server")) || QCoreApplication::arguments().contains(QStringLiteral("--background"))) {
        configDir = QDir(configDir).filePath(QStringLiteral("Server"));
    }
    QDir().mkpath(configDir);
    m_backupPath = QDir(configDir).filePath(QStringLiteral("downloads_backup.json"));
}

QJsonArray DownloadQueueState::load()
{
    QFile file(m_backupPath);
    if (!file.exists()) {
        return QJsonArray();
    }

    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray data = file.readAll();
        file.close();
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        const QJsonArray arr = doc.array();
        if (parseError.error == QJsonParseError::NoError && doc.isArray() && !arr.isEmpty()) {
            QJsonArray validArray;
            for (const QJsonValue &val : arr) {
                if (val.isObject()) {
                    validArray.append(val);
                } else {
                    qWarning() << "Skipping invalid non-object element in download queue backup file.";
                }
            }
            if (!validArray.isEmpty()) {
                emit resumeDownloadsRequested(validArray);
                return validArray;
            }
        } else if (parseError.error != QJsonParseError::NoError && !data.isEmpty()) {
            qWarning() << "Failed to parse download queue backup file:" << parseError.errorString();
        }
    }
    return QJsonArray();
}

void DownloadQueueState::save(const QList<DownloadItem>& activeItems, const QMap<QString, DownloadItem>& pausedItems, const QQueue<DownloadItem>& downloadQueue)
{
    QJsonArray queueArray;

    auto appendItem = [&queueArray](const DownloadItem &item, const QString &status) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), item.id);
        obj.insert(QStringLiteral("url"), item.url);
        obj.insert(QStringLiteral("options"), QJsonObject::fromVariantMap(item.options));
        obj.insert(QStringLiteral("metadata"), QJsonObject::fromVariantMap(item.metadata));
        obj.insert(QStringLiteral("status"), status);
        obj.insert(QStringLiteral("playlistIndex"), item.playlistIndex);
        obj.insert(QStringLiteral("tempFilePath"), item.tempFilePath);
        obj.insert(QStringLiteral("originalDownloadedFilePath"), item.originalDownloadedFilePath);
        queueArray.append(obj);
    };

    for (const DownloadItem &item : activeItems) {
        appendItem(item, QStringLiteral("queued")); // Active items revert to queued on app start
    }
    for (const DownloadItem &item : pausedItems) {
        const QString status = (item.options.value(QStringLiteral("is_stopped")).toBool() || item.options.value(QStringLiteral("is_failed")).toBool())
            ? QStringLiteral("stopped") : QStringLiteral("paused");
        appendItem(item, status);
    }
    for (const auto& item : downloadQueue) {
        appendItem(item, QStringLiteral("queued"));
    }

    if (queueArray.isEmpty()) {
        QFile::remove(m_backupPath);
    } else {
        QSaveFile file(m_backupPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(queueArray).toJson(QJsonDocument::Compact));
            if (!file.commit()) {
                qWarning() << "Failed to commit download queue backup file:" << file.errorString();
            }
        } else {
            qWarning() << "Failed to open download queue backup file for writing:" << file.errorString();
        }
    }
}

void DownloadQueueState::clear()
{
    QFile::remove(m_backupPath);
}
