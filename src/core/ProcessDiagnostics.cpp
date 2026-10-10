#include "ProcessDiagnostics.h"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <utility>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#endif

#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

namespace ProcessDiagnostics {

#ifdef Q_OS_WIN
namespace {

quint64 fileTimeToMilliseconds(const FILETIME &kernel, const FILETIME &user)
{
    ULARGE_INTEGER kernelValue{};
    ULARGE_INTEGER userValue{};
    kernelValue.LowPart = kernel.dwLowDateTime;
    kernelValue.HighPart = kernel.dwHighDateTime;
    userValue.LowPart = user.dwLowDateTime;
    userValue.HighPart = user.dwHighDateTime;
    return (kernelValue.QuadPart + userValue.QuadPart) / 10000ULL;
}

quint64 fileTimeToInteger(const FILETIME &time)
{
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}

QString processImageName(HANDLE process)
{
    if (!process) {
        return QStringLiteral("unknown");
    }

    wchar_t buffer[32768]{};
    DWORD length = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    if (!QueryFullProcessImageNameW(process, 0, buffer, &length)) {
        return QStringLiteral("unknown");
    }
    return QFileInfo(QString::fromWCharArray(buffer, static_cast<int>(length))).fileName();
}

SystemResourceSnapshot captureSystemResources()
{
    SystemResourceSnapshot result;
    FILETIME idle{}, kernel{}, user{};
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (!GetSystemTimes(&idle, &kernel, &user) || !GlobalMemoryStatusEx(&memory)) {
        return result;
    }

    result.available = true;
    result.cpuTimeMs = (fileTimeToInteger(kernel) + fileTimeToInteger(user)) / 10000ULL;
    result.idleTimeMs = fileTimeToInteger(idle) / 10000ULL;
    result.memoryTotalBytes = memory.ullTotalPhys;
    result.memoryAvailableBytes = memory.ullAvailPhys;
    return result;
}

QString priorityName(DWORD priority)
{
    switch (priority) {
    case IDLE_PRIORITY_CLASS: return QStringLiteral("idle");
    case BELOW_NORMAL_PRIORITY_CLASS: return QStringLiteral("below_normal");
    case NORMAL_PRIORITY_CLASS: return QStringLiteral("normal");
    case ABOVE_NORMAL_PRIORITY_CLASS: return QStringLiteral("above_normal");
    case HIGH_PRIORITY_CLASS: return QStringLiteral("high");
    case REALTIME_PRIORITY_CLASS: return QStringLiteral("realtime");
    default: return QStringLiteral("unknown");
    }
}

} // namespace
#endif

#ifdef Q_OS_LINUX
namespace {

struct LinuxProcessRecord {
    qint64 pid = 0;
    qint64 parentPid = 0;
    quint64 creationTime = 0;
    quint64 residentBytes = 0;
    quint64 cpuTimeMs = 0;
    quint64 readBytes = 0;
    quint64 writeBytes = 0;
    QString imageName;
};

SystemResourceSnapshot captureSystemResources()
{
    SystemResourceSnapshot result;
    QFile statFile(QStringLiteral("/proc/stat"));
    QFile memoryFile(QStringLiteral("/proc/meminfo"));
    if (!statFile.open(QIODevice::ReadOnly) || !memoryFile.open(QIODevice::ReadOnly)) {
        return result;
    }

    const QList<QByteArray> statLines = statFile.readAll().split('\n');
    const QByteArray cpuLine = statLines.isEmpty() ? QByteArray() : statLines.first();
    const QList<QByteArray> cpuFields = cpuLine.simplified().split(' ');
    if (cpuFields.size() < 5 || cpuFields.first() != QByteArrayLiteral("cpu")) {
        return result;
    }

    bool parsedCpu = false;
    quint64 totalTicks = 0;
    for (qsizetype index = 1; index < cpuFields.size(); ++index) {
        bool ok = false;
        const quint64 ticks = cpuFields.at(index).toULongLong(&ok);
        if (!ok) {
            return result;
        }
        totalTicks += ticks;
        parsedCpu = true;
    }

    quint64 availableBytes = 0;
    quint64 totalBytes = 0;
    for (const QByteArray &line : memoryFile.readAll().split('\n')) {
        const QList<QByteArray> fields = line.simplified().split(' ');
        if (fields.size() < 2) {
            continue;
        }
        bool ok = false;
        const quint64 kib = fields.at(1).toULongLong(&ok);
        if (!ok) {
            continue;
        }
        if (fields.first() == QByteArrayLiteral("MemTotal:")) {
            totalBytes = kib * 1024ULL;
        } else if (fields.first() == QByteArrayLiteral("MemAvailable:")) {
            availableBytes = kib * 1024ULL;
        }
    }

    const long ticksPerSecond = sysconf(_SC_CLK_TCK);
    if (!parsedCpu || ticksPerSecond <= 0 || totalBytes == 0) {
        return result;
    }

    const quint64 idleTicks = cpuFields.at(4).toULongLong();
    result.available = true;
    result.cpuTimeMs = totalTicks * 1000ULL / static_cast<quint64>(ticksPerSecond);
    result.idleTimeMs = idleTicks * 1000ULL / static_cast<quint64>(ticksPerSecond);
    result.memoryTotalBytes = totalBytes;
    result.memoryAvailableBytes = availableBytes;
    return result;
}

bool readLinuxProcess(qint64 pid, LinuxProcessRecord *record)
{
    if (!record) {
        return false;
    }

    QFile statFile(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!statFile.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray stat = statFile.readAll();
    const qsizetype closingName = stat.lastIndexOf(')');
    if (closingName < 0) {
        return false;
    }
    const QList<QByteArray> fields = stat.mid(closingName + 2).split(' ');
    if (fields.size() <= 19) {
        return false;
    }

    bool parentOk = false;
    bool userOk = false;
    bool systemOk = false;
    const qint64 parentPid = fields.at(1).toLongLong(&parentOk);
    const quint64 userTicks = fields.at(11).toULongLong(&userOk);
    const quint64 systemTicks = fields.at(12).toULongLong(&systemOk);
    bool startOk = false;
    const quint64 startTime = fields.at(19).toULongLong(&startOk);
    if (!parentOk || !userOk || !systemOk || !startOk) {
        return false;
    }

    QFile statusFile(QStringLiteral("/proc/%1/status").arg(pid));
    if (!statusFile.open(QIODevice::ReadOnly)) {
        return false;
    }
    quint64 residentBytes = 0;
    const QList<QByteArray> statusLines = statusFile.readAll().split('\n');
    for (const QByteArray &line : statusLines) {
        if (!line.startsWith("VmRSS:")) {
            continue;
        }
        const QList<QByteArray> parts = line.simplified().split(' ');
        if (parts.size() >= 2) {
            bool rssOk = false;
            residentBytes = parts.at(1).toULongLong(&rssOk) * 1024ULL;
            if (!rssOk) {
                residentBytes = 0;
            }
        }
        break;
    }

    const long ticksPerSecond = sysconf(_SC_CLK_TCK);
    if (ticksPerSecond <= 0) {
        return false;
    }

    record->pid = pid;
    record->parentPid = parentPid;
    record->creationTime = startTime;
    record->residentBytes = residentBytes;
    record->cpuTimeMs = (userTicks + systemTicks) * 1000ULL / static_cast<quint64>(ticksPerSecond);
    QFile ioFile(QStringLiteral("/proc/%1/io").arg(pid));
    if (ioFile.open(QIODevice::ReadOnly)) {
        for (const QByteArray &line : ioFile.readAll().split('\n')) {
            const QList<QByteArray> parts = line.simplified().split(' ');
            if (parts.size() < 2) {
                continue;
            }
            bool ioOk = false;
            const quint64 bytes = parts.at(1).toULongLong(&ioOk);
            if (!ioOk) {
                continue;
            }
            if (parts.first() == QByteArrayLiteral("read_bytes:")) {
                record->readBytes = bytes;
            } else if (parts.first() == QByteArrayLiteral("write_bytes:")) {
                record->writeBytes = bytes;
            }
        }
    }
    QFile commFile(QStringLiteral("/proc/%1/comm").arg(pid));
    if (commFile.open(QIODevice::ReadOnly)) {
        record->imageName = QString::fromUtf8(commFile.readAll()).trimmed();
    }
    if (record->imageName.isEmpty()) {
        record->imageName = QStringLiteral("unknown");
    }
    return true;
}

} // namespace
#endif

ProcessResourceSnapshot captureProcessTree(qint64 rootPid)
{
    ProcessResourceSnapshot result;
    result.platform = QStringLiteral("unsupported");
    result.rootPid = rootPid;
    if (rootPid <= 0) {
        return result;
    }

#ifdef Q_OS_WIN
    result.platform = QStringLiteral("windows");
    result.system = captureSystemResources();
    struct ProcessRecord {
        DWORD pid = 0;
        DWORD parentPid = 0;
    };

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return result;
    }

