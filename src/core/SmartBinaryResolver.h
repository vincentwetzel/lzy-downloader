#pragma once

#include <QString>
#include <QStringList>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QDebug>
#include "core/ProcessUtils.h"
#include <QFile>
#include <QTextStream>
#include "core/VersionParser.h"
#include "core/ConfigManager.h"

namespace SmartBinaryResolver {

/**
 * @brief Direct INI key reader to bypass Windows Registry fallbacks in QSettings.
 */
inline void dumpFullFile(const QString &filePath) {
    qDebug() << "[SmartBinaryResolver::dumpFullFile] === START FULL FILE DUMP of" << filePath << "===";
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[SmartBinaryResolver::dumpFullFile] Failed to open file for full dump:" << file.errorString();
        return;
    }
    QTextStream in(&file);
    int lineNum = 1;
    while (!in.atEnd()) {
        qDebug().noquote() << QStringLiteral("[Line %1]:").arg(lineNum++) << in.readLine();
    }
    qDebug() << "[SmartBinaryResolver::dumpFullFile] === END FULL FILE DUMP ===";
}

inline QString readIniKeyDirect(const QString &filePath, const QString &section, const QString &key) {
    qDebug() << "[SmartBinaryResolver::readIniKeyDirect] >>> DIRECT READ START <<<";
    qDebug() << "[SmartBinaryResolver::readIniKeyDirect] Target File:" << filePath;
    qDebug() << "[SmartBinaryResolver::readIniKeyDirect] Target Section:" << section << "Target Key:" << key;

    dumpFullFile(filePath);

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[SmartBinaryResolver::readIniKeyDirect] Failed to open file directly!" << file.errorString();
        return QString();
    }

    QString currentSection;
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        qDebug() << "[SmartBinaryResolver::readIniKeyDirect] Raw line read:" << line;
        if (line.isEmpty() || line.startsWith(QLatin1Char(';')) || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            currentSection = line.mid(1, line.length() - 2).trimmed();
            qDebug() << "[SmartBinaryResolver::readIniKeyDirect] Entered Section [" << currentSection << "]";
            continue;
        }
        if (currentSection.compare(section, Qt::CaseInsensitive) == 0) {
            int eqIdx = line.indexOf(QLatin1Char('='));
            if (eqIdx != -1) {
                QString k = line.left(eqIdx).trimmed();
                if (k.compare(key, Qt::CaseInsensitive) == 0) {
                    QString val = line.mid(eqIdx + 1).trimmed();
                    qDebug() << "[SmartBinaryResolver::readIniKeyDirect] MATCH FOUND! Key:" << k << "Value (raw):" << val;
                    val.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
                    if (val.startsWith(QLatin1Char('"')) && val.endsWith(QLatin1Char('"'))) {
                        val = val.mid(1, val.length() - 2);
                    } else if (val.startsWith(QLatin1Char('\'')) && val.endsWith(QLatin1Char('\''))) {
                        val = val.mid(1, val.length() - 2);
                    }
                    qDebug() << "[SmartBinaryResolver::readIniKeyDirect] Returning sanitized value:" << val;
                    return val;
                }
            }
        }
    }
    qDebug() << "[SmartBinaryResolver::readIniKeyDirect] Key" << key << "not found under section" << section;
    return QString();
}

/**
 * @brief Dynamically resolves the highest-version executable across all candidate directories.
 * 
 * Prioritizes: Manual Override > Local App bin/ > System PATH > Package Managers.
 * Probes candidates concurrently with a fast timeout to prevent GUI thread freezing.
 */
