#include "ArchiveManager.h"

// Qt headers
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>

// C++ standard library
#include <algorithm>
#include <array>

ArchiveManager::ArchiveManager(ConfigManager *configManager, QObject *parent)
    : ArchiveManager(configManager, QDir(configManager->getConfigDir()).filePath(QStringLiteral("download_archive.db")), false, parent) {
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
    closeCurrentThreadDatabase();
}

QString ArchiveManager::getArchiveDbPath() const {
    return m_dbPath;
}

QSqlDatabase ArchiveManager::getDatabase() {
    const QString connectionName = currentThreadConnectionName();
    QSqlDatabase db = QSqlDatabase::database(connectionName, false);
    if (!db.isValid()) {
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(m_dbPath);
    }

    if (!db.isOpen()) {
        if (!db.open()) {
            qCritical() << "Failed to open archive database:" << db.lastError().text();
        } else {
            QSqlQuery pragmaQuery(db);
            pragmaQuery.exec(QStringLiteral("PRAGMA journal_mode=WAL;"));
            pragmaQuery.exec(QStringLiteral("PRAGMA synchronous=NORMAL;"));
            pragmaQuery.exec(QStringLiteral("PRAGMA busy_timeout=5000;"));
        }
    }
    return db;
}

QString ArchiveManager::currentThreadConnectionName() const {
    return u"archive_connection_" + QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
}

void ArchiveManager::closeCurrentThreadDatabase() {
    const QString connectionName = currentThreadConnectionName();
    {
        QMutexLocker locker(&m_mutex);
        if (!QSqlDatabase::contains(connectionName)) {
            return;
        }
    }

    {
        QSqlDatabase db = QSqlDatabase::database(connectionName, false);
        if (db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
}

void ArchiveManager::closeDatabase() {
    closeCurrentThreadDatabase();
    qDebug() << "Archive database connection closed for current thread.";
}

void ArchiveManager::ensureSchema() {
    QMutexLocker locker(&m_mutex);
    QSqlDatabase db = getDatabase();
    if (!db.isOpen()) return;

    QSqlQuery query(db);

    // Create table if not exists
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS downloads ("
                    "url TEXT PRIMARY KEY, "
                    "normalized_url TEXT, "
                    "provider TEXT, "
                    "media_id TEXT, "
                    "timestamp REAL)"))) {
        qCritical() << "Failed to create downloads table:" << query.lastError().text();
        return;
    }

    // Check for missing columns and add them if necessary
    QSqlRecord record = db.record(QStringLiteral("downloads"));
    if (!record.contains(QStringLiteral("normalized_url"))) {
        query.exec(QStringLiteral("ALTER TABLE downloads ADD COLUMN normalized_url TEXT"));
    }
    if (!record.contains(QStringLiteral("provider"))) {
        query.exec(QStringLiteral("ALTER TABLE downloads ADD COLUMN provider TEXT"));
    }
    if (!record.contains(QStringLiteral("media_id"))) {
        query.exec(QStringLiteral("ALTER TABLE downloads ADD COLUMN media_id TEXT"));
    }
    if (!record.contains(QStringLiteral("timestamp"))) {
        query.exec(QStringLiteral("ALTER TABLE downloads ADD COLUMN timestamp REAL"));
    }

    backfillIdentityColumns();

    // Create indices
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_downloads_norm ON downloads(normalized_url)"));
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_downloads_provider_media ON downloads(provider, media_id)"));
}

