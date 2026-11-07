#include "TestPlaylistRetry.h"
#include "TestDownloadManager.h"

#include "core/ConfigManager.h"
#include "core/DownloadItem.h"
#include "core/DownloadQueueManager.h"
#include "core/ProcessUtils.h"
#include "core/PlaylistExpansionWorker.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonObject>

namespace {
QString fakeYtDlpPath()
{
    QString name = QStringLiteral("LzyTestFakeYtDlp");
#ifdef Q_OS_WIN
    name += QStringLiteral(".exe");
#endif
    return QDir(QCoreApplication::applicationDirPath()).filePath(name);
}

void configureFakeYtDlp(ConfigManager *config)
{
    config->set(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_path"), fakeYtDlpPath());
    config->set(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_auto_detected"), false);
    config->set(QStringLiteral("General"), QStringLiteral("cookies_from_browser"), QStringLiteral("Firefox"));
    ProcessUtils::clearCache();
}

QString authenticatedPlaylistUrl()
{
    return QStringLiteral("https://music.youtube.com/playlist?list=authenticated-playlist-retry");
}
}

void TestPlaylistRetry::init()
{
    BaseTest::init();
    QStandardPaths::setTestModeEnabled(true);
    QFile::remove(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                      .filePath(QStringLiteral("downloads_backup.json")));
    ProcessUtils::clearCache();
}

void TestPlaylistRetry::cleanup()
{
    ProcessUtils::clearCache();
    QFile::remove(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                      .filePath(QStringLiteral("downloads_backup.json")));
    BaseTest::cleanup();
}

void TestPlaylistRetry::testRetryingFailedPlaylistPlaceholderReexpandsTracks()
{
    ConfigManager *config = getConfigManager();
    QVERIFY(QFileInfo::exists(fakeYtDlpPath()));
    configureFakeYtDlp(config);

    DownloadManager manager(config, this);
    QCoreApplication::processEvents();
    const QString id = QStringLiteral("failed-playlist-placeholder");
    const QString url = authenticatedPlaylistUrl();
    const QVariantMap failedOptions = {
        {QStringLiteral("type"), QStringLiteral("audio")},
        {QStringLiteral("playlist_logic"), QStringLiteral("Ask")},
        {QStringLiteral("is_stopped"), true},
        {QStringLiteral("is_failed"), true}
    };
    manager.m_queueManager->processResumeDownloadsSelection(QJsonArray{QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("url"), url},
        {QStringLiteral("options"), QJsonObject::fromVariantMap(failedOptions)},
        {QStringLiteral("status"), QStringLiteral("stopped")}
    }});

    QSignalSpy detectedSpy(&manager, &DownloadManager::playlistActionRequested);
    QSignalSpy addedSpy(&manager, &DownloadManager::downloadAddedToQueue);
    manager.retryDownload({
        {QStringLiteral("id"), id},
        {QStringLiteral("url"), url},
        {QStringLiteral("playlistIndex"), -1},
        {QStringLiteral("options"), failedOptions}
    });

    QTRY_COMPARE_WITH_TIMEOUT(detectedSpy.count(), 1, 10000);
    const QList<QVariant> prompt = detectedSpy.first();
    QCOMPARE(prompt.at(0).toString(), url);
    QCOMPARE(prompt.at(1).toInt(), 3);

    const QVariantMap options = prompt.at(2).toMap();
    const auto items = qvariant_cast<QList<QVariantMap>>(prompt.at(3));
    QCOMPARE(items.size(), 3);
    manager.processPlaylistSelection(url, QStringLiteral("Download All"), options, items);

