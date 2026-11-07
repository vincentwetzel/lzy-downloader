#include "TestPlaylistRetry.h"

#include "core/ConfigManager.h"
#include "core/DownloadItem.h"
#include "core/DownloadQueueManager.h"
#include "core/ProcessUtils.h"
#include "core/PlaylistExpansionWorker.h"
#define private public
#include "core/DownloadManager.h"
#undef private

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

    QCOMPARE(manager.m_queueManager->queuedDownloadsCount(), 3);
    QCOMPARE(addedSpy.count(), 3);
    for (const QList<QVariant> &arguments : addedSpy) {
        const QVariantMap uiData = arguments.first().toMap();
        QVERIFY(uiData.value(QStringLiteral("playlistIndex")).toInt() > 0);
        QVERIFY(!uiData.value(QStringLiteral("options")).toMap()
                     .value(QStringLiteral("playlist_expansion_pending")).toBool());
    }
    manager.shutdown();
    ProcessUtils::clearCache();
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

QTEST_MAIN(TestPlaylistRetry)
