#include "ProcessUtils.h"
#include "core/ConfigManager.h"
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QProcessEnvironment>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <signal.h>
#endif
#include <QMutex>

namespace ProcessUtils {

#ifdef Q_OS_WIN
namespace {

HANDLE ensureTrackedProcessJob()
{
    static HANDLE s_processJob = nullptr;
    static bool s_initialized = false;

    if (s_initialized) {
        return s_processJob;
    }
    s_initialized = true;

    s_processJob = CreateJobObjectW(nullptr, nullptr);
    if (!s_processJob) {
        qWarning() << "[ProcessUtils] Failed to create process cleanup job object:" << GetLastError();
        return nullptr;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(
            s_processJob,
            JobObjectExtendedLimitInformation,
            &info,
            sizeof(info))) {
        qWarning() << "[ProcessUtils] Failed to configure process cleanup job object:" << GetLastError();
        CloseHandle(s_processJob);
        s_processJob = nullptr;
    }

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

// Helper: check common per-user tool install locations that may not be PATH
static QString findCommonUserTool(const QString& exeName)
{
#ifdef Q_OS_WIN
    const QString home = QProcessEnvironment::systemEnvironment().value("USERPROFILE");
    const QString localAppData = QProcessEnvironment::systemEnvironment().value("LOCALAPPDATA");
    const QString programData = QProcessEnvironment::systemEnvironment().value("ProgramData");

    if (!home.isEmpty()) {
        // deno (~/.deno/bin)
        const QString denoPath = QDir(home).filePath(".deno/bin/" + exeName);
        if (QFileInfo::exists(denoPath)) return denoPath;

        // scoop shims (~\scoop\shims)
        const QString scoopPath = QDir(home).filePath("scoop/shims/" + exeName);
        if (QFileInfo::exists(scoopPath)) return scoopPath;
    }

    if (!localAppData.isEmpty()) {
        // pip-installed Python scripts (%LOCALAPPDATA%\Programs\Python\Python*\Scripts\)
        const QString pythonScriptsDir = QDir(localAppData).filePath("Programs/Python");
        if (QFileInfo::exists(pythonScriptsDir)) {
            QDir pyDir(pythonScriptsDir);
            const QFileInfoList entries = pyDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo &entry : entries) {
                const QString candidate = entry.filePath() + "/Scripts/" + exeName;
                if (QFileInfo::exists(candidate)) return candidate;
            }
        }

        // WindowsApps execution aliases (winget-installed tools that may not be in PATH)
        // These are 0-byte stubs that only work through shell alias resolution.
        // We still return the path here — the caller (BinariesPage) detects 0-byte
        // stubs and handles them by prepending WindowsApps to PATH instead of
        // invoking the stub directly.
        const QString windowsAppsDir = QDir(localAppData).filePath("Microsoft/WindowsApps");
        if (QFileInfo::exists(windowsAppsDir)) {
            const QString aliasPath = QDir(windowsAppsDir).filePath(exeName);
            if (QFileInfo::exists(aliasPath)) return aliasPath;
        }
    }

    if (!programData.isEmpty()) {
        // Chocolatey (C:\ProgramData\chocolatey\bin)
        const QString chocoPath = QDir(programData).filePath("chocolatey/bin/" + exeName);
        if (QFileInfo::exists(chocoPath)) return chocoPath;
    }
#else
    const QString home = QProcessEnvironment::systemEnvironment().value("HOME");
    if (!home.isEmpty()) {
        // deno (~/.deno/bin)
        const QString denoPath = QDir(home).filePath(".deno/bin/" + exeName);
        if (QFileInfo::exists(denoPath)) return denoPath;
    }
#endif
    return QString();
}

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
#ifdef Q_OS_WIN
    QString exeName = name + ".exe";
#else
    QString exeName = name;
#endif

    // Test QStandardPaths::findExecutable
    QString systemPath = QStandardPaths::findExecutable(exeName);

    qDebug() << "[ProcessUtils] resolveBinary:" << name << "- systemPath:" << systemPath;

    // 1. Check config override
    QString configKey = name + "_path";
    QString customPath = configManager->get("Binaries", configKey, "").toString().trimmed();