    QTRY_COMPARE_WITH_TIMEOUT(manager.m_queueManager->queuedDownloadsCount(), 3, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(addedSpy.count(), 3, 2000);
    for (const QList<QVariant> &arguments : addedSpy) {
        const QVariantMap uiData = arguments.first().toMap();
        QVERIFY(uiData.value(QStringLiteral("playlistIndex")).toInt() > 0);
        QVERIFY(!uiData.value(QStringLiteral("options")).toMap()
                     .value(QStringLiteral("playlist_expansion_pending")).toBool());
    }
    manager.shutdown();
    ProcessUtils::clearCache();
}

void TestPlaylistRetry::testPlaylistRowsAreEnqueuedInBoundedBatches()
{
    ConfigManager *config = getConfigManager();
    TestableDownloadManager manager(config, this);
    QSignalSpy addedSpy(&manager, &DownloadManager::downloadAddedToQueue);

    // Occupy the manager's worker slots so this test measures queue insertion
    // without starting external download processes.
    for (int index = 0; index < manager.m_maxConcurrentDownloads; ++index) {
        manager.m_activeWorkers.insert(QStringLiteral("test-slot-%1").arg(index), nullptr);
    }

    QList<QVariantMap> items;
    for (int index = 0; index < 17; ++index) {
        items.append({
            {QStringLiteral("url"), QStringLiteral("https://media.example/item/%1").arg(index + 1)},
            {QStringLiteral("title"), QStringLiteral("Item %1").arg(index + 1)},
            {QStringLiteral("playlist_index"), index + 1}
        });
    }

    const QString playlistUrl = QStringLiteral("https://media.example/playlist?list=batch-test");
    manager.callOnPlaylistExpanded(playlistUrl, items, QString());

    QCOMPARE(addedSpy.count(), 0);
    QCOMPARE(manager.m_pendingPlaylistEnqueues.size(), 1);
    QVERIFY(QMetaObject::invokeMethod(&manager,
                                      "processNextPlaylistEnqueueBatch",
                                      Qt::DirectConnection));

    QCOMPARE(addedSpy.count(), 8);
    QCOMPARE(manager.m_pendingPlaylistEnqueues.head().nextItemIndex, 8);
    QTRY_COMPARE_WITH_TIMEOUT(addedSpy.count(), items.size(), 2000);
    QVERIFY(manager.m_pendingPlaylistEnqueues.isEmpty());

    manager.m_activeWorkers.clear();
    manager.shutdown();
}

void TestPlaylistRetry::testRestoredPendingPlaylistPlaceholderIsReprobed()
{
    ConfigManager *config = getConfigManager();
    QVERIFY(QFileInfo::exists(fakeYtDlpPath()));
    configureFakeYtDlp(config);

    DownloadManager manager(config, this);
    QCoreApplication::processEvents();
    const QString id = QStringLiteral("restored-pending-playlist");
    const QString url = authenticatedPlaylistUrl();
    const QVariantMap pendingOptions = {
        {QStringLiteral("type"), QStringLiteral("audio")},
        {QStringLiteral("playlist_logic"), QStringLiteral("Ask")},
        {QStringLiteral("playlist_expansion_pending"), true}
    };

    QSignalSpy detectedSpy(&manager, &DownloadManager::playlistActionRequested);
    manager.m_queueManager->processResumeDownloadsSelection(QJsonArray{QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("url"), url},
        {QStringLiteral("options"), QJsonObject::fromVariantMap(pendingOptions)},
        {QStringLiteral("status"), QStringLiteral("queued")}
    }});
    QTRY_COMPARE_WITH_TIMEOUT(detectedSpy.count(), 1, 10000);

    QCOMPARE(detectedSpy.first().at(0).toString(), url);
    QCOMPARE(detectedSpy.first().at(1).toInt(), 3);
    QVERIFY(manager.m_queueManager->m_pendingExpansions.contains(id));
    manager.shutdown();
    ProcessUtils::clearCache();
}

