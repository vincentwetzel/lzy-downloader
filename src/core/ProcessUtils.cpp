#include "ProcessUtils.h"

#include "core/ConfigManager.h"
#include "core/SmartBinaryResolver.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
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
#include <vector>

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

namespace {

struct ParsedVersion {
    bool isDate = false;
    QList<int> segments;
    QString raw;
};

#ifdef Q_OS_WIN
typedef DWORD (WINAPI *GetFileVersionInfoSizeW_t)(LPCWSTR, LPDWORD);
typedef BOOL (WINAPI *GetFileVersionInfoW_t)(LPCWSTR, DWORD, DWORD, LPVOID);
typedef BOOL (WINAPI *VerQueryValueW_t)(LPCVOID, LPCWSTR, LPVOID*, PUINT);

QString getFileVersionWindows(const QString &filePath) {
    HMODULE hVersion = LoadLibraryW(L"version.dll");
    if (!hVersion) return QString();

    auto pGetFileVersionInfoSizeW = reinterpret_cast<GetFileVersionInfoSizeW_t>(GetProcAddress(hVersion, "GetFileVersionInfoSizeW"));
    auto pGetFileVersionInfoW = reinterpret_cast<GetFileVersionInfoW_t>(GetProcAddress(hVersion, "GetFileVersionInfoW"));
    auto pVerQueryValueW = reinterpret_cast<VerQueryValueW_t>(GetProcAddress(hVersion, "VerQueryValueW"));

    if (!pGetFileVersionInfoSizeW || !pGetFileVersionInfoW || !pVerQueryValueW) {
        FreeLibrary(hVersion);
        return QString();
    }

    std::wstring wPath = QDir::toNativeSeparators(filePath).toStdWString();
    DWORD dummy;
    DWORD size = pGetFileVersionInfoSizeW(wPath.c_str(), &dummy);
    if (size == 0) {
        FreeLibrary(hVersion);
        return QString();
    }

    std::vector<BYTE> buffer(size);
    if (!pGetFileVersionInfoW(wPath.c_str(), 0, size, buffer.data())) {
        FreeLibrary(hVersion);
        return QString();
    }

    // 1. Try VS_FIXEDFILEINFO first for semantic version
    VS_FIXEDFILEINFO *info = nullptr;
    UINT infoSize = 0;
    if (pVerQueryValueW(buffer.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &infoSize) && infoSize > 0 && info) {
        // Try file version first (much more reliable for Gyan.dev FFmpeg/FFprobe builds)
        int major = HIWORD(info->dwFileVersionMS);
        int minor = LOWORD(info->dwFileVersionMS);
        int patch = HIWORD(info->dwFileVersionLS);
        if (major > 0) {
            FreeLibrary(hVersion);
            return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
        }

        // Fallback to product version
        major = HIWORD(info->dwProductVersionMS);
        minor = LOWORD(info->dwProductVersionMS);
        patch = HIWORD(info->dwProductVersionLS);
        if (major > 0) {
            FreeLibrary(hVersion);
            return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
        }
    }

    // 2. Fall back to StringFileInfo translation block for ProductVersion string
    struct LANGANDCODEPAGE {
        WORD wLanguage;
        WORD wCodePage;
    } *lpTranslate;
    UINT cbTranslate = 0;
    if (pVerQueryValueW(buffer.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<LPVOID*>(&lpTranslate), &cbTranslate) && cbTranslate > 0) {
        for (unsigned int i = 0; i < (cbTranslate / sizeof(struct LANGANDCODEPAGE)); i++) {
            wchar_t subBlock[50];
            swprintf(subBlock, 50, L"\\StringFileInfo\\%04x%04x\\ProductVersion", lpTranslate[i].wLanguage, lpTranslate[i].wCodePage);
            wchar_t *lpBuffer = nullptr;
            UINT dwBytes = 0;
            if (pVerQueryValueW(buffer.data(), subBlock, reinterpret_cast<LPVOID*>(&lpBuffer), &dwBytes) && dwBytes > 0 && lpBuffer) {
                QString verStr = QString::fromWCharArray(lpBuffer).trimmed();
                FreeLibrary(hVersion);
                return verStr;
            }
        }
    }
    FreeLibrary(hVersion);
    return QString();
}
#endif

QString fetchFfmpegVersion(const QString& execPath) {
    if (execPath.isEmpty() || !QFileInfo::exists(execPath)) {
        return QStringLiteral("Not Found");
    }

    QProcess process;
    process.start(execPath, QStringList{QStringLiteral("-version")});
    if (process.waitForFinished(3000)) {
        QString output = QString::fromUtf8(process.readAllStandardOutput());
        if (output.isEmpty()) {
            output = QString::fromUtf8(process.readAllStandardError());
        }

        // Match specific clean patterns: YYYY-MM-DD, semantic version (e.g., 6.0, 4.4.1), or N-builds
        if (output.contains(QLatin1String("version"))) {
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

ParsedVersion parseVersion(const QString &ver, const QString &binaryName) {
    ParsedVersion pv;
    pv.raw = ver;
    if (ver.isEmpty() || ver == QStringLiteral("Unknown") || ver == QStringLiteral("Not Found")) {
        return pv;
    }

    // Clean up non-version prefixes/suffixes
    QString cleanVer = ver.trimmed().toLower();
    if (cleanVer.startsWith(QLatin1Char('v'))) {
        cleanVer.remove(0, 1);
    }

    // Check if it is a date-based version (e.g., 2023-02-22)
    static const QRegularExpression dateRe(QStringLiteral(R"(^(\d{4})[-.](\d{2})[-.](\d{2}))"));
    QRegularExpressionMatch dateMatch = dateRe.match(cleanVer);
    if (dateMatch.hasMatch()) {
        pv.isDate = true;
        pv.segments << dateMatch.captured(1).toInt();
        pv.segments << dateMatch.captured(2).toInt();
        pv.segments << dateMatch.captured(3).toInt();
        return pv;
    }

    // Parse generic dotted segments
    static const QRegularExpression segmentRe(QStringLiteral(R"(\d+)"));
    auto it = segmentRe.globalMatch(cleanVer);
    while (it.hasNext()) {
        pv.segments << it.next().captured().toInt();
    }

    // If the first segment looks like a year (>= 1900)
    if (!pv.segments.isEmpty() && pv.segments.first() >= 1900) {
        pv.isDate = true;
    }

    return pv;
}

bool isNewer(const ParsedVersion &v1, const ParsedVersion &v2, const QString &binaryName) {
    // v1 is newer than v2?
    if (v1.segments.isEmpty()) return false;
    if (v2.segments.isEmpty()) return true;

    // For ffmpeg/ffprobe, handle date-based dev builds vs semantic releases
    // Date-based versions (YYYY-MM-DD) are development builds and are older than stable semantic releases
    if (binaryName == QStringLiteral("ffmpeg") || binaryName == QStringLiteral("ffprobe")) {
        if (v1.isDate != v2.isDate) {
            if (v1.isDate) {
                // v1 is date (dev build), v2 is semantic (stable)
                // Stable semantic versions 5.0+ are newer than dev builds
                int v2Major = v2.segments.first();
                qInfo() << "[ProcessUtils::isNewer] Comparing date-based dev build to semantic release:"
                        << "dev version is OLDER if stable version is" << v2Major << ">= 5";
                return v2Major < 5;  // date is newer ONLY if semantic version is < 5.0
            } else {
                // v1 is semantic (stable), v2 is date (dev build)
                // Stable semantic versions 5.0+ are newer than dev builds
                int v1Major = v1.segments.first();
                qInfo() << "[ProcessUtils::isNewer] Comparing semantic release to date-based dev build:"
                        << "semantic release is NEWER if version is" << v1Major << ">= 5";
                return v1Major >= 5;  // semantic is newer if version >= 5.0
            }
        }
    }

    // Standard segment-by-segment comparison
    int maxLen = qMax(v1.segments.size(), v2.segments.size());
    for (int i = 0; i < maxLen; ++i) {
        int seg1 = i < v1.segments.size() ? v1.segments[i] : 0;
        int seg2 = i < v2.segments.size() ? v2.segments[i] : 0;
        if (seg1 > seg2) return true;
        if (seg1 < seg2) return false;
    }

    return false;
}

QString queryBinaryVersion(const QString &binaryName, const QString &path) {
#ifdef Q_OS_WIN
    QString winVer = getFileVersionWindows(path);
    if (!winVer.isEmpty() && winVer != QStringLiteral("0.0.0") && winVer != QStringLiteral("0.0.0.0")) {
        qInfo() << "[ProcessUtils::queryBinaryVersion] Read Windows file version resource for" << path << ":" << winVer;
        return winVer;
    }
#endif

    if (binaryName == QStringLiteral("ffmpeg") || binaryName == QStringLiteral("ffprobe")) {
        return fetchFfmpegVersion(path);
    }

    QProcess process;
    QStringList args;
    args << QStringLiteral("--version");

    process.start(path, args);
    if (process.waitForFinished(1000)) {
        QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        if (output.isEmpty()) {
            output = QString::fromUtf8(process.readAllStandardError()).trimmed();
        }

        static const QRegularExpression genericRe(QStringLiteral(R"(\b\d+(\.\d+)+\b)"));
        QRegularExpressionMatch match = genericRe.match(output);
        if (match.hasMatch()) return match.captured(0);
    }
    return QStringLiteral("0.0.0");
}

} // namespace

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
    // 1. Check INI first
    if (configManager) {
        QString configKey = name + QStringLiteral("_path");
        QString configuredPath = configManager->get(QStringLiteral("Binaries"), configKey).toString();
        if (!configuredPath.isEmpty() && QFileInfo::exists(configuredPath)) {
            FoundBinary found;
            found.path = QDir::toNativeSeparators(configuredPath);
            found.source = QStringLiteral("Custom");
            return found;
        }
    }

    // 2. If not found in INI, scan all known locations in priority order:
    // - App-local paths (applicationDirPath, bin subdirectories)
    // - User AppData paths (LzyDownloader folder and bin subdirectories) - priority over system binaries
    // - System PATH directories - fallback for system-installed binaries
    // - Package manager directories (Scoop, Chocolatey, WinGet, Homebrew)
    QStringList searchDirs;

    // App-local paths
    searchDirs << QCoreApplication::applicationDirPath();
    searchDirs << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("bin"));

    // User AppData paths (Local and Roaming)
    QString appLocalData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!appLocalData.isEmpty()) {
        searchDirs << QDir(appLocalData).filePath(QStringLiteral("bin"));
        searchDirs << QDir(appLocalData).filePath(QStringLiteral("Server/bin"));
    }
    QString appRoamingData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appRoamingData.isEmpty()) {
        searchDirs << QDir(appRoamingData).filePath(QStringLiteral("bin"));
        searchDirs << QDir(appRoamingData).filePath(QStringLiteral("Server/bin"));
    }

