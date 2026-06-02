#include "TestArchiveManager.h"
#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <QDir>

void TestArchiveManager::init() {
    BaseTest::init();
    // Use a physical temp file because SQLite :memory: databases are thread-specific
    // and cannot be easily shared across threads for concurrency testing.
    m_dbPath = QDir(getTempDir()).filePath(QStringLiteral("test_archive.db"));
    m_archiveManager = new ArchiveManager(getConfigManager(), m_dbPath, false, this);
}

void TestArchiveManager::cleanup() {
    if (m_archiveManager) {
        delete m_archiveManager;
        m_archiveManager = nullptr;
    }
    BaseTest::cleanup();
}

void TestArchiveManager::testUrlNormalization() {
    // Add a standard YouTube URL
    m_archiveManager->addToArchive(QStringLiteral("https://www.youtube.com/watch?v=dQw4w9WgXcQ"));
    
    // Check alternate formats that should automatically resolve to the same video ID
    QVERIFY(m_archiveManager->isInArchive(QStringLiteral("https://youtu.be/dQw4w9WgXcQ")));
    QVERIFY(m_archiveManager->isInArchive(QStringLiteral("https://www.youtube.com/embed/dQw4w9WgXcQ")));
    QVERIFY(m_archiveManager->isInArchive(QStringLiteral("https://music.youtube.com/watch?v=dQw4w9WgXcQ")));
    
    // Add a non-YouTube URL with trailing query parameters
    m_archiveManager->addToArchive(QStringLiteral("https://vimeo.com/123456?autoplay=1"));
    
    // Ensure query parameters are aggressively stripped when checking the archive
    QVERIFY(m_archiveManager->isInArchive(QStringLiteral("https://vimeo.com/123456")));
}

void TestArchiveManager::testConcurrentAccess() {
    // Test that multiple threads can safely interact with the database
    const int numThreads = 10;
    QList<QFuture<void>> futures;
    
    for (int i = 0; i < numThreads; ++i) {
        futures.append(QtConcurrent::run([this, i]() {
            QString url = QStringLiteral("https://example.com/video/%1").arg(i);
            m_archiveManager->addToArchive(url);
            QVERIFY(m_archiveManager->isInArchive(url));
        }));
    }
    
    for (QFuture<void> &future : futures) {
        future.waitForFinished();
    }
    
    // Verify all items were successfully committed and are readable
    for (int i = 0; i < numThreads; ++i) {
        QString url = QStringLiteral("https://example.com/video/%1").arg(i);
        QVERIFY(m_archiveManager->isInArchive(url));
    }
}

QTEST_GUILESS_MAIN(TestArchiveManager)