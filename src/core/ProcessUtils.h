#pragma once
#include <QString>
#include <QHash>

class ConfigManager;
class QProcess;

namespace ProcessUtils {
    struct FoundBinary {
        QString path;
        QString source; // "Custom", "Bundled", "System PATH", or "Not Found"
    };
    FoundBinary findBinary(const QString& name, ConfigManager* configManager);
    FoundBinary resolveBinary(const QString& name, ConfigManager* configManager);
    void setProcessEnvironment(QProcess &process);
    void terminateProcessTree(QProcess *process, int gracefulTimeoutMs = 2000);
    void sendGracefulInterrupt(qint64 pid);

    QString fetchFfmpegVersion(const QString& execPath);

    // Cache management
    void clearCache();
    void cacheBinary(const QString& name, const FoundBinary& found);
    FoundBinary getCachedBinary(const QString& name);
    bool hasCachedBinary(const QString& name);
}