void TestPlaylistRetry::testPlaylistLogicModesControlProbeAndExpansion()
{
    ConfigManager *config = getConfigManager();
    QVERIFY(QFileInfo::exists(fakeYtDlpPath()));
    config->set(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_path"), fakeYtDlpPath());
    config->set(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_auto_detected"), false);
    ProcessUtils::clearCache();

    const QString playlistUrl = QStringLiteral("https://music.youtube.com/playlist?list=playlist-policy-probe");
    {
        PlaylistExpansionWorker worker(playlistUrl, config, this);
        worker.setProperty("options", QVariantMap{{QStringLiteral("type"), QStringLiteral("audio")}});
        QSignalSpy detectedSpy(&worker, &PlaylistExpansionWorker::playlistDetected);
        worker.startExpansion(QStringLiteral("Ask"));
        QTRY_COMPARE_WITH_TIMEOUT(detectedSpy.count(), 1, 10000);
        QCOMPARE(detectedSpy.first().at(1).toInt(), 3);
        QCOMPARE(qvariant_cast<QList<QVariantMap>>(detectedSpy.first().at(3)).size(), 3);
    }
    {
        PlaylistExpansionWorker worker(playlistUrl, config, this);
        worker.setProperty("options", QVariantMap{{QStringLiteral("type"), QStringLiteral("audio")}});
        QSignalSpy expandedSpy(&worker, &PlaylistExpansionWorker::expansionFinished);
        worker.startExpansion(QStringLiteral("Download All (no prompt)"));
        QTRY_COMPARE_WITH_TIMEOUT(expandedSpy.count(), 1, 10000);
        QVERIFY2(expandedSpy.first().at(2).toString().isEmpty(), qPrintable(expandedSpy.first().at(2).toString()));
        const auto items = qvariant_cast<QList<QVariantMap>>(expandedSpy.first().at(1));
        QCOMPARE(items.size(), 3);
        QCOMPARE(items.at(2).value(QStringLiteral("playlist_index")).toInt(), 3);
    }
    {
        PlaylistExpansionWorker worker(playlistUrl, config, this);
        worker.setProperty("options", QVariantMap{{QStringLiteral("type"), QStringLiteral("audio")}});
        QSignalSpy expandedSpy(&worker, &PlaylistExpansionWorker::expansionFinished);
        worker.startExpansion(QStringLiteral("Download Single (ignore playlist)"));
        QTRY_COMPARE_WITH_TIMEOUT(expandedSpy.count(), 1, 10000);
        QVERIFY2(expandedSpy.first().at(2).toString().isEmpty(), qPrintable(expandedSpy.first().at(2).toString()));
        const auto items = qvariant_cast<QList<QVariantMap>>(expandedSpy.first().at(1));
        QCOMPARE(items.size(), 3);
        QCOMPARE(items.first().value(QStringLiteral("url")).toString(),
                 QStringLiteral("https://media.example/watch?id=policy-1"));
        QVERIFY(items.first().value(QStringLiteral("is_playlist")).toBool());
    }
    ProcessUtils::clearCache();
}

void TestPlaylistRetry::testTransientPlaylistProbeFallback() {
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

void TestPlaylistRetry::testSearchProbeFailureDoesNotFallBackToSingleWorker() {
    TestableDownloadManager manager(getConfigManager(), this);

    QSignalSpy expansionSpy(&manager, SIGNAL(playlistExpansionFinished(QString,int)));
    const QString searchUrl = QStringLiteral("https://music.youtube.com/search?q=shout+to+the+lord");

    manager.callOnPlaylistExpanded(searchUrl, {},
                                   QStringLiteral("Playlist expansion timed out after 45 seconds."));

    QVERIFY(!expansionSpy.isEmpty());
    QCOMPARE(expansionSpy.last().at(0).toString(), searchUrl);
    QCOMPARE(expansionSpy.last().at(1).toInt(), 0);
}

void TestPlaylistRetry::testSearchProbeUsesFlatPlaylistAndReturnsEntries() {
    ConfigManager *configManager = getConfigManager();
    const QString fakeYtDlpName = QStringLiteral("LzyTestFakeYtDlp")
#ifdef Q_OS_WIN
        + QStringLiteral(".exe")
#endif
        ;
    const QString fakeYtDlpPath = QDir(QCoreApplication::applicationDirPath()).filePath(fakeYtDlpName);
    QVERIFY2(QFileInfo::exists(fakeYtDlpPath), qPrintable(QStringLiteral("Fake yt-dlp not found: %1").arg(fakeYtDlpPath)));
    configManager->set(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_path"), fakeYtDlpPath);
    configManager->set(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_auto_detected"), false);
    ProcessUtils::clearCache();

    PlaylistExpansionWorker worker(QStringLiteral("https://media.example/search?q=search-probe"), configManager, this);
    worker.setProperty("options", QVariantMap{{QStringLiteral("type"), QStringLiteral("video")}});
    QSignalSpy expansionSpy(&worker, &PlaylistExpansionWorker::expansionFinished);

    worker.startExpansion(QStringLiteral("Download All (no prompt)"));

    QTRY_COMPARE_WITH_TIMEOUT(expansionSpy.count(), 1, 10000);
    const QList<QVariant> result = expansionSpy.first();
    QVERIFY2(result.at(2).toString().isEmpty(), qPrintable(result.at(2).toString()));
    const QList<QVariantMap> items = qvariant_cast<QList<QVariantMap>>(result.at(1));
    QCOMPARE(items.size(), 3);
    QCOMPARE(items.at(0).value(QStringLiteral("url")).toString(), QStringLiteral("https://media.example/watch?id=search-1"));
    QCOMPARE(items.at(2).value(QStringLiteral("playlist_index")).toInt(), 3);
    ProcessUtils::clearCache();
}

void TestPlaylistRetry::testWatchUrlWithPlaylistQueryIsExpanded() {
    ConfigManager *configManager = getConfigManager();
    const QString fakeYtDlpName = QStringLiteral("LzyTestFakeYtDlp")
#ifdef Q_OS_WIN
        + QStringLiteral(".exe")
#endif
        ;
    const QString fakeYtDlpPath = QDir(QCoreApplication::applicationDirPath()).filePath(fakeYtDlpName);
    QVERIFY2(QFileInfo::exists(fakeYtDlpPath), qPrintable(QStringLiteral("Fake yt-dlp not found: %1").arg(fakeYtDlpPath)));
    configManager->set(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_path"), fakeYtDlpPath);
    configManager->set(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_auto_detected"), false);
    ProcessUtils::clearCache();

    PlaylistExpansionWorker worker(QStringLiteral("https://youtube.com/watch?v=j0dr1SnyR9c&list=playlist-probe&index=3"), configManager, this);
    worker.setProperty("options", QVariantMap{{QStringLiteral("type"), QStringLiteral("video")}});
    QSignalSpy playlistSpy(&worker, &PlaylistExpansionWorker::playlistDetected);

    worker.startExpansion(QStringLiteral("Ask"));

    QTRY_COMPARE_WITH_TIMEOUT(playlistSpy.count(), 1, 10000);
    const QList<QVariant> result = playlistSpy.first();
    QCOMPARE(result.at(0).toString(), QStringLiteral("https://youtube.com/watch?v=j0dr1SnyR9c&list=playlist-probe&index=3"));
    QCOMPARE(result.at(1).toInt(), 1);
    const QList<QVariantMap> items = qvariant_cast<QList<QVariantMap>>(result.at(3));
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().value(QStringLiteral("url")).toString(), QStringLiteral("https://media.example/watch?id=playlist-3"));
    QCOMPARE(items.first().value(QStringLiteral("playlist_index")).toInt(), 3);
    ProcessUtils::clearCache();
}

void TestPlaylistRetry::testExplicitPlaylistFailureClassification() {
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

void TestPlaylistRetry::testEnqueueUsesPersistedPlaylistLogic() {
    ConfigManager *config = getConfigManager();
    config->set(QStringLiteral("General"), QStringLiteral("playlist_logic"),
                QStringLiteral("Download All (no prompt)"));

    TestableDownloadManager manager(config, this);
    QSignalSpy addedSpy(&manager, &DownloadManager::downloadAddedToQueue);

    QVariantMap options;
    options.insert(QStringLiteral("type"), QStringLiteral("video"));
    const QString url = QStringLiteral("https://media.example.test/watch?id=persisted-setting");
    manager.enqueueDownload(url, options);

    QVERIFY(!addedSpy.isEmpty());
    const QVariantMap queuedItem = addedSpy.first().at(0).toMap();
    QCOMPARE(queuedItem.value(QStringLiteral("options")).toMap()
                 .value(QStringLiteral("playlist_logic")).toString(),
             QStringLiteral("Download All (no prompt)"));
}

void TestPlaylistRetry::testSinglePlaylistSettingQueuesOnlyFirstItem() {
    TestableDownloadManager manager(getConfigManager(), this);
    QSignalSpy addedSpy(&manager, &DownloadManager::downloadAddedToQueue);
    for (int slot = 0; slot < manager.m_maxConcurrentDownloads; ++slot) {
        manager.m_activeWorkers.insert(QStringLiteral("test-slot-%1").arg(slot), nullptr);
    }

    QVariantMap options;
    options.insert(QStringLiteral("type"), QStringLiteral("video"));
    options.insert(QStringLiteral("playlist_logic"),
                   QStringLiteral("Download Single (ignore playlist)"));

    const QVariantMap firstItem = {
        {QStringLiteral("url"), QStringLiteral("https://media.example.test/watch?id=first")},
        {QStringLiteral("is_playlist"), true},
        {QStringLiteral("playlist_index"), 1}
    };
    const QVariantMap secondItem = {
        {QStringLiteral("url"), QStringLiteral("https://media.example.test/watch?id=second")},
        {QStringLiteral("is_playlist"), true},
        {QStringLiteral("playlist_index"), 2}
    };

    manager.emitPlaylistExpansion(QStringLiteral("https://media.example.test/playlist?id=single"),
                                   options, {firstItem, secondItem});

    QTRY_COMPARE_WITH_TIMEOUT(addedSpy.count(), 1, 2000);
    const QVariantMap queuedItem = addedSpy.first().at(0).toMap();
    QCOMPARE(queuedItem.value(QStringLiteral("url")).toString(),
             QStringLiteral("https://media.example.test/watch?id=first"));
    manager.m_activeWorkers.clear();
}

void TestPlaylistRetry::testDownloadAllCreatesOneQueueItemPerExpandedItem() {
    TestableDownloadManager manager(getConfigManager(), this);
    QSignalSpy addedSpy(&manager, &DownloadManager::downloadAddedToQueue);
    for (int slot = 0; slot < manager.m_maxConcurrentDownloads; ++slot) {
        manager.m_activeWorkers.insert(QStringLiteral("test-slot-%1").arg(slot), nullptr);
    }

    QVariantMap options;
    options.insert(QStringLiteral("type"), QStringLiteral("video"));
    options.insert(QStringLiteral("playlist_logic"), QStringLiteral("Download All (no prompt)"));
    const QList<QVariantMap> expandedItems = {
        {{QStringLiteral("url"), QStringLiteral("https://media.example/watch?id=item-1")},
         {QStringLiteral("title"), QStringLiteral("Item 1")},
         {QStringLiteral("is_playlist"), true},
         {QStringLiteral("playlist_index"), 1}},
        {{QStringLiteral("url"), QStringLiteral("https://media.example/watch?id=item-2")},
         {QStringLiteral("title"), QStringLiteral("Item 2")},
         {QStringLiteral("is_playlist"), true},
         {QStringLiteral("playlist_index"), 2}},
        {{QStringLiteral("url"), QStringLiteral("https://media.example/watch?id=item-3")},
         {QStringLiteral("title"), QStringLiteral("Item 3")},
         {QStringLiteral("is_playlist"), true},
         {QStringLiteral("playlist_index"), 3}}
    };

    manager.emitPlaylistExpansion(QStringLiteral("https://media.example/playlist?id=batch"),
                                   options, expandedItems);

    QTRY_COMPARE_WITH_TIMEOUT(addedSpy.count(), 3, 2000);
    QCOMPARE(addedSpy.at(0).at(0).toMap().value(QStringLiteral("url")).toString(),
             QStringLiteral("https://media.example/watch?id=item-1"));
    QCOMPARE(addedSpy.at(2).at(0).toMap().value(QStringLiteral("url")).toString(),
             QStringLiteral("https://media.example/watch?id=item-3"));
    manager.m_activeWorkers.clear();
}

QTEST_MAIN(TestPlaylistRetry)
