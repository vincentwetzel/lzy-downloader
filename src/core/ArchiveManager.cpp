#include "ArchiveManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QDateTime>
#include <QUrl>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QDebug>
#include <QFileInfo>
#include <algorithm>

ArchiveManager::ArchiveManager(ConfigManager *configManager, QObject *parent)
    : ArchiveManager(configManager, configManager->getConfigDir() + "/download_archive.db", false, parent) {
    // Constructor chaining to the new one
}

// New constructor for custom database path or testing
ArchiveManager::ArchiveManager(ConfigManager *configManager, const QString &dbPath, bool forTesting, QObject *parent)
    : QObject(parent), m_configManager(configManager), m_dbPath(dbPath) {
    if (forTesting) {
        qDebug() << "ArchiveManager using test database path:" << m_dbPath;
    } else {
        qDebug() << "ArchiveManager using standard database path:" << m_dbPath;
    }
    ensureSchema();
}

ArchiveManager::~ArchiveManager() {
    {
        QMutexLocker locker(&m_mutex);
        QString connectionName;
        {
            QSqlDatabase db = QSqlDatabase::database("archive_connection");
            if (db.isValid() && db.isOpen()) {
                connectionName = db.connectionName();
                db.close();
            }
        }
        if (!connectionName.isEmpty()) {
            QSqlDatabase::removeDatabase(connectionName);
        }
    }
}

QString ArchiveManager::getArchiveDbPath() const {
    return m_dbPath;
}

QSqlDatabase ArchiveManager::getDatabase() {
    QSqlDatabase db = QSqlDatabase::database("archive_connection");
    if (!db.isValid()) {
        db = QSqlDatabase::addDatabase("QSQLITE", "archive_connection");
        db.setDatabaseName(m_dbPath);
    }

    if (!db.isOpen()) {
        if (!db.open()) {
            qCritical() << "Failed to open archive database:" << db.lastError().text();
        }
    }
    return db;
}

void ArchiveManager::closeDatabase() {
    QMutexLocker locker(&m_mutex);
    QSqlDatabase db = QSqlDatabase::database("archive_connection");
    if (db.isValid() && db.isOpen()) {
        db.close();
        qDebug() << "Archive database connection closed.";
    }
}

void ArchiveManager::ensureSchema() {
    QMutexLocker locker(&m_mutex);
    QSqlDatabase db = getDatabase();
    if (!db.isOpen()) return;

    QSqlQuery query(db);

    // Create table if not exists
    if (!query.exec("CREATE TABLE IF NOT EXISTS downloads ("
                    "url TEXT PRIMARY KEY, "
                    "normalized_url TEXT, "
                    "provider TEXT, "
                    "media_id TEXT, "
                    "timestamp REAL)")) {
        qCritical() << "Failed to create downloads table:" << query.lastError().text();
        return;
    }

    // Check for missing columns and add them if necessary
    QSqlRecord record = db.record("downloads");
    if (!record.contains("normalized_url")) {
        query.exec("ALTER TABLE downloads ADD COLUMN normalized_url TEXT");
    }
    if (!record.contains("provider")) {
        query.exec("ALTER TABLE downloads ADD COLUMN provider TEXT");
    }
    if (!record.contains("media_id")) {
        query.exec("ALTER TABLE downloads ADD COLUMN media_id TEXT");
    }
    if (!record.contains("timestamp")) {
        query.exec("ALTER TABLE downloads ADD COLUMN timestamp REAL");
    }

    backfillIdentityColumns();

    // Create indices
    query.exec("CREATE INDEX IF NOT EXISTS idx_downloads_norm ON downloads(normalized_url)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_downloads_provider_media ON downloads(provider, media_id)");
}

void ArchiveManager::backfillIdentityColumns() {
    QSqlDatabase db = getDatabase();
    if (!db.isOpen()) return;

    QSqlQuery query(db);
    query.exec("SELECT url, normalized_url, provider, media_id FROM downloads "
               "WHERE normalized_url IS NULL OR normalized_url = '' "
               "OR provider IS NULL OR provider = '' "
               "OR (provider = 'youtube' AND (media_id IS NULL OR media_id = ''))");

    while (query.next()) {
        QString url = query.value(0).toString();
        UrlIdentity identity = buildIdentity(url);

        QSqlQuery updateQuery(db);
        updateQuery.prepare("UPDATE downloads SET normalized_url = ?, provider = ?, media_id = ? WHERE url = ?");
        updateQuery.addBindValue(identity.normalizedUrl);
        updateQuery.addBindValue(identity.provider);
        updateQuery.addBindValue(identity.mediaId);
        updateQuery.addBindValue(url);
        updateQuery.exec();
    }
}

