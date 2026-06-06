#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QMutex>
#include "ConfigManager.h"

/**
 * @class ArchiveManager
 * @brief Manages a database of previously downloaded URLs to prevent duplicates.
 *
 * This class uses an SQLite database to keep a record of all completed downloads.
 * It provides methods to check if a URL has already been downloaded (the "archive")
 * and to add new URLs to it. It is thread-safe and handles database connections
 * per-thread.
 */
class ArchiveManager : public QObject {
    Q_OBJECT
    friend class TestArchiveManager;

public:
    /**
     * @brief Constructs an ArchiveManager using the default database path.
     * @param configManager A pointer to the application's ConfigManager.
     * @param parent The parent QObject.
     */
    explicit ArchiveManager(ConfigManager *configManager, QObject *parent = nullptr);

    /**
     * @brief Constructs an ArchiveManager with a custom database path, mainly for testing.
     * @param configManager A pointer to the application's ConfigManager.
     * @param dbPath The absolute path to the SQLite database file.
     * @param forTesting If true, enables test-specific logging.
     * @param parent The parent QObject.
     */
    explicit ArchiveManager(ConfigManager *configManager, const QString &dbPath, bool forTesting, QObject *parent = nullptr);

    /**
     * @brief Destructor that ensures database connections are cleaned up.
     */
    ~ArchiveManager();

    /**
     * @brief Checks if a URL is already in the download archive.
     *
     * This method normalizes the URL and also checks for provider-specific IDs (like YouTube video IDs)
     * to perform a more robust duplicate check.
     *
     * @param url The URL to check.
     * @return True if the URL is found in the archive, false otherwise.
     */
    [[nodiscard]] bool isInArchive(const QString &url);

    /**
     * @brief Adds a URL to the download archive.
     * @param url The URL to add.
     */
    void addToArchive(const QString &url);

    /**
     * @brief Gets the absolute path to the archive database file.
     * @return The database file path.
     */
    [[nodiscard]] QString getArchiveDbPath() const;

    /**
     * @brief Closes the database connection for the current thread.
     */
    void closeDatabase();

private:
    ConfigManager *m_configManager;
    QString m_dbPath;
    QMutex m_mutex;

    void ensureSchema();
    void backfillIdentityColumns();

    QSqlDatabase getDatabase();
    QString currentThreadConnectionName() const;
    void closeCurrentThreadDatabase();

    struct UrlIdentity {
        QString provider;
        QString mediaId;
        QString normalizedUrl;
    };

    UrlIdentity buildIdentity(const QString &url) const;
    QString extractVideoId(const QString &url) const;
    QString normalizeUrl(const QString &url) const;
};
