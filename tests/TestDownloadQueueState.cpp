#include "BaseTest.h"
#include "core/DownloadQueueState.h"

#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QQueue>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest/QtTest>

class TestDownloadQueueState : public BaseTest
{
    Q_OBJECT

public:
    explicit TestDownloadQueueState(QObject *parent = nullptr);

private slots:
    void testSaveRestoresStatusesAndFields();
    void testEmptySaveRemovesBackup();
    void testLoadFiltersNonObjectEntries();
};

TestDownloadQueueState::TestDownloadQueueState(QObject *parent)
    : BaseTest(parent)
{
    QStandardPaths::setTestModeEnabled(true);
}

void TestDownloadQueueState::testSaveRestoresStatusesAndFields()
{
    DownloadQueueState state;
    DownloadItem active;
    active.id = QStringLiteral("active-id");
    active.url = QStringLiteral("https://example.test/active");
    active.options.insert(QStringLiteral("type"), QStringLiteral("video"));
    active.metadata.insert(QStringLiteral("title"), QStringLiteral("Active item"));
    active.playlistIndex = 2;
    active.tempFilePath = QStringLiteral("active.part");
    active.originalDownloadedFilePath = QStringLiteral("active.original");

    DownloadItem paused;
    paused.id = QStringLiteral("paused-id");
    paused.url = QStringLiteral("https://example.test/paused");

    DownloadItem stopped;
    stopped.id = QStringLiteral("stopped-id");
    stopped.options.insert(QStringLiteral("is_stopped"), true);

    DownloadItem failed;
    failed.id = QStringLiteral("failed-id");
    failed.options.insert(QStringLiteral("is_failed"), true);

    DownloadItem queued;
    queued.id = QStringLiteral("queued-id");

    QMap<QString, DownloadItem> pausedItems;
    pausedItems.insert(paused.id, paused);
    pausedItems.insert(stopped.id, stopped);
    pausedItems.insert(failed.id, failed);
    QQueue<DownloadItem> queue;
    queue.enqueue(queued);

    state.save({active}, pausedItems, queue);

    QSignalSpy resumeSpy(&state, &DownloadQueueState::resumeDownloadsRequested);
    const QJsonArray restored = state.load();
    QCOMPARE(restored.size(), 5);
    QCOMPARE(resumeSpy.count(), 1);

    QMap<QString, QString> statuses;
    for (const QJsonValue &value : restored) {
        const QJsonObject object = value.toObject();
        statuses.insert(object.value(QStringLiteral("id")).toString(),
                        object.value(QStringLiteral("status")).toString());
    }
    QCOMPARE(statuses.value(active.id), QStringLiteral("queued"));
    QCOMPARE(statuses.value(paused.id), QStringLiteral("paused"));
    QCOMPARE(statuses.value(stopped.id), QStringLiteral("stopped"));
    QCOMPARE(statuses.value(failed.id), QStringLiteral("stopped"));
    QCOMPARE(statuses.value(queued.id), QStringLiteral("queued"));

    const QJsonObject activeObject = restored.at(0).toObject();
    QCOMPARE(activeObject.value(QStringLiteral("url")).toString(), active.url);
    QCOMPARE(activeObject.value(QStringLiteral("playlistIndex")).toInt(), active.playlistIndex);
    QCOMPARE(activeObject.value(QStringLiteral("tempFilePath")).toString(), active.tempFilePath);
    QCOMPARE(activeObject.value(QStringLiteral("metadata")).toObject().value(QStringLiteral("title")).toString(),
             QStringLiteral("Active item"));

    state.clear();
}

void TestDownloadQueueState::testEmptySaveRemovesBackup()
{
    DownloadQueueState state;
    DownloadItem item;
    item.id = QStringLiteral("temporary-id");
    QQueue<DownloadItem> queue;
    queue.enqueue(item);
    state.save({}, {}, queue);
    QVERIFY(!state.load().isEmpty());

    state.save({}, {}, {});
    QVERIFY(state.load().isEmpty());
}

void TestDownloadQueueState::testLoadFiltersNonObjectEntries()
{
    DownloadQueueState state;
    DownloadItem item;
    item.id = QStringLiteral("valid-id");
    QQueue<DownloadItem> queue;
    queue.enqueue(item);
    state.save({}, {}, queue);

    const QString backupPath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                   .filePath(QStringLiteral("downloads_backup.json"));
    QFile backup(backupPath);
    QVERIFY(backup.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QJsonArray mixed;
    mixed.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("valid-id")}});
    mixed.append(QStringLiteral("invalid-entry"));
    mixed.append(42);
    backup.write(QJsonDocument(mixed).toJson(QJsonDocument::Compact));
    backup.close();

    QSignalSpy resumeSpy(&state, &DownloadQueueState::resumeDownloadsRequested);
    const QJsonArray restored = state.load();
    QCOMPARE(restored.size(), 1);
    QCOMPARE(restored.first().toObject().value(QStringLiteral("id")).toString(), QStringLiteral("valid-id"));
    QCOMPARE(resumeSpy.count(), 1);
    state.clear();
}

QTEST_GUILESS_MAIN(TestDownloadQueueState)
#include "TestDownloadQueueState.moc"
