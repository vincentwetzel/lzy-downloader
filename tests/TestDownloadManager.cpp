#include "TestDownloadManager.h"
#include <QSignalSpy>

void TestDownloadManager::init() {
    BaseTest::init();
    // We instantiate the base class for general testing, but we'll use a local TestableDownloadManager when needed
}

void TestDownloadManager::cleanup() {
    BaseTest::cleanup();
}

void TestDownloadManager::testTransientPlaylistProbeFallback() {
    TestableDownloadManager manager(getConfigManager(), this);
    
    QSignalSpy expansionSpy(&manager, SIGNAL(playlistExpansionFinished(QString,int)));

    const QString fakeSingleUrl = QStringLiteral("https://media.example/watch?id=1234567890");
    QList<QVariantMap> emptyItems;
    QString timeoutError = QStringLiteral("yt-dlp probe timed out after 30 seconds");

    // Call the private slot directly
    manager.callOnPlaylistExpanded(fakeSingleUrl, emptyItems, timeoutError);

    QVERIFY(!expansionSpy.isEmpty());
    QCOMPARE(expansionSpy.last().at(0).toString(), fakeSingleUrl);
    
    // The key expectation: the manager should create a fallback item for the direct URL
    // So itemCount should be 1 (the single fallback item)
    QCOMPARE(expansionSpy.last().at(1).toInt(), 1);
}

void TestDownloadManager::testExplicitPlaylistFailureClassification() {
    TestableDownloadManager manager(getConfigManager(), this);
    
    QSignalSpy expansionSpy(&manager, SIGNAL(playlistExpansionFinished(QString,int)));

    const QString explicitPlaylistUrl = QStringLiteral("https://media.example/playlist?list=collection-123");
    QList<QVariantMap> emptyItems;
    QString timeoutError = QStringLiteral("yt-dlp probe timed out after 30 seconds");

    // Call the private slot directly
    manager.callOnPlaylistExpanded(explicitPlaylistUrl, emptyItems, timeoutError);

    // Should still emit finished, but with 0 items because it's a known explicit playlist 
    // and cannot fall back to a direct download
    QVERIFY(!expansionSpy.isEmpty());
    QCOMPARE(expansionSpy.last().at(0).toString(), explicitPlaylistUrl);
    QCOMPARE(expansionSpy.last().at(1).toInt(), 0);
}

QTEST_MAIN(TestDownloadManager)
