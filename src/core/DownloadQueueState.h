#pragma once

#include "DownloadItem.h"
#include <QObject>
#include <QJsonArray>
#include <QQueue>
#include <QMap>
#include <QList>

class DownloadQueueState : public QObject
{
    Q_OBJECT
public:
    explicit DownloadQueueState(QObject *parent = nullptr);
    QJsonArray load();
    void save(const QList<DownloadItem>& activeItems, const QMap<QString, DownloadItem>& pausedItems, const QQueue<DownloadItem>& downloadQueue);
    static void saveToPath(const QString &backupPath, const QList<DownloadItem> &activeItems,
                           const QMap<QString, DownloadItem> &pausedItems,
                           const QQueue<DownloadItem> &downloadQueue);
    QString backupPath() const { return m_backupPath; }
    void clear(); // To remove the backup file
signals:
    void resumeDownloadsRequested(const QJsonArray &arr);
private:
    QString m_backupPath;
};
