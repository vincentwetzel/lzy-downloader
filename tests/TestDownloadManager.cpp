#include "TestDownloadManager.h"
#include "core/ProcessUtils.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QUuid>

void TestDownloadManager::init() {
    BaseTest::init();
    ProcessUtils::clearCache();
    // We instantiate the base class for general testing, but we'll use a local TestableDownloadManager when needed
}

void TestDownloadManager::cleanup() {
    ProcessUtils::clearCache();
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

void TestDownloadManager::testSlowProbeFallbackAndExplicitPlaylistSmoke() {
    ConfigManager *configManager = getConfigManager();
    const QString fakeYtDlpName = QStringLiteral("LzyTestFakeYtDlp")
#ifdef Q_OS_WIN
        + QStringLiteral(".exe")
#endif
        ;
    const QString fakeYtDlpPath = QDir(QCoreApplication::applicationDirPath()).filePath(fakeYtDlpName);
    QVERIFY2(QFileInfo::exists(fakeYtDlpPath), qPrintable(QStringLiteral("Fake yt-dlp not found: %1").arg(fakeYtDlpPath)));

    const QString destination = QDir(getTempDir()).filePath(QStringLiteral("completed"));
    configManager->set(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory"), destination);
    configManager->set(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_path"), fakeYtDlpPath);
    configManager->set(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_auto_detected"), false);
    configManager->set(QStringLiteral("Metadata"), QStringLiteral("use_aria2c"), false);
    configManager->set(QStringLiteral("Metadata"), QStringLiteral("embed_metadata"), false);
    configManager->set(QStringLiteral("Metadata"), QStringLiteral("embed_thumbnail"), false);
    configManager->set(QStringLiteral("Metadata"), QStringLiteral("embed_chapters"), false);
    configManager->set(QStringLiteral("General"), QStringLiteral("output_template"), QStringLiteral("%(id)s.%(ext)s"));
    configManager->set(QStringLiteral("General"), QStringLiteral("output_template_video"), QString());
    configManager->set(QStringLiteral("General"), QStringLiteral("output_template_audio"), QString());
    configManager->save();
    ProcessUtils::clearCache();

    TestableDownloadManager manager(configManager, this);
    QSignalSpy addedSpy(&manager, &DownloadManager::downloadAddedToQueue);
    QSignalSpy startedSpy(&manager, &DownloadManager::downloadStarted);
    QSignalSpy finishedSpy(&manager, &DownloadManager::downloadFinished);

    const QString ordinaryUrl = QStringLiteral("https://media.example/watch?id=slow-probe");
    const QString explicitPlaylistUrl = QStringLiteral("https://media.example/playlist?list=slow-probe");
    QVariantMap options;
    options.insert(QStringLiteral("type"), QStringLiteral("video"));
    options.insert(QStringLiteral("format"), QStringLiteral("best[ext=webm]"));

    QElapsedTimer elapsed;
    elapsed.start();
    manager.enqueueDownload(ordinaryUrl, options);
    manager.enqueueDownload(explicitPlaylistUrl, options);

    QString ordinaryId;
    QString explicitPlaylistId;
    for (const QList<QVariant> &arguments : addedSpy) {
        const QVariantMap item = arguments.at(0).toMap();
        if (item.value(QStringLiteral("url")).toString() == ordinaryUrl) {
            ordinaryId = item.value(QStringLiteral("id")).toString();
        } else if (item.value(QStringLiteral("url")).toString() == explicitPlaylistUrl) {
            explicitPlaylistId = item.value(QStringLiteral("id")).toString();
        }
    }
    QVERIFY(!ordinaryId.isEmpty());
    QVERIFY(!explicitPlaylistId.isEmpty());

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 60000);
    QVERIFY2(elapsed.elapsed() >= 40000, "Playlist probes completed before the production watchdog window.");

    bool ordinarySucceeded = false;
    bool explicitPlaylistFailed = false;
    for (const QList<QVariant> &arguments : finishedSpy) {
        const QString id = arguments.at(0).toString();
        const bool success = arguments.at(1).toBool();
        if (id == ordinaryId) {
            ordinarySucceeded = success;
        } else if (id == explicitPlaylistId) {
            explicitPlaylistFailed = !success;
            QVERIFY(arguments.at(2).toString().contains(QStringLiteral("Playlist expansion failed")));
        }
    }
    QVERIFY(ordinarySucceeded);
    QVERIFY(explicitPlaylistFailed);
    bool ordinaryStarted = false;
    bool explicitPlaylistStarted = false;
    for (const QList<QVariant> &arguments : startedSpy) {
        const QString id = arguments.at(0).toString();
        ordinaryStarted = ordinaryStarted || id == ordinaryId;
        explicitPlaylistStarted = explicitPlaylistStarted || id == explicitPlaylistId;
    }
    QVERIFY(ordinaryStarted);
    QVERIFY(!explicitPlaylistStarted);
    QVERIFY(QFileInfo::exists(QDir(destination).filePath(QStringLiteral("ordinary-fallback.webm"))));

    ProcessUtils::clearCache();
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
