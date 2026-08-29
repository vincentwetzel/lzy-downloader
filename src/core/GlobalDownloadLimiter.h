#pragma once

#include <QList>
#include <QString>
#include <functional>

/**
 * Coordinates download-worker slots between multiple LzyDownloader processes.
 *
 * The GUI and server-mode launcher can be separate processes, so a
 * DownloadManager-local counter is not sufficient to enforce max_threads.
 * This class keeps a small, locked process registry in the user's app-local
 * data directory and removes entries belonging to processes that no longer
 * exist.
 */
class GlobalDownloadLimiter {
public:
    explicit GlobalDownloadLimiter(const QString &namespaceKey = QStringLiteral("default"));
    ~GlobalDownloadLimiter();

    GlobalDownloadLimiter(const GlobalDownloadLimiter &) = delete;
    GlobalDownloadLimiter &operator=(const GlobalDownloadLimiter &) = delete;

    bool tryAcquire(int maximumSlots);
    void release();
    void releaseAll();

private:
    struct Holder {
        qint64 processId = 0;
        int slotCount = 0;
    };

    bool withLockedState(const std::function<bool(QList<Holder> &)> &operation);
    QList<Holder> readState() const;
    bool writeState(const QList<Holder> &holders) const;
    void removeStaleHolders(QList<Holder> &holders) const;
    static bool isProcessAlive(qint64 processId);

    QString m_lockPath;
    QString m_statePath;
    qint64 m_processId;
    int m_reservedSlots = 0;
};
