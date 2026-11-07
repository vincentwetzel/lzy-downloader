#include "TestDownloadManager.h"
#include "core/DownloadFinalizer.h"
#include "core/MetadataEmbedder.h"
#include "core/ProcessUtils.h"
#include "core/SortingManager.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUuid>

void TestDownloadManager::init() {
    BaseTest::init();
    ProcessUtils::clearCache();
    // We instantiate the base class for general testing, but we'll use a local TestableDownloadManager when needed
}

void TestDownloadManager::cleanup() {
    ProcessUtils::clearCache();
    // DownloadQueueState intentionally persists active/stopped items across
    // application restarts. Each test creates a fresh manager, so remove the
    // shared test backup after the manager under test has been destroyed to
    // prevent a prior test's item from being restored into the next one.
    QFile::remove(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                      .filePath(QStringLiteral("downloads_backup.json")));
    BaseTest::cleanup();
}

void TestDownloadManager::testQueueIdleUsesManagerState() {
    TestableDownloadManager manager(getConfigManager(), this);

    QVERIFY(manager.isQueueIdle());

    DownloadItem activeItem;
    activeItem.id = QStringLiteral("active-item");
    activeItem.url = QStringLiteral("https://media.example/active");
    manager.m_activeItems.insert(activeItem.id, activeItem);
    QVERIFY(!manager.isQueueIdle());
    manager.m_activeItems.clear();

    DownloadItem queuedItem;
    queuedItem.id = QStringLiteral("queued-item");
    queuedItem.url = QStringLiteral("https://media.example/queued");
    manager.m_queueManager->enqueueDownload(queuedItem, false);
    QVERIFY(!manager.isQueueIdle());
    QVERIFY(manager.m_queueManager->cancelQueuedOrPausedDownload(queuedItem.id));
    QVERIFY(manager.isQueueIdle());

    manager.m_queueManager->m_pendingExpansions.insert(QStringLiteral("expansion"), queuedItem.url);
    QVERIFY(!manager.isQueueIdle());
    manager.m_queueManager->m_pendingExpansions.clear();
    QVERIFY(manager.isQueueIdle());
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

void TestDownloadManager::testDownloadWorkerUsesDedicatedThread()
{
    ConfigManager *configManager = getConfigManager();
    const QString fakeYtDlpName = QStringLiteral("LzyTestFakeYtDlp")
#ifdef Q_OS_WIN
        + QStringLiteral(".exe")
#endif
        ;
    const QString fakeYtDlpPath = QDir(QCoreApplication::applicationDirPath()).filePath(fakeYtDlpName);
    QVERIFY2(QFileInfo::exists(fakeYtDlpPath), qPrintable(QStringLiteral("Fake yt-dlp not found: %1").arg(fakeYtDlpPath)));

    QProcess fakeProbe;
    ProcessUtils::setProcessEnvironment(fakeProbe);
    fakeProbe.start(fakeYtDlpPath, {QStringLiteral("--dump-single-json"), QStringLiteral("--no-download"),
                                    QStringLiteral("https://media.example.test/thread-regression")});
    if (!fakeProbe.waitForStarted(5000)) {
        QSKIP("The test environment cannot launch the fake yt-dlp executable.");
    }
    fakeProbe.kill();
    fakeProbe.waitForFinished(5000);

    configManager->set(QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory"), getTempDir());
    configManager->set(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory"), getTempDir());
    configManager->set(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_path"), fakeYtDlpPath);
    configManager->set(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_auto_detected"), false);
    configManager->set(QStringLiteral("Metadata"), QStringLiteral("use_aria2c"), false);
    configManager->set(QStringLiteral("Metadata"), QStringLiteral("embed_metadata"), false);
    configManager->set(QStringLiteral("Metadata"), QStringLiteral("embed_thumbnail"), false);
    configManager->set(QStringLiteral("General"), QStringLiteral("output_template"), QStringLiteral("%(id)s.%(ext)s"));
    configManager->save();
    ProcessUtils::clearCache();

    TestableDownloadManager manager(configManager, this);
    QSignalSpy addedSpy(&manager, &DownloadManager::downloadAddedToQueue);
    QSignalSpy finishedSpy(&manager, &DownloadManager::downloadFinished);

    QVariantMap options;
    options.insert(QStringLiteral("type"), QStringLiteral("video"));
    const QString url = QStringLiteral("https://media.example.test/thread-regression");
    manager.enqueueDownload(url, options);

    QTRY_VERIFY_WITH_TIMEOUT(!addedSpy.isEmpty(), 5000);
    const QString id = addedSpy.first().at(0).toMap().value(QStringLiteral("id")).toString();
    QVERIFY(!id.isEmpty());

    auto findWorkerThread = [&manager, &id]() -> QThread * {
        const QString expectedName = QStringLiteral("yt-dlp-download-%1").arg(id);
        for (QThread *thread : manager.findChildren<QThread *>()) {
            if (thread->objectName() == expectedName) {
                return thread;
            }
        }
        return nullptr;
    };

    QTRY_VERIFY_WITH_TIMEOUT(findWorkerThread() != nullptr, 5000);
    QThread *workerThread = findWorkerThread();
    QVERIFY(workerThread != QThread::currentThread());
    QVERIFY(workerThread->isRunning());

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 15000);
    QCOMPARE(finishedSpy.first().at(0).toString(), id);
    QVERIFY(finishedSpy.first().at(1).toBool());
    ProcessUtils::clearCache();
}

void TestDownloadManager::testMetadataEmbedderRunsOffGuiThread()
{
    MetadataEmbedder *embedder = new MetadataEmbedder(getConfigManager());
    QThread workerThread;
    QThread *emittingThread = nullptr;
    bool succeeded = false;

    connect(embedder, &MetadataEmbedder::finished, this,
            [&workerThread, &emittingThread, &succeeded](bool success, const QString &) {
        emittingThread = QThread::currentThread();
        succeeded = success;
        workerThread.quit();
    }, Qt::DirectConnection);
    connect(&workerThread, &QThread::finished, embedder, &QObject::deleteLater);

    embedder->moveToThread(&workerThread);
    connect(&workerThread, &QThread::started, embedder, [embedder]() {
        // The opus fast path avoids external FFmpeg while still exercising
        // the embedder's worker-thread affinity and completion signal.
        embedder->processFile(QStringLiteral("missing-test-file.opus"), 1, false);
    }, Qt::QueuedConnection);
    workerThread.start();

    QTRY_VERIFY_WITH_TIMEOUT(emittingThread != nullptr, 5000);
    QVERIFY(workerThread.wait(5000));
    QCOMPARE(emittingThread, &workerThread);
    QVERIFY(succeeded);
}

void TestDownloadManager::testMetadataEmbedderSkipsMissingThumbnailWithoutOtherWork()
{
    MetadataEmbedder *embedder = new MetadataEmbedder(getConfigManager());
    QSignalSpy finishedSpy(embedder, &MetadataEmbedder::finished);

    embedder->processFile(QStringLiteral("missing-test-file.opus"), 0, false);

    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(finishedSpy.first().at(0).toBool());
    delete embedder;
}

void TestDownloadManager::testMetadataEmbedderSkipsUnsupportedOpusThumbnailRemux()
{
    const QString thumbnailPath = QDir(getTempDir()).filePath(QStringLiteral("opus-thumbnail.jpg"));
    QImage thumbnail(8, 8, QImage::Format_RGB32);
    thumbnail.fill(Qt::white);
    QVERIFY(thumbnail.save(thumbnailPath, "JPG"));

    MetadataEmbedder *embedder = new MetadataEmbedder(getConfigManager());
    embedder->setThumbnailPath(thumbnailPath);
    QSignalSpy finishedSpy(embedder, &MetadataEmbedder::finished);

    embedder->processFile(QStringLiteral("missing-test-file.opus"), 0, false);

    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(finishedSpy.first().at(0).toBool());
    delete embedder;
}

void TestDownloadManager::testCompletionSurvivesReentrantItemRemoval()
{
    ConfigManager *configManager = getConfigManager();
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString id = QStringLiteral("66666666-6666-4666-8666-666666666666");
    const QString mediaPath = QDir(tempDir.path()).filePath(QStringLiteral("playlist-item.opus"));
    QFile media(mediaPath);
    QVERIFY(media.open(QIODevice::WriteOnly));
    media.write("synthetic opus");
    media.close();

    configManager->set(QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory"), tempDir.path());
    configManager->set(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory"), tempDir.path());
    configManager->set(QStringLiteral("Metadata"), QStringLiteral("embed_thumbnail"), false);

    TestableDownloadManager manager(configManager, this);
    QSignalSpy finishedSpy(&manager, &DownloadManager::downloadFinished);

    DownloadItem item;
    item.id = id;
    item.url = QStringLiteral("https://media.example.test/reentrant-playlist-item");
    item.tempFilePath = mediaPath;
    item.playlistIndex = 1;
    item.options.insert(QStringLiteral("type"), QStringLiteral("audio"));
    item.options.insert(QStringLiteral("is_playlist"), true);
    item.metadata.insert(QStringLiteral("id"), QStringLiteral("reentrant-playlist-item"));
    item.metadata.insert(QStringLiteral("postprocessor_warning"), QStringLiteral("synthetic completion warning"));

    // A completion warning emits progress synchronously. Simulate a UI action
    // that clears the row during that callback; the slot must not retain a
    // QMap reference after the reentrant removal.
    QObject worker;
    manager.m_activeWorkers.insert(id, &worker);
    manager.m_activeItems.insert(id, item);
    bool removedDuringCompletionProgress = false;
    connect(&manager, &DownloadManager::downloadProgress, &manager,
            [&manager, &removedDuringCompletionProgress, id](const QString &progressId, const QVariantMap &data) {
        if (progressId == id && data.contains(QStringLiteral("status"))) {
            removedDuringCompletionProgress = true;
            manager.m_activeItems.remove(id);
        }
    }, Qt::DirectConnection);

    const int slotIndex = manager.metaObject()->indexOfSlot(
        "onWorkerFinished(QString,bool,QString,QString,QString,QVariantMap)");
    QVERIFY(slotIndex >= 0);
    QVERIFY(manager.metaObject()->method(slotIndex).invoke(
        &manager,
        Q_ARG(QString, id),
        Q_ARG(bool, true),
        Q_ARG(QString, QStringLiteral("completed")),
        Q_ARG(QString, mediaPath),
        Q_ARG(QString, QString()),
        Q_ARG(QVariantMap, item.metadata)));

    QCOMPARE(finishedSpy.count(), 0);
    QVERIFY(removedDuringCompletionProgress);
    QVERIFY(!manager.m_activeItems.contains(id));
}

void TestDownloadManager::testFinalizationDoesNotBlockGuiThread()
{
    ConfigManager *configManager = getConfigManager();
    QTemporaryDir sourceDir;
    QTemporaryDir destinationDir;
    if (!sourceDir.isValid() || !destinationDir.isValid()) {
        QSKIP("The test environment cannot create isolated temporary directories.");
    }
    const QString tempRoot = sourceDir.path();
    const QString destination = destinationDir.path();
    const QString id = QStringLiteral("55555555-5555-4555-8555-555555555555");
    const QString sourcePath = QDir(tempRoot).filePath(QStringLiteral("finalization-test.webm"));
    QFile source(sourcePath);
    if (!source.open(QIODevice::WriteOnly)) {
        QSKIP("The test environment cannot write isolated temporary files.");
    }
    source.write("test media");
    source.close();

    configManager->set(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory"), destination);
    configManager->set(QStringLiteral("SortingRules"), QStringLiteral("size"), 0);

    SortingManager sortingManager(configManager);
    DownloadFinalizer finalizer(configManager, &sortingManager, nullptr);
    QSignalSpy completionSpy(&finalizer, &DownloadFinalizer::finalizationComplete);

    DownloadItem item;
    item.id = id;
    item.url = QStringLiteral("https://media.example.test/finalization-regression");
    item.tempFilePath = sourcePath;
    item.options.insert(QStringLiteral("type"), QStringLiteral("video"));
    item.metadata.insert(QStringLiteral("id"), QStringLiteral("finalization-regression"));

    int heartbeatCount = 0;
    QTimer heartbeat;
    heartbeat.setInterval(10);
    connect(&heartbeat, &QTimer::timeout, this, [&heartbeatCount]() { ++heartbeatCount; });
    heartbeat.start();

    QElapsedTimer callTimer;
    callTimer.start();
    finalizer.finalize(id, item);
    QVERIFY(callTimer.elapsed() < 1000);

    QTRY_COMPARE_WITH_TIMEOUT(completionSpy.count(), 1, 5000);
    QVERIFY(heartbeatCount > 0);
    QVERIFY(completionSpy.first().at(1).toBool());
    QVERIFY(QFileInfo::exists(QDir(destination).filePath(QStringLiteral("finalization-test.webm"))));
}

void TestDownloadManager::testCompletionStateSaveDoesNotBlockGuiThread()
{
    TestableDownloadManager manager(getConfigManager(), this);

    int heartbeatCount = 0;
    QTimer heartbeat;
    heartbeat.setInterval(10);
    connect(&heartbeat, &QTimer::timeout, this, [&heartbeatCount]() { ++heartbeatCount; });
    heartbeat.start();

    QElapsedTimer callTimer;
    callTimer.start();
    manager.callOnFinalizationComplete(QStringLiteral("completion-save-regression"), false,
                                       QStringLiteral("synthetic finalization failure"));
    QVERIFY(callTimer.elapsed() < 1000);
    QTRY_VERIFY_WITH_TIMEOUT(heartbeatCount > 0, 1000);
}

QTEST_MAIN(TestDownloadManager)
