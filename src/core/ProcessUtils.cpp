#include "ProcessUtils.h"
#include "core/ConfigManager.h"
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QProcessEnvironment>
#include <QStringList>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <signal.h>
#include <sys/types.h>
#endif
#include <QMutex>
#include "core/SmartBinaryResolver.h"

namespace ProcessUtils {

#ifdef Q_OS_WIN
namespace {

HANDLE ensureTrackedProcessJob()
{
    static HANDLE s_processJob = []() -> HANDLE {
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (!job) {
            qWarning() << "[ProcessUtils] Failed to create process cleanup job object:" << GetLastError();
            return nullptr;
        }

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(
                job,
                JobObjectExtendedLimitInformation,
                &info,
                sizeof(info))) {
            qWarning() << "[ProcessUtils] Failed to configure process cleanup job object:" << GetLastError();
            CloseHandle(job);
            return nullptr;
        }
        return job;
    }();

    return s_processJob;
}

void assignProcessToTerminationJob(QProcess *process)
{
    if (!process) {
        return;
    }

    const qint64 pid = process->processId();
    if (pid <= 0) {
        return;
    }

    HANDLE job = ensureTrackedProcessJob();
    if (!job) {
        return;
    }

    HANDLE processHandle = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_QUOTA | PROCESS_TERMINATE,
        FALSE,
        static_cast<DWORD>(pid));
    if (!processHandle) {
        qWarning() << "[ProcessUtils] Failed to open process for job tracking. PID:" << pid
                   << "error:" << GetLastError();
        return;
    }

    if (!AssignProcessToJobObject(job, processHandle)) {
        const DWORD error = GetLastError();
        if (error != ERROR_ACCESS_DENIED) {
            qWarning() << "[ProcessUtils] Failed to assign PID" << pid
                       << "to cleanup job object. error:" << error;
        }
    } else {
        qInfo() << "[ProcessUtils] Assigned PID" << pid << "to shutdown cleanup job.";
    }

    CloseHandle(processHandle);
}

} // namespace
#endif

// Static cache for resolved binary paths
static QHash<QString, FoundBinary> s_binaryCache;
static QMutex s_binaryCacheMutex;

FoundBinary findBinary(const QString& name, ConfigManager* configManager)
{
    {
        QMutexLocker locker(&s_binaryCacheMutex);
        // Check cache first
        if (s_binaryCache.contains(name)) {
            return s_binaryCache.value(name);
        }
    }

    FoundBinary result = resolveBinary(name, configManager);

    {
        QMutexLocker locker(&s_binaryCacheMutex);
        // Cache the result
        s_binaryCache.insert(name, result);
    }

    return result;
}

FoundBinary resolveBinary(const QString& name, ConfigManager* configManager)
{
    return SmartBinaryResolver::resolve(name, configManager);
}

void clearCache() {
    QMutexLocker locker(&s_binaryCacheMutex);
    s_binaryCache.clear();
}

void cacheBinary(const QString& name, const FoundBinary& found) {
    QMutexLocker locker(&s_binaryCacheMutex);
    s_binaryCache.insert(name, found);
}

FoundBinary getCachedBinary(const QString& name) {
    QMutexLocker locker(&s_binaryCacheMutex);
    return s_binaryCache.value(name);
}

bool hasCachedBinary(const QString& name) {
    QMutexLocker locker(&s_binaryCacheMutex);
    return s_binaryCache.contains(name);
}

void setProcessEnvironment(QProcess &process) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    const QStringList proxyKeys = {
        QStringLiteral("HTTP_PROXY"),
        QStringLiteral("HTTPS_PROXY"),
        QStringLiteral("ALL_PROXY"),
        QStringLiteral("http_proxy"),
        QStringLiteral("https_proxy"),
        QStringLiteral("all_proxy")
    };
    for (const QString &key : proxyKeys) {
        env.remove(key);
    }
    process.setProcessEnvironment(env);