    QList<ProcessRecord> records;
    PROCESSENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Process32First(snapshot, &entry)) {
        do {
            records.append({entry.th32ProcessID, entry.th32ParentProcessID});
        } while (Process32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);

    QList<DWORD> pending{static_cast<DWORD>(rootPid)};
    QSet<DWORD> included;
    while (!pending.isEmpty()) {
        const DWORD pid = pending.takeFirst();
        if (included.contains(pid)) {
            continue;
        }
        included.insert(pid);
        for (const ProcessRecord &record : records) {
            if (record.parentPid == pid && record.pid != pid) {
                pending.append(record.pid);
            }
        }
    }

    for (const DWORD pid : std::as_const(included)) {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!process) {
            continue;
        }

        FILETIME creation{}, exit{}, kernel{}, user{};
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        if (!GetProcessTimes(process, &creation, &exit, &kernel, &user)) {
            CloseHandle(process);
            continue;
        }
        const auto recordIt = std::find_if(records.cbegin(), records.cend(), [pid](const ProcessRecord &record) {
            return record.pid == pid;
        });
        const qint64 parentPid = recordIt == records.cend() ? 0 : static_cast<qint64>(recordIt->parentPid);
        const quint64 cpuTimeMs = fileTimeToMilliseconds(kernel, user);
        const quint64 residentBytes = K32GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory))
            ? memory.WorkingSetSize : 0;
        IO_COUNTERS io{};
        const bool ioAvailable = GetProcessIoCounters(process, &io) != FALSE;

        ProcessResourceDetail detail;
        detail.pid = static_cast<qint64>(pid);
        detail.parentPid = parentPid;
        detail.creationTime = fileTimeToInteger(creation);
        detail.cpuTimeMs = cpuTimeMs;
        detail.residentBytes = residentBytes;
        detail.readBytes = ioAvailable ? io.ReadTransferCount : 0;
        detail.writeBytes = ioAvailable ? io.WriteTransferCount : 0;
        detail.imageName = processImageName(process);
        result.processes.append(detail);
        result.cpuTimeMs += cpuTimeMs;
        result.residentBytes += residentBytes;
        result.readBytes += detail.readBytes;
        result.writeBytes += detail.writeBytes;
        result.processIds.append(detail.pid);
        if (pid == static_cast<DWORD>(rootPid)) {
            result.rootParentPid = parentPid;
            result.rootCreationTime = detail.creationTime;
            result.rootImageName = detail.imageName;
            result.priorityClass = priorityName(GetPriorityClass(process));
        }
        CloseHandle(process);
    }
    result.processCount = result.processIds.size();
    result.available = result.processCount > 0;
    return result;