    if (!customPath.isEmpty()) {
        qDebug() << "[ProcessUtils] Custom path configured for" << name << ":" << customPath;
        if (!QFileInfo::exists(customPath)) {
            return {QDir::toNativeSeparators(customPath), "Invalid Custom"};
        }

        QString canonicalCustom = QFileInfo(customPath).canonicalFilePath();

        if (!systemPath.isEmpty() && canonicalCustom == QFileInfo(systemPath).canonicalFilePath()) {
            return {QDir::toNativeSeparators(systemPath), "System PATH"};
        }

        return {QDir::toNativeSeparators(customPath), "Custom"};
    }

    // 2. Check system PATH first (allows users to provide their own unbundled binaries)
    if (!systemPath.isEmpty()) {
        qDebug() << "[ProcessUtils] Found" << name << "in System PATH:" << systemPath;
        return {QDir::toNativeSeparators(systemPath), "System PATH"};
    }

    // 2b. Check common per-user tool locations (deno at ~/.deno/bin, etc.)
    //     These tools often install to a user-local path that isn't in PATH.
    QString userToolPath = findCommonUserTool(exeName);
    if (!userToolPath.isEmpty()) {
        qDebug() << "[ProcessUtils] Found" << name << "in User Local:" << userToolPath;
        return {QDir::toNativeSeparators(userToolPath), "User Local"};
    }

    qDebug() << "[ProcessUtils]" << name << "NOT FOUND - searching all PATH directories for" << exeName;
    
    // Final diagnostic: manually search PATH
    QString pathEnv = QProcessEnvironment::systemEnvironment().value("PATH");
    QStringList pathDirs = pathEnv.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    qDebug() << "[ProcessUtils] PATH contains" << pathDirs.size() << "directories";
    for (const QString& dir : pathDirs) {
        QString candidate = QDir(dir).filePath(exeName);
        if (QFileInfo::exists(candidate)) {
            qDebug() << "[ProcessUtils] FOUND" << name << "at" << candidate << "(in PATH dir:" << dir << ")";
            return {QDir::toNativeSeparators(candidate), "System PATH"};
        }
    }
    
    return {name, "Not Found"};
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
    env.insert("PYTHONUTF8", "1");
    env.insert("PYTHONIOENCODING", "utf-8");
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
        QProcess::startDetached("taskkill.exe", {"/PID", QString::number(pid), "/T", "/F"});
    }
    
    // Force immediate kill without waiting if we are aborting
    process->kill();
#else
    process->kill();
#endif
}

void sendGracefulInterrupt(qint64 pid) {
    if (pid <= 0) return;
    qInfo() << "[ProcessUtils] Sending graceful interrupt to PID" << pid;

#ifdef Q_OS_WIN
    if (AttachConsole(static_cast<DWORD>(pid))) {
        SetConsoleCtrlHandler(NULL, TRUE);
        GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
        FreeConsole();
        SetConsoleCtrlHandler(NULL, FALSE);
    } else {
        qWarning() << "[ProcessUtils] Failed to attach console to PID" << pid << "for graceful interrupt. Error:" << GetLastError();
    }
#else
    kill(pid, SIGINT);
#endif
}

QString fetchFfmpegVersion(const QString& execPath) {
    if (execPath.isEmpty() || !QFileInfo::exists(execPath)) {
        return "Not Found";
    }

    QProcess process;
    process.start(execPath, {"-version"});
    if (process.waitForFinished(3000)) {
        QString output = process.readAllStandardOutput();
        
        // Match specific clean patterns: YYYY-MM-DD, semantic version (e.g., 6.0, 4.4.1), or N-builds
        QRegularExpression re("(?:ffmpeg|ffprobe) version ([0-9]{4}-[0-9]{2}-[0-9]{2}|[0-9]+\\.[0-9]+(?:\\.[0-9]+)?|[Nn]-[0-9]+)");
        QRegularExpressionMatch match = re.match(output);
        if (match.hasMatch()) {
            return match.captured(1);
        }
        
        // Fallback: take the first sequence of characters before a hyphen or space
        QRegularExpression fallbackRe("(?:ffmpeg|ffprobe) version ([^- ]+)");
        QRegularExpressionMatch fallbackMatch = fallbackRe.match(output);
        if (fallbackMatch.hasMatch()) {
            return fallbackMatch.captured(1);
        }
    }
    return "Unknown";
}

} // namespace ProcessUtils
