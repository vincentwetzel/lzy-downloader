#include "BaseTest.h"
#include "core/DownloadQueueManager.h"
#include "core/DownloadQueueState.h"

#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest/QtTest>

class TestDownloadQueueManager : public BaseTest {
    Q_OBJECT

private slots:
    void testEquivalentSourceUrlsAreDeduplicated();
    void testRetryDoesNotEnqueueEquivalentActiveItem();
    void testExplicitRedownloadRemovesTerminalPausedItem();
    void testRestoredRetryPreservesPlaylistIndex();
};

void TestDownloadQueueManager::testEquivalentSourceUrlsAreDeduplicated()
{
    DownloadQueueState state;
    DownloadQueueManager manager(getConfigManager(), getArchiveManager(), &state);

    DownloadItem first;
    first.id = QStringLiteral("first");
    first.url = QStringLiteral("https://youtu.be/dQw4w9WgXcQ?is=first");

    QSignalSpy addedSpy(&manager, &DownloadQueueManager::downloadAddedToQueue);
    manager.enqueueDownload(first);

    QCOMPARE(manager.getDuplicateStatus(
                  QStringLiteral("https://youtu.be/dQw4w9WgXcQ?is=second"), {}),
             DownloadQueueManager::DuplicateInQueue);
    QCOMPARE(addedSpy.count(), 1);
}

void TestDownloadQueueManager::testRetryDoesNotEnqueueEquivalentActiveItem()
{
    DownloadQueueState state;
    DownloadQueueManager manager(getConfigManager(), getArchiveManager(), &state);

    DownloadItem active;
    active.id = QStringLiteral("active");
    active.url = QStringLiteral("https://youtu.be/dQw4w9WgXcQ?is=first");
    QMap<QString, DownloadItem> activeItems;
    activeItems.insert(active.id, active);

    QVariantMap retryData;
    retryData.insert(QStringLiteral("id"), QStringLiteral("retry"));
    retryData.insert(QStringLiteral("url"), QStringLiteral("https://youtu.be/dQw4w9WgXcQ?is=second"));

    QSignalSpy addedSpy(&manager, &DownloadQueueManager::downloadAddedToQueue);
    manager.retryDownload(retryData, activeItems);

    QCOMPARE(addedSpy.count(), 0);
    QCOMPARE(manager.queuedDownloadsCount(), 0);
}

void TestDownloadQueueManager::testExplicitRedownloadRemovesTerminalPausedItem()
{
    DownloadQueueState state;
    DownloadQueueManager manager(getConfigManager(), getArchiveManager(), &state);

    DownloadItem stopped;
    stopped.id = QStringLiteral("stopped");
    stopped.url = QStringLiteral("https://youtu.be/dQw4w9WgXcQ");
    stopped.options.insert(QStringLiteral("is_stopped"), true);
    manager.processResumeDownloadsSelection(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), stopped.id},
            {QStringLiteral("url"), stopped.url},
            {QStringLiteral("options"), QJsonObject::fromVariantMap(stopped.options)},
            {QStringLiteral("status"), QStringLiteral("stopped")}
        }
    });

    DownloadItem candidate;
    candidate.id = QStringLiteral("new-job");
    candidate.url = QStringLiteral("https://www.youtube.com/watch?v=dQw4w9WgXcQ");
    QString removedId;
    QVERIFY(manager.removeTerminalPausedDuplicate(candidate, &removedId));
    QCOMPARE(removedId, stopped.id);
    QCOMPARE(manager.pausedDownloadsCount(), 0);
    QCOMPARE(manager.getDuplicateStatus(candidate, {}), DownloadQueueManager::NotDuplicate);

    DownloadItem paused;
    paused.id = QStringLiteral("paused");
    paused.url = stopped.url;
    manager.processResumeDownloadsSelection(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), paused.id},
            {QStringLiteral("url"), paused.url},
            {QStringLiteral("status"), QStringLiteral("paused")}
        }
    });
    QVERIFY(!manager.removeTerminalPausedDuplicate(candidate, nullptr));
    QCOMPARE(manager.pausedDownloadsCount(), 1);
}

void TestDownloadQueueManager::testRestoredRetryPreservesPlaylistIndex()
{
    DownloadQueueState state;
    DownloadQueueManager manager(getConfigManager(), getArchiveManager(), &state);

    const QString id = QStringLiteral("restored-playlist-item");
    const QString url = QStringLiteral("https://www.youtube.com/watch?v=restored-playlist-item");
    const QVariantMap options = {
        {QStringLiteral("type"), QStringLiteral("audio")},
        {QStringLiteral("playlist_index"), 5},
        {QStringLiteral("is_stopped"), true}
    };

    QSignalSpy addedSpy(&manager, &DownloadQueueManager::downloadAddedToQueue);
    manager.processResumeDownloadsSelection(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), id},
            {QStringLiteral("url"), url},
            {QStringLiteral("options"), QJsonObject::fromVariantMap(options)},
            {QStringLiteral("playlistIndex"), 5},
            {QStringLiteral("status"), QStringLiteral("stopped")}
        }
    });

    QCOMPARE(addedSpy.count(), 1);
    const QVariantMap restoredUiData = addedSpy.first().at(0).toMap();
    QCOMPARE(restoredUiData.value(QStringLiteral("playlistIndex")).toInt(), 5);

    manager.retryDownload(restoredUiData, {});
    const DownloadItem retried = manager.takeNextQueuedDownload();
    QCOMPARE(retried.id, id);
    QCOMPARE(retried.playlistIndex, 5);
}

QTEST_GUILESS_MAIN(TestDownloadQueueManager)
#include "TestDownloadQueueManager.moc"
