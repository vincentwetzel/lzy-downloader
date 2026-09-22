#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

namespace ProcessDiagnostics {

struct ProcessResourceSnapshot {
    bool available = false;
    int processCount = 0;
    quint64 residentBytes = 0;
    quint64 cpuTimeMs = 0;
    QList<qint64> processIds;
    QString priorityClass;
    QString platform;
};

/**
 * Captures aggregate resource usage for a process and its descendants.
 *
 * The caller owns the sampling interval and must invoke this off the GUI
 * thread. Unsupported platforms return available=false rather than probing
 * through a shell or adding a runtime dependency.
 */
[[nodiscard]] ProcessResourceSnapshot captureProcessTree(qint64 rootPid);

} // namespace ProcessDiagnostics