inline ProcessUtils::FoundBinary resolve(const QString& binaryName, ConfigManager* configManager) {
    qInfo() << "[SmartBinaryResolver::resolve] =========================================";
    qInfo() << "[SmartBinaryResolver::resolve] Starting binary resolution for:" << binaryName;

    ProcessUtils::FoundBinary result;
    result.path = QString();
    result.source = QStringLiteral("Not Found");

    if (!configManager) {
        qWarning() << "[SmartBinaryResolver::resolve] ConfigManager is null! Aborting.";
        return result;
    }

    // 1. Check for manual override in settings.ini
    const QString configKey = binaryName + QStringLiteral("_path");
    const QString iniPath = QDir(configManager->getConfigDir()).filePath(QStringLiteral("settings.ini"));
    qInfo() << "[SmartBinaryResolver::resolve] Reading override path directly from settings.ini:";
    qInfo() << "[SmartBinaryResolver::resolve]   Config Directory:" << configManager->getConfigDir();
    qInfo() << "[SmartBinaryResolver::resolve]   settings.ini Path:" << iniPath;

    const QString overridePath = readIniKeyDirect(iniPath, QStringLiteral("Binaries"), configKey);
    qInfo() << "[SmartBinaryResolver::resolve] Direct read result for key" << configKey << "is:" << (overridePath.isEmpty() ? "<EMPTY>" : overridePath);

    // Discrepancy & Registry Ghost Check
    const QString configManagerValue = configManager->get(QStringLiteral("Binaries"), configKey).toString();
    qInfo() << "[SmartBinaryResolver::resolve] ConfigManager value for" << configKey << "is:" << (configManagerValue.isEmpty() ? "<EMPTY>" : configManagerValue);

    if (overridePath.isEmpty() && !configManagerValue.isEmpty()) {
        qWarning() << "[SmartBinaryResolver::resolve] !!! DISCREPANCY DETECTED !!!";
        qWarning() << "[SmartBinaryResolver::resolve]   settings.ini on disk has NO entry, but ConfigManager/Registry has:" << configManagerValue;
        qWarning() << "[SmartBinaryResolver::resolve]   This is a Registry ghost! Purging from ConfigManager memory to prevent write-back...";
        configManager->set(QStringLiteral("Binaries"), configKey, QString());
        configManager->remove(QStringLiteral("Binaries"), configKey);
        configManager->save();
        qWarning() << "[SmartBinaryResolver::resolve]   Purge completed successfully.";
    }

    if (!overridePath.isEmpty()) {
        QFileInfo fileInfo(overridePath);
        qInfo() << "[SmartBinaryResolver::resolve] Checking overridePath file properties: exists:" << fileInfo.exists() << "isFile:" << fileInfo.isFile() << "isExecutable:" << fileInfo.isExecutable();
        if (fileInfo.exists() && fileInfo.isFile() && fileInfo.isExecutable()) {
            result.path = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
            result.source = QStringLiteral("Custom");
            qInfo() << "[SmartBinaryResolver::resolve] >>> RESOLVED TO MANUAL OVERRIDE (CUSTOM) <<< Path:" << result.path;
            return result;
        } else {
            result.path = overridePath;
            result.source = QStringLiteral("Invalid Custom");
            qWarning() << "[SmartBinaryResolver::resolve] >>> INVALID MANUAL OVERRIDE PATH <<< Path:" << overridePath;
            return result;
        }
    }

        // 2. Collect candidate directories, starting with the local app bin folder
    const QString localBinDir = QDir(configManager->getConfigDir()).filePath(QStringLiteral("bin"));
    const QString exeExtension = QStringLiteral(".exe");
    QStringList searchDirs;

        // Check Local app bin directory as a candidate (treated as first priority/tie-breaker)
        searchDirs.append(localBinDir);

    // Priority 2: System PATH environmental variable
    const QStringList pathDirs = QProcessEnvironment::systemEnvironment().value(QStringLiteral("PATH"))
                                    .split(QDir::listSeparator(), Qt::SkipEmptyParts);
    qInfo() << "[SmartBinaryResolver::resolve] Collected system PATH directories. Total Count:" << pathDirs.size();
    searchDirs.append(pathDirs);

    // Priority 3: Common package manager fallbacks
    const QString localAppData = QProcessEnvironment::systemEnvironment().value(QStringLiteral("LOCALAPPDATA"));
    qInfo() << "[SmartBinaryResolver::resolve] LOCALAPPDATA environment variable:" << (localAppData.isEmpty() ? "<EMPTY>" : localAppData);
    if (!localAppData.isEmpty()) {
        searchDirs.append(QDir(localAppData).filePath(QStringLiteral("Microsoft/WindowsApps")));
        searchDirs.append(QDir(localAppData).filePath(QStringLiteral("Programs/Python/Python314/Scripts")));
        searchDirs.append(QDir(localAppData).filePath(QStringLiteral("Programs/Python/Python313/Scripts")));
        searchDirs.append(QDir(localAppData).filePath(QStringLiteral("Programs/Python/Python312/Scripts")));
        searchDirs.append(QDir(localAppData).filePath(QStringLiteral("Programs/Python/Python311/Scripts")));
    }

    // Find all candidate executable paths
    QStringList candidates;
    qInfo() << "[SmartBinaryResolver::resolve] Scanning search directories for" << binaryName << "...";

    for (const QString& dirPath : searchDirs) {
        QDir dir(dirPath);
        if (!dir.exists()) continue;

        QString absolutePath;
#ifdef Q_OS_WIN
        QString targetFile = binaryName;
        if (!targetFile.endsWith(exeExtension, Qt::CaseInsensitive)) {
            targetFile += exeExtension;
        }
        if (dir.exists(targetFile)) {
            absolutePath = dir.absoluteFilePath(targetFile);
        }
#else
        if (dir.exists(binaryName)) {
            absolutePath = dir.absoluteFilePath(binaryName);
        }
#endif
        if (!absolutePath.isEmpty()) {
            QFileInfo info(absolutePath);
            if (info.exists() && info.isFile() && info.isExecutable()) {
                QString nativePath = QDir::toNativeSeparators(info.absoluteFilePath());
                if (!candidates.contains(nativePath)) {
                    qInfo() << "[SmartBinaryResolver::resolve]   FOUND CANDIDATE:" << nativePath << "in directory:" << dirPath;
                    candidates.append(nativePath);
                }
            }
        }
    }

    qInfo() << "[SmartBinaryResolver::resolve] Total candidates discovered:" << candidates.size();

    if (candidates.isEmpty()) {
        qWarning() << "[SmartBinaryResolver::resolve] >>> RESOLUTION FAILED: No candidates found anywhere <<<";
        return result;
    }

    // If only one candidate found, no need to probe versions
    if (candidates.size() == 1) {
        result.path = candidates.first();
        if (result.path.startsWith(QDir::toNativeSeparators(localBinDir), Qt::CaseInsensitive)) {
            result.source = QStringLiteral("Local App Directory");
        } else {
            result.source = QStringLiteral("System PATH");
        }
        return result;
    }

    // 3. Version-Aware Smart Evaluation
    QString bestPath;
    Version bestVersion = Version::parse(QStringLiteral("0.0.0"));
    QString bestSource = QStringLiteral("Not Found");

    for (const QString& path : candidates) {
        QProcess process;
        ProcessUtils::setProcessEnvironment(process);
        process.setProcessChannelMode(QProcess::MergedChannels);

        const QString argVersion = (binaryName == QStringLiteral("ffmpeg") || binaryName == QStringLiteral("ffprobe")) 
                                    ? QStringLiteral("-version") : QStringLiteral("--version");

        process.start(path, {argVersion});
        qInfo() << "[SmartBinaryResolver] Probing" << binaryName << "at path:" << path;
        if (process.waitForFinished(5000)) { // Increased timeout to 5 seconds
            qInfo() << "[SmartBinaryResolver]   Probe for" << path << "finished.";
            const QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
            const QString firstLine = output.split(QLatin1Char('\n')).first().trimmed();
            QString rawVersion;

            // Generic match for standard dotted semantic versions or YYYY-MM-DD/YYYY.MM.DD dates
            QRegularExpression verRe(QStringLiteral(R"((\d+(?:\.\d+)+|\d{4}-\d{2}-\d{2}))"));
            QRegularExpressionMatch match = verRe.match(firstLine);
            if (match.hasMatch()) {
                rawVersion = match.captured(1);
            } else {
                rawVersion = firstLine;
            }

            Version parsedVersion = Version::parse(rawVersion);

            // Heuristic for FFmpeg/FFprobe: Map year-based snapshot versions (>= 2000)
            // to equivalent semantic major versions (Year - 2017) to allow fair comparison
            // with true semantic releases (like 8.1.1 or 7.0).
            if (binaryName == QStringLiteral("ffmpeg") || binaryName == QStringLiteral("ffprobe")) {
                static const QRegularExpression yearRe(QStringLiteral(R"(^(20\d{2})([\.-]))"));
                QRegularExpressionMatch yearMatch = yearRe.match(rawVersion);
                if (yearMatch.hasMatch()) {
                    int year = yearMatch.captured(1).toInt();
                    int virtualMajor = year - 2017;
                    QString mappedVersion = rawVersion;
                    mappedVersion.replace(0, 4, QString::number(virtualMajor));
                    parsedVersion = Version::parse(mappedVersion);
                    qInfo() << "[SmartBinaryResolver]   FFmpeg year-based version detected:" << rawVersion 
                            << "-> Mapped to virtual semantic version:" << parsedVersion.toString();
                }
            }
            qInfo() << "[SmartBinaryResolver]   Path:" << path << "Raw version output:" << firstLine << "Parsed version:" << parsedVersion.toString();

            if (bestPath.isEmpty() || parsedVersion > bestVersion) {
                bestPath = path;
                bestVersion = parsedVersion;
                if (path.startsWith(QDir::toNativeSeparators(localBinDir), Qt::CaseInsensitive)) {
                    bestSource = QStringLiteral("App Directory");
                } else {
                    bestSource = QStringLiteral("System PATH");
                }
                qInfo() << "[SmartBinaryResolver]   New best version found:" << bestVersion.toString() << "at" << bestPath;
            }
        } else {
            qWarning() << "[SmartBinaryResolver]   Version probe for" << path << "timed out or failed to finish.";
            if (process.state() != QProcess::NotRunning) {
                process.kill();
                process.waitForFinished(1000); // Give it a moment to die
            }
        }
    }

    qInfo() << "[SmartBinaryResolver::resolve] Smart evaluation complete for" << binaryName;
    qInfo() << "[SmartBinaryResolver::resolve]   Best path resolved:" << (bestPath.isEmpty() ? "<NONE>" : bestPath) << "Version:" << bestVersion.toString() << "Source:" << bestSource;

    if (bestPath.isEmpty() && !candidates.isEmpty()) {
        bestPath = candidates.first();
        if (bestPath.startsWith(QDir::toNativeSeparators(localBinDir), Qt::CaseInsensitive)) {
            bestSource = QStringLiteral("App Directory");
        } else {
            bestSource = QStringLiteral("System PATH");
        }
    }

    result.path = bestPath;
    result.source = bestSource;
    return result;
}

} // namespace SmartBinaryResolver