#ifdef Q_OS_WIN
    QString localAppData = QProcessEnvironment::systemEnvironment().value(QStringLiteral("LOCALAPPDATA"));
    if (!localAppData.isEmpty()) {
        searchDirs << QDir(localAppData).filePath(QStringLiteral("LzyDownloader"));
        searchDirs << QDir(localAppData).filePath(QStringLiteral("LzyDownloader/bin"));
        searchDirs << QDir(localAppData).filePath(QStringLiteral("LzyDownloader/Server/bin"));
    }
    QString roamingAppData = QProcessEnvironment::systemEnvironment().value(QStringLiteral("APPDATA"));
    if (!roamingAppData.isEmpty()) {
        searchDirs << QDir(roamingAppData).filePath(QStringLiteral("LzyDownloader"));
        searchDirs << QDir(roamingAppData).filePath(QStringLiteral("LzyDownloader/bin"));
        searchDirs << QDir(roamingAppData).filePath(QStringLiteral("LzyDownloader/Server/bin"));
    }
#else
    searchDirs << QDir(QDir::homePath()).filePath(QStringLiteral(".local/share/LzyDownloader/bin"));
    searchDirs << QDir(QDir::homePath()).filePath(QStringLiteral(".local/share/LzyDownloader/Server/bin"));
