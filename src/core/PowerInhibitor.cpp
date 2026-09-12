#include "PowerInhibitor.h"

#include <QDebug>

#if defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <IOKit/pwr_mgt/IOPMLib.h>
#elif defined(Q_OS_LINUX)
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <fcntl.h>
#include <unistd.h>
#endif

PowerInhibitor::~PowerInhibitor()
{
    release();
}

bool PowerInhibitor::acquire() noexcept
{
    if (m_active) {
        return true;
    }

#if defined(Q_OS_WIN)
    // A persistent power request is the Windows API intended for long-running
    // work. It is not tied to a worker thread and does not keep the display on.
    wchar_t reason[] = L"LzyDownloader downloads active";
    REASON_CONTEXT context{};
    context.Version = POWER_REQUEST_CONTEXT_VERSION;
    context.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    context.Reason.SimpleReasonString = reason;

    HANDLE powerRequest = PowerCreateRequest(&context);
    if (powerRequest != INVALID_HANDLE_VALUE) {
        if (PowerSetRequest(powerRequest, PowerRequestSystemRequired)) {
            m_windowsPowerRequest = powerRequest;
            m_active = true;
            qInfo() << "Windows system power request enabled for active downloads.";
            return true;
        }

        qWarning() << "Unable to set Windows system power request. Error:" << GetLastError();
        CloseHandle(powerRequest);
    } else {
        qWarning() << "Unable to create Windows system power request. Error:" << GetLastError();
    }

    // Keep a compatibility fallback for unusual Windows configurations where
    // the power-request object is unavailable. This legacy state is cleared
    // explicitly during release and is still better than no protection.
    if (SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED) == 0) {
        qWarning() << "Unable to prevent Windows system idle sleep. Error:" << GetLastError();
        return false;
    }
    m_windowsExecutionStateActive = true;
    m_active = true;
    return true;
#elif defined(Q_OS_MACOS)
    IOPMAssertionID assertionId = kIOPMNullAssertionID;
    const IOReturn result = IOPMAssertionCreateWithName(
        kIOPMAssertPreventUserIdleSystemSleep,
        kIOPMAssertionLevelOn,
        CFSTR("LzyDownloader downloads active"),
        &assertionId);
    if (result != kIOReturnSuccess) {
        qWarning() << "Unable to prevent macOS idle sleep. IOPM status:" << result;
        return false;
    }
    m_macosAssertionId = assertionId;
    m_active = true;
    return true;
#elif defined(Q_OS_LINUX)
    QDBusConnection systemBus = QDBusConnection::systemBus();
    if (!systemBus.isConnected()) {
        qWarning() << "Unable to prevent Linux idle sleep: system D-Bus is unavailable.";
        return false;
    }

    QDBusInterface loginManager(
        QStringLiteral("org.freedesktop.login1"),
        QStringLiteral("/org/freedesktop/login1"),
        QStringLiteral("org.freedesktop.login1.Manager"),
        systemBus);
    if (loginManager.isValid()) {
        const QDBusReply<QDBusUnixFileDescriptor> reply = loginManager.call(
            QStringLiteral("Inhibit"),
            QStringLiteral("sleep"),
            QStringLiteral("LzyDownloader"),
            QStringLiteral("Downloads are active"),
            QStringLiteral("block"));
        if (reply.isValid() && reply.value().isValid()) {
            const int duplicatedFd = fcntl(reply.value().fileDescriptor(), F_DUPFD_CLOEXEC, 3);
            if (duplicatedFd >= 0) {
                m_linuxLogin1Fd = duplicatedFd;
                m_active = true;
                return true;
            }
            qWarning() << "Unable to retain the Linux logind sleep-inhibitor descriptor.";
        } else if (!reply.isValid()) {
            qWarning() << "Linux logind sleep inhibition failed:" << reply.error().message();
        }
    }

    // Some desktop environments expose the freedesktop screensaver
    // inhibition API even when logind is unavailable. It is a useful fallback
    // for graphical Linux sessions; the logind lock above remains preferred.
    QDBusInterface screenSaver(
        QStringLiteral("org.freedesktop.ScreenSaver"),
        QStringLiteral("/ScreenSaver"),
        QStringLiteral("org.freedesktop.ScreenSaver"),
        QDBusConnection::sessionBus());
    if (screenSaver.isValid()) {
        const QDBusReply<uint> reply = screenSaver.call(
            QStringLiteral("Inhibit"),
            QStringLiteral("LzyDownloader"),
            QStringLiteral("Downloads are active"));
        if (reply.isValid()) {
            m_linuxScreenSaverCookie = reply.value();
            m_linuxScreenSaverInhibited = true;
            m_active = true;
            return true;
        }
    }

    qWarning() << "No usable Linux sleep-inhibition service was found.";
    return false;
#else
    qWarning() << "Sleep inhibition is not implemented for this platform.";
    return false;
#endif
}

void PowerInhibitor::release() noexcept
{
    if (!m_active) {
        return;
    }

#if defined(Q_OS_WIN)
    if (m_windowsPowerRequest) {
        const HANDLE powerRequest = static_cast<HANDLE>(m_windowsPowerRequest);
        if (!PowerClearRequest(powerRequest, PowerRequestSystemRequired)) {
            qWarning() << "Unable to clear Windows system power request. Error:" << GetLastError();
        }
        CloseHandle(powerRequest);
        m_windowsPowerRequest = nullptr;
    }
    if (m_windowsExecutionStateActive) {
        if (SetThreadExecutionState(ES_CONTINUOUS) == 0) {
            qWarning() << "Unable to release Windows system idle-sleep inhibition. Error:" << GetLastError();
        }
        m_windowsExecutionStateActive = false;
    }
#elif defined(Q_OS_MACOS)
    const IOReturn result = IOPMAssertionRelease(static_cast<IOPMAssertionID>(m_macosAssertionId));
    if (result != kIOReturnSuccess) {
        qWarning() << "Unable to release macOS idle-sleep inhibition. IOPM status:" << result;
    }
    m_macosAssertionId = kIOPMNullAssertionID;
#elif defined(Q_OS_LINUX)
    if (m_linuxLogin1Fd >= 0) {
        close(m_linuxLogin1Fd);
        m_linuxLogin1Fd = -1;
    }
    if (m_linuxScreenSaverInhibited) {
        QDBusInterface screenSaver(
            QStringLiteral("org.freedesktop.ScreenSaver"),
            QStringLiteral("/ScreenSaver"),
            QStringLiteral("org.freedesktop.ScreenSaver"),
            QDBusConnection::sessionBus());
        if (screenSaver.isValid()) {
            const QDBusMessage reply = screenSaver.call(
                QStringLiteral("UnInhibit"), m_linuxScreenSaverCookie);
            if (reply.type() == QDBusMessage::ErrorMessage) {
                qWarning() << "Unable to release Linux screensaver inhibition:" << reply.errorMessage();
            }
        }
        m_linuxScreenSaverCookie = 0;
        m_linuxScreenSaverInhibited = false;
    }
#endif

    m_active = false;
}

bool PowerInhibitor::isActive() const noexcept
{
    return m_active;
}
