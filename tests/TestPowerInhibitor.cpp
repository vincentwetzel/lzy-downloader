#include "core/PowerInhibitor.h"
#include <QtTest/QtTest>

class TestPowerInhibitor : public QObject {
    Q_OBJECT

private slots:
    void acquireAndReleaseAreIdempotent();
};

void TestPowerInhibitor::acquireAndReleaseAreIdempotent()
{
    PowerInhibitor inhibitor;
    QVERIFY(!inhibitor.isActive());

    const bool acquired = inhibitor.acquire();
    QCOMPARE(inhibitor.isActive(), acquired);
    if (acquired) {
        QVERIFY(inhibitor.acquire());
    }

    inhibitor.release();
    QVERIFY(!inhibitor.isActive());

    // Releasing an already released inhibitor must remain harmless.
    inhibitor.release();
    QVERIFY(!inhibitor.isActive());
}

QTEST_GUILESS_MAIN(TestPowerInhibitor)

#include "TestPowerInhibitor.moc"
