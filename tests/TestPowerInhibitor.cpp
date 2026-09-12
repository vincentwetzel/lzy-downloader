#include <QtGlobal>

#if defined(Q_OS_WIN)
// Verify the native request without invoking elevated powercfg commands.
#define private public
#endif
#include "core/PowerInhibitor.h"
#if defined(Q_OS_WIN)
#undef private
#endif
#include <QtTest/QtTest>

class TestPowerInhibitor : public QObject {
    Q_OBJECT

private slots:
    void acquireAndReleaseAreIdempotent();
#if defined(Q_OS_WIN)
    void windowsPersistentPowerRequestLifecycle();
#endif
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

#if defined(Q_OS_WIN)
void TestPowerInhibitor::windowsPersistentPowerRequestLifecycle()
{
    PowerInhibitor inhibitor;
    QVERIFY(inhibitor.acquire());
    QVERIFY(inhibitor.m_windowsPowerRequest != nullptr);
    QVERIFY(!inhibitor.m_windowsExecutionStateActive);

    // Re-acquisition must not replace or stack the native request.
    void *request = inhibitor.m_windowsPowerRequest;
    QVERIFY(inhibitor.acquire());
    QCOMPARE(inhibitor.m_windowsPowerRequest, request);

    inhibitor.release();
    QVERIFY(!inhibitor.isActive());
    QVERIFY(inhibitor.m_windowsPowerRequest == nullptr);
    QVERIFY(!inhibitor.m_windowsExecutionStateActive);
}
#endif

QTEST_GUILESS_MAIN(TestPowerInhibitor)

#include "TestPowerInhibitor.moc"