#endif

    // System PATH directories
    QString pathEnv = QProcessEnvironment::systemEnvironment().value(QStringLiteral("PATH"));
#ifdef Q_OS_WIN
    QStringList pathDirs = pathEnv.split(QLatin1Char(';'), Qt::SkipEmptyParts);
#else
    QStringList pathDirs = pathEnv.split(QLatin1Char(':'), Qt::SkipEmptyParts);
#endif
    for (const QString &dir : pathDirs) {
        searchDirs << dir;
    }

    // Common package manager / platform-specific directories
#ifdef Q_OS_WIN
    QString userProfile = QProcessEnvironment::systemEnvironment().value(QStringLiteral("USERPROFILE"));
    if (!userProfile.isEmpty()) {
        searchDirs << QDir(userProfile).filePath(QStringLiteral("scoop/shims"));
        searchDirs << QDir(userProfile).filePath(QStringLiteral("scoop/apps/ffmpeg/current/bin"));
        searchDirs << QDir(userProfile).filePath(QStringLiteral("scoop/apps/yt-dlp/current"));
        searchDirs << QDir(userProfile).filePath(QStringLiteral("scoop/apps/gallery-dl/current"));
        searchDirs << QDir(userProfile).filePath(QStringLiteral(".deno/bin"));
    }
    QString programData = QProcessEnvironment::systemEnvironment().value(QStringLiteral("ProgramData"));
    if (!programData.isEmpty()) {
        searchDirs << QDir(programData).filePath(QStringLiteral("chocolatey/bin"));
    }
    if (!localAppData.isEmpty()) {
        searchDirs << QDir(localAppData).filePath(QStringLiteral("Microsoft/WindowsApps"));
    }

    // Common manual installation folders on Windows
    searchDirs << QStringLiteral("C:\\ffmpeg\\bin");
    searchDirs << QStringLiteral("C:\\ffmpeg");
    searchDirs << QStringLiteral("C:\\Program Files\\ffmpeg\\bin");
    searchDirs << QStringLiteral("C:\\Program Files (x86)\\ffmpeg\\bin");
