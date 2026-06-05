#pragma once

#include <QObject>
#include "DownloadItem.h"

class ConfigManager;
class SortingManager;
class ArchiveManager;

class DownloadFinalizer : public QObject {
    Q_OBJECT

public:
    explicit DownloadFinalizer(ConfigManager *configManager, SortingManager *sortingManager, ArchiveManager *archiveManager, QObject *parent = nullptr);

    // Passed by value so the finalizer can modify its own local copy
    void finalize(const QString &id, DownloadItem item);

signals:
    void progressUpdated(const QString &id, const QVariantMap &data);
    void finalizationComplete(const QString &id, bool success, const QString &message);
    void finalPathReady(const QString &id, const QString &path);

private:
    bool copyDirectoryRecursively(const QString &sourceDir, const QString &destDir);

    ConfigManager *m_configManager;
    SortingManager *m_sortingManager;
    ArchiveManager *m_archiveManager;
};