#ifdef Q_OS_WIN
    if (!process.property("_lzy_shutdown_job_connected").toBool()) {
        process.setProperty("_lzy_shutdown_job_connected", true);
        QProcess *p = &process;
        QObject::connect(p, &QProcess::started, p, [p]() {
            assignProcessToTerminationJob(p);
        });
    }

    if (process.state() != QProcess::NotRunning) {
        assignProcessToTerminationJob(&process);
    }
#endif
}

void terminateProcessTree(QProcess *process, int gracefulTimeoutMs) {
    if (!process || process->state() == QProcess::NotRunning) {
        return;
    }

    const qint64 pid = process->processId();
    qInfo() << "[ProcessUtils] Terminating process tree for PID" << pid;

#ifdef Q_OS_WIN
    if (pid > 0) {
        // Run taskkill synchronously to ensure it enumerates and kills child processes (like ffmpeg)
        // BEFORE the parent process is killed. Using startDetached + process->kill() causes a race
        // condition where the parent dies instantly and taskkill orphans the children.
        QProcess killer;
        killer.start(QStringLiteral("taskkill.exe"), {QStringLiteral("/PID"), QString::number(pid), QStringLiteral("/T"), QStringLiteral("/F")});
        killer.waitForFinished(gracefulTimeoutMs > 0 ? gracefulTimeoutMs : 2000);
    }

    process->kill();
#else
    Q_UNUSED(gracefulTimeoutMs)
    process->kill();
#endif
}

void sendGracefulInterrupt(qint64 pid) {
    if (pid <= 0) return;
    qInfo() << "[ProcessUtils] Sending graceful interrupt to PID" << pid;

#ifdef Q_OS_WIN
    // Disable CTRL+C handling for our process permanently.
    // GenerateConsoleCtrlEvent is asynchronous, so restoring the handler immediately
    // afterwards causes a race condition where the OS kills the app before FreeConsole finishes.
    SetConsoleCtrlHandler(nullptr, TRUE);

    if (AttachConsole(static_cast<DWORD>(pid))) {
        GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
        // Allow the async signal a brief moment to be delivered to the target process
        Sleep(50);
        FreeConsole();
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            // Already attached to a console (e.g. Debug Console). Send to process group directly.
            GenerateConsoleCtrlEvent(CTRL_C_EVENT, static_cast<DWORD>(pid));
        } else {
            qWarning() << "[ProcessUtils] Failed to attach console to PID" << pid << "for graceful interrupt. Error:" << err;
        }
    }
#else
    kill(static_cast<pid_t>(pid), SIGINT);
#endif
}

QString fetchFfmpegVersion(const QString& execPath) {
    if (execPath.isEmpty() || !QFileInfo::exists(execPath)) {
        return QStringLiteral("Not Found");
    }

    QProcess process;
    process.start(execPath, QStringList{QStringLiteral("-version")});
    if (process.waitForFinished(3000)) {
        QString output = QString::fromUtf8(process.readAllStandardOutput());

        // Match specific clean patterns: YYYY-MM-DD, semantic version (e.g., 6.0, 4.4.1), or N-builds
        static const QRegularExpression re(QStringLiteral("(?:ffmpeg|ffprobe) version ([0-9]{4}-[0-9]{2}-[0-9]{2}|[0-9]+\\.[0-9]+(?:\\.[0-9]+)?|[Nn]-[0-9]+)"));
        QRegularExpressionMatch match = re.match(output);
        if (match.hasMatch()) {
            return match.captured(1);
        }

        // Fallback: take the first sequence of characters before a hyphen or space
        static const QRegularExpression fallbackRe(QStringLiteral("(?:ffmpeg|ffprobe) version ([^- ]+)"));
        QRegularExpressionMatch fallbackMatch = fallbackRe.match(output);
        if (fallbackMatch.hasMatch()) {
            return fallbackMatch.captured(1);
        }
    } else {
        if (process.state() != QProcess::NotRunning) {
            qWarning() << "[ProcessUtils] fetchFfmpegVersion timed out for" << execPath;
            process.kill();
            process.waitForFinished(1000);
        } else {
            qWarning() << "[ProcessUtils] fetchFfmpegVersion failed to start for" << execPath << ":" << process.errorString();
        }
    }
    return QStringLiteral("Unknown");
}

} // namespace ProcessUtils
