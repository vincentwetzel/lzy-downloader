#include "ui/CoordinatorWindowActivation.h"

#include <QWidget>
#include <QtTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

class TestCoordinatorWindowActivation final : public QObject {
    Q_OBJECT

private slots:
    void testActivationRestoresWindowVisibility();
};

void TestCoordinatorWindowActivation::testActivationRestoresWindowVisibility()
{
    QWidget window;
    window.setAttribute(Qt::WA_DontShowOnScreen, true);

    CoordinatorWindowActivation::activate(window);

    QVERIFY(window.isVisible());
    QVERIFY(!window.testAttribute(Qt::WA_DontShowOnScreen));
#ifdef Q_OS_WIN
    const HWND nativeWindow = reinterpret_cast<HWND>(window.winId());
    if (!nativeWindow || !IsWindow(nativeWindow)) {
        QSKIP("The Qt minimal plugin did not provide a native window handle.");
    }
    QVERIFY(IsWindowVisible(nativeWindow));
#endif
    window.hide();
}

QTEST_MAIN(TestCoordinatorWindowActivation)
#include "TestCoordinatorWindowActivation.moc"
