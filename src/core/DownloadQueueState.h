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
    void clear(); // To remove the backup file
signals:
    void resumeDownloadsRequested(const QJsonArray &arr);
private:
    QString m_backupPath;
};