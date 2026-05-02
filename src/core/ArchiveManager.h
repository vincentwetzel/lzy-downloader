#ifndef ARCHIVEMANAGER_H
#define ARCHIVEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QMutex>
#include "ConfigManager.h"

class ArchiveManager : public QObject {
    Q_OBJECT

public:
    explicit ArchiveManager(ConfigManager *configManager, QObject *parent = nullptr);
    // New constructor for specifying custom database path, primarily for testing
    explicit ArchiveManager(ConfigManager *configManager, const QString &dbPath, bool forTesting, QObject *parent = nullptr);
    ~ArchiveManager();

    bool isInArchive(const QString &url);
    void addToArchive(const QString &url);
    QString getArchiveDbPath() const;
    void closeDatabase(); // New public method

private:
    ConfigManager *m_configManager;
    QString m_dbPath;
    QMutex m_mutex;

    void ensureSchema();
    void backfillIdentityColumns();

    QSqlDatabase getDatabase();

    struct UrlIdentity {
        QString provider;
        QString mediaId;
        QString normalizedUrl;
    };

    UrlIdentity buildIdentity(const QString &url) const;
    QString extractVideoId(const QString &url) const;
    QString normalizeUrl(const QString &url) const;
};

#endif // ARCHIVEMANAGER_H