#include "core/GlobalDownloadLimiter.h"

#include <QStandardPaths>
#include <QtTest/QtTest>

class TestGlobalDownloadLimiter : public QObject
{
    Q_OBJECT

private slots:
    void enforcesLimitAcrossInstances();
    void releaseIsIdempotent();
};

void TestGlobalDownloadLimiter::enforcesLimitAcrossInstances()
{
    QStandardPaths::setTestModeEnabled(true);

    GlobalDownloadLimiter first(QStringLiteral("limiter-test-capacity"));
    GlobalDownloadLimiter second(QStringLiteral("limiter-test-capacity"));

    QVERIFY(first.tryAcquire(2));
    QVERIFY(second.tryAcquire(2));
    QVERIFY(!first.tryAcquire(2));

    first.release();
    QVERIFY(first.tryAcquire(2));
}

void TestGlobalDownloadLimiter::releaseIsIdempotent()
{
    QStandardPaths::setTestModeEnabled(true);

    GlobalDownloadLimiter limiter(QStringLiteral("limiter-test-idempotence"));
    QVERIFY(limiter.tryAcquire(1));
    limiter.release();
    limiter.release();

    QVERIFY(limiter.tryAcquire(1));
}

QTEST_GUILESS_MAIN(TestGlobalDownloadLimiter)

#include "TestGlobalDownloadLimiter.moc"