void ArchiveManager::backfillIdentityColumns() {
    QSqlDatabase db = getDatabase();
    if (!db.isOpen()) return;

    QSqlQuery query(db);
    query.exec(QStringLiteral("SELECT url, normalized_url, provider, media_id FROM downloads "
               "WHERE normalized_url IS NULL OR normalized_url = '' "
               "OR provider IS NULL OR provider = '' "
               "OR (provider = 'youtube' AND (media_id IS NULL OR media_id = ''))"));

    db.transaction();
    QSqlQuery updateQuery(db);
    updateQuery.prepare(QStringLiteral("UPDATE downloads SET normalized_url = ?, provider = ?, media_id = ? WHERE url = ?"));

    while (query.next()) {
        QString url = query.value(0).toString();
        UrlIdentity identity = buildIdentity(url);

        updateQuery.bindValue(0, identity.normalizedUrl);
        updateQuery.bindValue(1, identity.provider);
        updateQuery.bindValue(2, identity.mediaId);
        updateQuery.bindValue(3, url);
        updateQuery.exec();
    }
    db.commit();
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
    query.prepare(QStringLiteral("INSERT INTO downloads(url, normalized_url, provider, media_id, timestamp) "
                  "VALUES (?, ?, ?, ?, ?) "
                  "ON CONFLICT(url) DO UPDATE SET "
                  "normalized_url = excluded.normalized_url, "
                  "provider = excluded.provider, "
                  "media_id = excluded.media_id, "
                  "timestamp = excluded.timestamp"));

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

    if (identity.provider == QStringLiteral("youtube") && !identity.mediaId.isEmpty()) {
        query.prepare(QStringLiteral("SELECT 1 FROM downloads WHERE provider = ? AND media_id = ? LIMIT 1"));
        query.addBindValue(QStringLiteral("youtube"));
        query.addBindValue(identity.mediaId);
        if (query.exec() && query.next()) return true;
    }

    query.prepare(QStringLiteral("SELECT 1 FROM downloads WHERE normalized_url = ? OR url = ? LIMIT 1"));
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
        identity.provider = QStringLiteral("youtube");
    } else {
        identity.provider = QStringLiteral("generic");
    }

    return identity;
}

QString ArchiveManager::extractVideoId(const QString &urlStr) const {
    QUrl url(urlStr);
    QString host = url.host().toLower();

    static const QRegularExpression idRe(QStringLiteral("^[0-9A-Za-z_-]{11}$"));

    if (host.contains(u"youtube.com")) {
        QUrlQuery query(url);
        QString v = query.queryItemValue(QStringLiteral("v"));
        if (!v.isEmpty()) {
            if (idRe.match(v).hasMatch()) return v;
        }

        static const QRegularExpression re(QStringLiteral(R"(/(?:shorts|live|embed)/([0-9A-Za-z_-]{11})(?:[/?#]|$))"));
        QRegularExpressionMatch match = re.match(url.path());
        if (match.hasMatch()) return match.captured(1);

        // Fallback patterns only for YouTube domains to prevent misclassifying generic URLs
        static const QRegularExpression p1(QStringLiteral(R"((?:v=|/)([0-9A-Za-z_-]{11})(?:[/?#]|$))"));
        const QRegularExpressionMatch m1 = p1.match(urlStr);
        if (m1.hasMatch()) return m1.captured(1);
    } else if (host.contains(u"youtu.be")) {
        const QStringView pathView(url.path());
        const QStringView normalizedPath = pathView.startsWith(u'/') ? pathView.mid(1) : pathView;
        const auto parts = normalizedPath.split(u'/', Qt::SkipEmptyParts);
        if (!parts.isEmpty()) {
            const QStringView seg = parts.first();
            if (idRe.matchView(seg).hasMatch()) return seg.toString();
        }
    }

    return QString();
}

QString ArchiveManager::normalizeUrl(const QString &urlStr) const {
    QUrl url(urlStr);
    if (!url.isValid()) return QString();

    QString host = url.host().toLower();
    if (host.isEmpty()) return QString();

    QString path = url.path();
    // Remove duplicate slashes and trailing slash
    if (path.contains(u"//")) {
        static const QRegularExpression duplicateSlashes(QStringLiteral(R"(/{2,})"));
        path.replace(duplicateSlashes, QStringLiteral("/"));
    }
    if (path.endsWith(u'/') && path.length() > 1) path.chop(1);

    QUrlQuery query(url);
    QList<QPair<QString, QString>> queryItems = query.queryItems(QUrl::FullyDecoded);

    // Sort keys to ensure consistent order
    std::sort(queryItems.begin(), queryItems.end(), [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
        return a.first < b.first;
    });

    QStringList keptParams;
    if (host.contains(u"youtube") || host.contains(u"youtu.be")) {
        static constexpr std::array<QStringView, 8> dropParams = {
            u"utm_source", u"utm_medium", u"utm_campaign", u"utm_term", u"utm_content",
            u"si", u"feature", u"pp"
        };
        for (const auto &item : std::as_const(queryItems)) {
            auto it = std::ranges::find_if(dropParams, [&](QStringView p) {
                return item.first.compare(p, Qt::CaseInsensitive) == 0;
            });
            if (it != dropParams.end()) continue;
            keptParams.append(item.first + u'=' + item.second);
        }
    } else {
        for (const auto &item : std::as_const(queryItems)) {
            keptParams.append(item.first + u'=' + item.second);
        }
    }

    QString queryString;
    if (!keptParams.isEmpty()) {
        queryString = u"?" + keptParams.join(u"&");
    }

    return host + path + queryString;
}
