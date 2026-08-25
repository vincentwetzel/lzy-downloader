#pragma once

#include <QtGlobal>

/**
 * Best-effort platform power-management inhibition for active downloads.
 *
 * The inhibitor keeps the operating system awake while at least one download
 * is active, without preventing the display from turning off. If the host
 * platform does not expose a usable power-management service, callers can
 * continue normally and should retain their own diagnostics.
 */
class PowerInhibitor {
public:
    PowerInhibitor() = default;
    ~PowerInhibitor();

    PowerInhibitor(const PowerInhibitor &) = delete;
    PowerInhibitor &operator=(const PowerInhibitor &) = delete;

    /**
     * Acquires the platform sleep inhibitor.
     *
     * The operation is idempotent. Returns true when the inhibitor is held
     * after the call and false when the platform refused or cannot provide it.
     */
    [[nodiscard]] bool acquire() noexcept;

    /** Releases the platform sleep inhibitor, if held. */
    void release() noexcept;

    /** Returns whether this instance currently holds an inhibitor. */
    [[nodiscard]] bool isActive() const noexcept;

private:
    bool m_active = false;

#if defined(Q_OS_MACOS)
    unsigned int m_macosAssertionId = 0;
#elif defined(Q_OS_LINUX)
    int m_linuxLogin1Fd = -1;
    unsigned int m_linuxScreenSaverCookie = 0;
    bool m_linuxScreenSaverInhibited = false;
#endif
};