#else
    searchDirs << QStringLiteral("/usr/local/bin");
    searchDirs << QStringLiteral("/usr/bin");
    searchDirs << QStringLiteral("/bin");
    searchDirs << QStringLiteral("/opt/homebrew/bin");
    searchDirs << QDir(QDir::homePath()).filePath(QStringLiteral(".local/bin"));
    searchDirs << QDir(QDir::homePath()).filePath(QStringLiteral("bin"));
    searchDirs << QDir(QDir::homePath()).filePath(QStringLiteral(".deno/bin"));
#endif

    // Deduplicate search directories while preserving order
    QStringList uniqueDirs;
    for (const QString &dir : searchDirs) {
        QString cleanDir = QDir::cleanPath(dir);
        if (!uniqueDirs.contains(cleanDir) && QDir(cleanDir).exists()) {
            uniqueDirs << cleanDir;
        }
    }

    QString exeName = name;
#ifdef Q_OS_WIN
    if (!exeName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
        exeName += QStringLiteral(".exe");
    }
#endif

    QStringList candidatePaths;
    for (const QString &dir : uniqueDirs) {
        QString fullPath = QDir(dir).filePath(exeName);
        if (QFileInfo::exists(fullPath) && QFileInfo(fullPath).isFile()) {
#ifdef Q_OS_WIN
            candidatePaths << QDir::toNativeSeparators(fullPath);
#else
            if (QFileInfo(fullPath).isExecutable()) {
                candidatePaths << QDir::toNativeSeparators(fullPath);
            }
#endif
        }
    }

    if (candidatePaths.isEmpty()) {
        FoundBinary notFound;
        notFound.path = QString();
        notFound.source = QStringLiteral("Not Found");
        qWarning() << "[ProcessUtils::resolveBinary] No candidates found for" << name;
        return notFound;
    }

    qInfo() << "[ProcessUtils::resolveBinary] Discovered candidates for" << name << "in priority order:" << candidatePaths;

    // 3. Find the newest candidate among all found binaries
    QString bestPath = candidatePaths.first();
    QString bestVerStr = queryBinaryVersion(name, bestPath);
    ParsedVersion bestVersion = parseVersion(bestVerStr, name);
    qInfo() << "[ProcessUtils::resolveBinary] Initial best candidate for" << name << ":" << bestPath << "version:" << bestVerStr;

    for (int i = 1; i < candidatePaths.size(); ++i) {
        QString currentPath = candidatePaths.at(i);
        QString currentVerStr = queryBinaryVersion(name, currentPath);
        ParsedVersion currentVersion = parseVersion(currentVerStr, name);
        qInfo() << "[ProcessUtils::resolveBinary] Evaluating candidate:" << currentPath << "version:" << currentVerStr;

        bool preferCurrent = false;
        if (currentVersion.segments.isEmpty() != bestVersion.segments.isEmpty()) {
            // One has an unknown/unparsed version and the other has a known version.
            // If the unknown candidate lives in LzyDownloader AppData and the known best is system PATH (e.g. C:\ffmpeg),
            // we prefer AppData as it represents our up-to-date downloaded binary.
            if (currentVersion.segments.isEmpty() && currentPath.toLower().contains(QStringLiteral("lzydownloader"))) {
                preferCurrent = true;
            } else if (bestVersion.segments.isEmpty() && bestPath.toLower().contains(QStringLiteral("lzydownloader"))) {
                preferCurrent = false;
            } else {
                preferCurrent = isNewer(currentVersion, bestVersion, name);
            }
        } else {
            preferCurrent = isNewer(currentVersion, bestVersion, name);
        }

        // Fallback to AppData preference if versions are equal or both are unknown
        if (!preferCurrent && currentVersion.segments == bestVersion.segments) {
            if (currentPath.toLower().contains(QStringLiteral("lzydownloader")) && !bestPath.toLower().contains(QStringLiteral("lzydownloader"))) {
                preferCurrent = true;
            }
        }

        if (preferCurrent) {
            qInfo() << "[ProcessUtils::resolveBinary] Candidate" << currentPath << "(" << currentVerStr << ") is preferred over" << bestPath << "(" << bestVerStr << ")";
            bestPath = currentPath;
            bestVerStr = currentVerStr;
            bestVersion = currentVersion;
        }
    }
    qInfo() << "[ProcessUtils::resolveBinary] Selected final best candidate for" << name << ":" << bestPath << "version:" << bestVerStr;

    // Determine source based on path properties
    QString bestPathLower = bestPath.toLower();
    QString source = QStringLiteral("System PATH");
    if (bestPathLower.contains(QCoreApplication::applicationDirPath().toLower())) {
        source = QStringLiteral("App Directory");
    } else if (bestPathLower.contains(QStringLiteral("scoop"))) {
        source = QStringLiteral("Scoop");
    } else if (bestPathLower.contains(QStringLiteral("chocolatey")) || bestPathLower.contains(QStringLiteral("choco"))) {
        source = QStringLiteral("Chocolatey");
    } else if (bestPathLower.contains(QStringLiteral("windowsapps"))) {
        source = QStringLiteral("WinGet");
    } else if (bestPathLower.contains(QStringLiteral("homebrew"))) {
        source = QStringLiteral("Homebrew");
    } else if (bestPathLower.contains(QStringLiteral("lzydownloader"))) {
        source = QStringLiteral("User AppData");
    }

    FoundBinary found;
    found.path = bestPath;
    found.source = source;
    return found;
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

} // namespace ProcessUtils
