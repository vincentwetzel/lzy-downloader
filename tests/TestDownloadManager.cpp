#include "TestDownloadManager.h"
#include <QSignalSpy>
#include <QUuid>

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

void TestDownloadManager::testNonInteractiveDuplicateUsesFailureSignal() {
    TestableDownloadManager manager(getConfigManager(), this);

    QSignalSpy duplicateSpy(&manager, SIGNAL(duplicateDownloadDetected(QString,QString)));
    QSignalSpy failureSpy(&manager, SIGNAL(nonInteractiveRequestFailed(QString,QString,QString)));

    const QString url = QStringLiteral("https://media.example/noninteractive-duplicate-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QVariantMap options;
    options.insert(QStringLiteral("type"), QStringLiteral("video"));
    options.insert(QStringLiteral("non_interactive"), true);
    options.insert(QStringLiteral("id"), QStringLiteral("discord-job-id"));

    manager.enqueueDownload(url, options);
    manager.enqueueDownload(url, options);

    QCOMPARE(duplicateSpy.count(), 0);
    QCOMPARE(failureSpy.count(), 1);
    QCOMPARE(failureSpy.at(0).at(0).toString(), QStringLiteral("discord-job-id"));
    QCOMPARE(failureSpy.at(0).at(1).toString(), url);
}

QTEST_MAIN(TestDownloadManager)
