#include "GlobalDownloadLimiter.h"

#include <algorithm>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {
constexpr int kLockTimeoutMs = 100;
constexpr int kStaleLockTimeMs = 5000;
}

GlobalDownloadLimiter::GlobalDownloadLimiter(const QString &namespaceKey)
    : m_processId(QCoreApplication::applicationPid())
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir dataDirectory(dataDir);
    if ((!dataDirectory.exists() && !dataDirectory.mkpath(QStringLiteral(".")))
        || !QFileInfo(dataDir).isWritable()) {
        // Some test sandboxes and locked-down installations do not permit a
        // new directory below AppLocalDataLocation. Keep the coordination
        // functional with a per-user temporary fallback in that case.
        const QString fallbackDir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("LzyDownloader"));
        dataDir = fallbackDir;
        dataDirectory = QDir(fallbackDir);
        if (!dataDirectory.exists()) {
            dataDirectory.mkpath(QStringLiteral("."));
        }
    }

    const QByteArray namespaceHash = QCryptographicHash::hash(
        namespaceKey.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    const QString stem = QStringLiteral("global_download_slots_%1").arg(QString::fromLatin1(namespaceHash));
    m_lockPath = QDir(dataDir).filePath(stem + QStringLiteral(".lock"));
    m_statePath = QDir(dataDir).filePath(stem + QStringLiteral(".json"));
}

GlobalDownloadLimiter::~GlobalDownloadLimiter()
{
    releaseAll();
}

bool GlobalDownloadLimiter::tryAcquire(int maximumSlots)
{
    if (maximumSlots < 1) {
        return false;
    }

    const bool acquired = withLockedState([this, maximumSlots](QList<Holder> &holders) {
        removeStaleHolders(holders);

        int totalSlots = 0;
        for (const Holder &holder : holders) {
            totalSlots += holder.slotCount;
        }
        if (totalSlots >= maximumSlots) {
            return false;
        }

        auto holderIt = std::find_if(holders.begin(), holders.end(), [this](const Holder &holder) {
            return holder.processId == m_processId;
        });
        if (holderIt == holders.end()) {
            holders.append({m_processId, 1});
        } else {
            holderIt->slotCount++;
        }
        return true;
    });

    if (acquired) {
        ++m_reservedSlots;
    }
    return acquired;
}

void GlobalDownloadLimiter::release()
{
    if (m_reservedSlots <= 0) {
        return;
    }

    const bool released = withLockedState([this](QList<Holder> &holders) {
        auto holderIt = std::find_if(holders.begin(), holders.end(), [this](const Holder &holder) {
            return holder.processId == m_processId;
        });
        if (holderIt == holders.end() || holderIt->slotCount <= 0) {
            return false;
        }

        --holderIt->slotCount;
        if (holderIt->slotCount == 0) {
            holders.erase(holderIt);
        }
        return true;
    });

    if (released) {
        --m_reservedSlots;
    }
}

void GlobalDownloadLimiter::releaseAll()
{
    while (m_reservedSlots > 0) {
        const int previousCount = m_reservedSlots;
        release();
        if (m_reservedSlots == previousCount) {
            // The registry may already have been cleaned up by another
            // process or may be inaccessible during shutdown.
            m_reservedSlots = 0;
        }
    }
}

bool GlobalDownloadLimiter::withLockedState(const std::function<bool(QList<Holder> &)> &operation)
{
    QLockFile lock(m_lockPath);
    lock.setStaleLockTime(kStaleLockTimeMs);
    if (!lock.tryLock(kLockTimeoutMs)) {
        return false;
    }

    QList<Holder> holders = readState();
    const bool result = operation(holders);
    bool persisted = true;
    if (result || !holders.isEmpty()) {
        persisted = writeState(holders);
    } else {
        persisted = !QFile::exists(m_statePath) || QFile::remove(m_statePath);
    }
    return result && persisted;
}

QList<GlobalDownloadLimiter::Holder> GlobalDownloadLimiter::readState() const
{
    QFile file(m_statePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        return {};
    }

    QList<Holder> holders;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        const qint64 processId = object.value(QStringLiteral("pid")).toInteger();
        const int slotCount = object.value(QStringLiteral("slots")).toInt();
        if (processId > 0 && slotCount > 0) {
            holders.append({processId, slotCount});
        }
    }
    return holders;
}

bool GlobalDownloadLimiter::writeState(const QList<Holder> &holders) const
{
    QJsonArray array;
    for (const Holder &holder : holders) {
        QJsonObject object;
        object.insert(QStringLiteral("pid"), holder.processId);
        object.insert(QStringLiteral("slots"), holder.slotCount);
        array.append(object);
    }

    QSaveFile file(m_statePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
    return file.commit();
}

void GlobalDownloadLimiter::removeStaleHolders(QList<Holder> &holders) const
{
    holders.erase(std::remove_if(holders.begin(), holders.end(), [](const Holder &holder) {
        return !isProcessAlive(holder.processId);
    }), holders.end());
}

bool GlobalDownloadLimiter::isProcessAlive(qint64 processId)
{
#ifdef Q_OS_WIN
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(processId));
    if (!process) {
        // Access denial still means that a process exists. Treat it as alive
        // so a permissions quirk cannot overcommit the user's bandwidth.
        return GetLastError() == ERROR_ACCESS_DENIED;
    }

    DWORD exitCode = 0;
    const bool queried = GetExitCodeProcess(process, &exitCode) != FALSE;
    CloseHandle(process);
    return queried && exitCode == STILL_ACTIVE;
#else
    if (kill(static_cast<pid_t>(processId), 0) == 0) {
        return true;
    }
    return errno == EPERM;
#endif
}
