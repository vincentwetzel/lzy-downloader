#include "CoordinatorWindowActivation.h"

#include <QDebug>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

void CoordinatorWindowActivation::activate(QWidget &window)
{
    window.setAttribute(Qt::WA_DontShowOnScreen, false);
    window.showNormal();
    window.raise();
    window.activateWindow();

#ifdef Q_OS_WIN
    // An older launcher may have left the first native show request hidden;
    // restore the HWND after Qt updates its own visibility state.
    const HWND nativeWindow = reinterpret_cast<HWND>(window.winId());
    if (nativeWindow && IsWindow(nativeWindow)) {
        ShowWindow(nativeWindow, SW_RESTORE);
        const BOOL foregroundSet = SetForegroundWindow(nativeWindow);
        qInfo() << "[CoordinatorWindowActivation] Native window restored."
                << "native_visible=" << (IsWindowVisible(nativeWindow) != FALSE)
                << "foreground_set=" << (foregroundSet != FALSE)
                << "extended_style=" << Qt::hex << GetWindowLongPtrW(nativeWindow, GWL_EXSTYLE);
    } else {
        qWarning() << "[CoordinatorWindowActivation] Native window handle is unavailable.";
    }
#endif
}