void ArchiveManager::addToArchive(const QString &url) {
    UrlIdentity identity = buildIdentity(url);
    if (identity.normalizedUrl.isEmpty()) {
        qWarning() << "Could not normalize URL for archive:" << url;
        return;
    }

    QMutexLocker locker(&m_mutex);
    QSqlDatabase db = getDatabase();
    if (!db.isOpen()) return;

    QSqlQuery query(db);
    query.prepare("INSERT INTO downloads(url, normalized_url, provider, media_id, timestamp) "
                  "VALUES (?, ?, ?, ?, ?) "
                  "ON CONFLICT(url) DO UPDATE SET "
                  "normalized_url = excluded.normalized_url, "
                  "provider = excluded.provider, "
                  "media_id = excluded.media_id, "
                  "timestamp = excluded.timestamp");

    query.addBindValue(url);
    query.addBindValue(identity.normalizedUrl);
    query.addBindValue(identity.provider);
    query.addBindValue(identity.mediaId);
    query.addBindValue(QDateTime::currentMSecsSinceEpoch() / 1000.0); // Python uses time.time() which is seconds (float)

    if (!query.exec()) {
        qCritical() << "Failed writing to archive DB:" << query.lastError().text();
    } else {
        qDebug() << "Added to archive DB: provider=" << identity.provider
                 << "media_id=" << identity.mediaId << "url=" << url;
    }
}

bool ArchiveManager::isInArchive(const QString &url) {
    if (!QFileInfo::exists(m_dbPath)) return false;

    UrlIdentity identity = buildIdentity(url);
    if (identity.normalizedUrl.isEmpty()) return false;

    QMutexLocker locker(&m_mutex);
    QSqlDatabase db = getDatabase();
    if (!db.isOpen()) return false;

    QSqlQuery query(db);

    if (identity.provider == "youtube" && !identity.mediaId.isEmpty()) {
        query.prepare("SELECT 1 FROM downloads WHERE provider = ? AND media_id = ? LIMIT 1");
        query.addBindValue("youtube");
        query.addBindValue(identity.mediaId);
        if (query.exec() && query.next()) return true;
    }

    query.prepare("SELECT 1 FROM downloads WHERE normalized_url = ? OR url = ? LIMIT 1");
    query.addBindValue(identity.normalizedUrl);
    query.addBindValue(url);

    if (query.exec() && query.next()) return true;

    return false;
}

ArchiveManager::UrlIdentity ArchiveManager::buildIdentity(const QString &urlStr) const {
    UrlIdentity identity;
    identity.mediaId = extractVideoId(urlStr);
    identity.normalizedUrl = normalizeUrl(urlStr);

    if (!identity.mediaId.isEmpty()) {
        identity.provider = "youtube";
    } else {
        identity.provider = "generic";
    }

    return identity;
}

QString ArchiveManager::extractVideoId(const QString &urlStr) const {
    QUrl url(urlStr);
    QString host = url.host().toLower();

    if (host.contains("youtube.com")) {
        QUrlQuery query(url);
        QString v = query.queryItemValue("v");
        if (!v.isEmpty()) {
            QRegularExpression re("^[0-9A-Za-z_-]{11}$");
            if (re.match(v).hasMatch()) return v;
        }

        QRegularExpression re("/(?:shorts|live|embed)/([0-9A-Za-z_-]{11})(?:[/?#]|$)");
        QRegularExpressionMatch match = re.match(url.path());
        if (match.hasMatch()) return match.captured(1);
    }

    if (host.contains("youtu.be")) {
        QString path = url.path();
        if (path.startsWith("/")) path = path.mid(1);
        QStringList parts = path.split("/");
        if (!parts.isEmpty()) {
            QString seg = parts.first();
            QRegularExpression re("^[0-9A-Za-z_-]{11}$");
            if (re.match(seg).hasMatch()) return seg;
        }
    }

    // Fallback patterns
    QRegularExpression p1("(?:v=|/)([0-9A-Za-z_-]{11}).*");
    QRegularExpressionMatch m1 = p1.match(urlStr);
    if (m1.hasMatch()) return m1.captured(1);

    QRegularExpression p2("youtu\\.be/([0-9A-Za-z_-]{11})");
    QRegularExpressionMatch m2 = p2.match(urlStr);
    if (m2.hasMatch()) return m2.captured(1);

    return QString();
}

QString ArchiveManager::normalizeUrl(const QString &urlStr) const {
    QUrl url(urlStr);
    if (!url.isValid()) return QString();

    QString host = url.host().toLower();
    if (host.isEmpty()) return QString();

    QString path = url.path();
    // Remove duplicate slashes and trailing slash
    static QRegularExpression duplicateSlashes("/+");
    path.replace(duplicateSlashes, "/");
    if (path.endsWith("/") && path.length() > 1) path.chop(1);

    QUrlQuery query(url);
    QList<QPair<QString, QString>> queryItems = query.queryItems(QUrl::FullyDecoded);

    // For non-YouTube URLs, strip all query params (they're usually session/tracking junk)
    // For YouTube URLs, only strip known tracking params
    QStringList keptParams;
    if (!host.contains("youtube") && !host.contains("youtu.be")) {
        // Generic URLs: strip all query parameters for archive matching
        QString result = (host + path).toLower();
        return result;
    }

    // Filter YouTube query parameters
    QSet<QString> dropParams = {
        "utm_source", "utm_medium", "utm_campaign", "utm_term", "utm_content",
        "si", "feature", "pp"
    };

    // Sort keys to ensure consistent order
    std::sort(queryItems.begin(), queryItems.end(), [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
        return a.first < b.first;
    });

    for (const auto &item : queryItems) {
        if (dropParams.contains(item.first.toLower())) continue;
        keptParams.append(item.first + "=" + item.second);
    }

    QString queryString;
    if (!keptParams.isEmpty()) {
        queryString = "?" + keptParams.join("&");
    }

    return (host + path + queryString).toLower();
}