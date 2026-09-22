#include "ProcessDiagnostics.h"

#include <QDir>
#include <QFile>
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
    quint64 residentBytes = 0;
    quint64 cpuTimeMs = 0;
};

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
    if (fields.size() <= 12) {
        return false;
    }

    bool parentOk = false;
    bool userOk = false;
    bool systemOk = false;
    const qint64 parentPid = fields.at(1).toLongLong(&parentOk);
    const quint64 userTicks = fields.at(11).toULongLong(&userOk);
    const quint64 systemTicks = fields.at(12).toULongLong(&systemOk);
    if (!parentOk || !userOk || !systemOk) {
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
    record->residentBytes = residentBytes;
    record->cpuTimeMs = (userTicks + systemTicks) * 1000ULL / static_cast<quint64>(ticksPerSecond);
    return true;
}

} // namespace
#endif

ProcessResourceSnapshot captureProcessTree(qint64 rootPid)
{
    ProcessResourceSnapshot result;
    result.platform = QStringLiteral("unsupported");
    if (rootPid <= 0) {
        return result;
    }

#ifdef Q_OS_WIN
    result.platform = QStringLiteral("windows");
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
        if (GetProcessTimes(process, &creation, &exit, &kernel, &user)) {
            result.cpuTimeMs += fileTimeToMilliseconds(kernel, user);
        }
        if (K32GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory))) {
            result.residentBytes += memory.WorkingSetSize;
        }
        if (pid == static_cast<DWORD>(rootPid)) {
            result.priorityClass = priorityName(GetPriorityClass(process));
        }
        result.processIds.append(static_cast<qint64>(pid));
        CloseHandle(process);
    }
    result.processCount = result.processIds.size();
    result.available = result.processCount > 0;
    return result;
#elif defined(Q_OS_LINUX)
    result.platform = QStringLiteral("linux");
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
            result.residentBytes += record.residentBytes;
            result.cpuTimeMs += record.cpuTimeMs;
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
