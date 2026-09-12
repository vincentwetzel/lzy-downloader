#include "core/RuntimeCoordinator.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QUuid>
#include <QtTest>

class TestRuntimeCoordinator : public QObject {
    Q_OBJECT

private slots:
    void testOwnerReceivesClientCommand();
    void testRejectsUnexpectedCommand();
};

void TestRuntimeCoordinator::testOwnerReceivesClientCommand()
{
    const QString name = QStringLiteral("LzyDownloaderTestCoordinator-%1")
                             .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    RuntimeCoordinator owner(name);
    if (owner.startOrNotify(QStringLiteral("show-ui")) != RuntimeCoordinator::StartResult::Owner) {
        QSKIP("QLocalServer is unavailable in this execution environment.");
    }

    QSignalSpy spy(&owner, &RuntimeCoordinator::commandReceived);
    RuntimeCoordinator client(name);
    const auto notifyClient = [&]() {
        return client.startOrNotify(QStringLiteral("ensure-api"))
               == RuntimeCoordinator::StartResult::ClientNotified;
    };
    // Windows named-pipe connection establishment can briefly lag behind the
    // owner's successful listen() call when both coordinator objects share a
    // test process. Use an explicit loop because QTRY_VERIFY evaluates its
    // expression again during its final assertion; calling startOrNotify()
    // there would send the command twice after a successful notification.
    bool notified = false;
    QElapsedTimer notifyTimer;
    notifyTimer.start();
    while (!notified && notifyTimer.elapsed() < 3000) {
        notified = notifyClient();
        if (!notified) {
            QTest::qWait(50);
        }
    }
    QVERIFY(notified);
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(), QStringLiteral("ensure-api"));
}

void TestRuntimeCoordinator::testRejectsUnexpectedCommand()
{
    RuntimeCoordinator coordinator(QStringLiteral("LzyDownloaderInvalidCommand"));
    QVERIFY(coordinator.startOrNotify(QStringLiteral("enqueue arbitrary input"))
            == RuntimeCoordinator::StartResult::Unavailable);
}

QTEST_GUILESS_MAIN(TestRuntimeCoordinator)
#include "TestRuntimeCoordinator.moc"
