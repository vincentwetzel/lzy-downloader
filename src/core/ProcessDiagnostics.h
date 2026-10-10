#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

namespace ProcessDiagnostics {

struct ProcessResourceDetail {
    qint64 pid = 0;
    qint64 parentPid = 0;
    quint64 creationTime = 0;
    quint64 residentBytes = 0;
    quint64 cpuTimeMs = 0;
    quint64 readBytes = 0;
    quint64 writeBytes = 0;
    QString imageName;
};

struct SystemResourceSnapshot {
    bool available = false;
    quint64 cpuTimeMs = 0;
    quint64 idleTimeMs = 0;
    quint64 memoryTotalBytes = 0;
    quint64 memoryAvailableBytes = 0;
};

struct ProcessResourceSnapshot {
    bool available = false;
    int processCount = 0;
    quint64 residentBytes = 0;
    quint64 cpuTimeMs = 0;
    quint64 readBytes = 0;
    quint64 writeBytes = 0;
    QList<qint64> processIds;
    QList<ProcessResourceDetail> processes;
    qint64 rootPid = 0;
    qint64 rootParentPid = 0;
    quint64 rootCreationTime = 0;
    QString rootImageName;
    QString priorityClass;
    QString platform;
    SystemResourceSnapshot system;
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