#elif defined(Q_OS_LINUX)
    result.platform = QStringLiteral("linux");
    result.system = captureSystemResources();
    QDir procDir(QStringLiteral("/proc"));
    const QStringList entries = procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QList<LinuxProcessRecord> records;
    for (const QString &entry : entries) {
        bool ok = false;
        const qint64 pid = entry.toLongLong(&ok);
        if (!ok) {
            continue;
        }
        LinuxProcessRecord record;
        if (readLinuxProcess(pid, &record)) {
            records.append(record);
        }
    }

    QList<qint64> pending{rootPid};
    QSet<qint64> included;
    while (!pending.isEmpty()) {
        const qint64 pid = pending.takeFirst();
        if (included.contains(pid)) {
            continue;
        }
        included.insert(pid);
        for (const LinuxProcessRecord &record : records) {
            if (record.parentPid == pid && record.pid != pid) {
                pending.append(record.pid);
            }
        }
    }

    for (const qint64 pid : std::as_const(included)) {
        for (const LinuxProcessRecord &record : records) {
            if (record.pid != pid) {
                continue;
            }
            result.processIds.append(pid);
            ProcessResourceDetail detail;
            detail.pid = record.pid;
            detail.parentPid = record.parentPid;
            detail.creationTime = record.creationTime;
            detail.cpuTimeMs = record.cpuTimeMs;
            detail.residentBytes = record.residentBytes;
            detail.readBytes = record.readBytes;
            detail.writeBytes = record.writeBytes;
            detail.imageName = record.imageName;
            result.processes.append(detail);
            result.residentBytes += record.residentBytes;
            result.cpuTimeMs += record.cpuTimeMs;
            result.readBytes += record.readBytes;
            result.writeBytes += record.writeBytes;
            if (pid == rootPid) {
                result.rootParentPid = record.parentPid;
                result.rootCreationTime = record.creationTime;
                result.rootImageName = record.imageName;
            }
            break;
        }
    }
    result.processCount = result.processIds.size();
    result.available = result.processCount > 0;
    return result;
#else
    Q_UNUSED(rootPid)
    return result;
#endif
}

} // namespace ProcessDiagnostics
