#include "BinaryFinder.h"
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>
#include <QProcessEnvironment>

QStringList BinaryFinder::getExtendedSearchPaths() {
    QStringList paths;

    // 1. System PATH
    QString systemPath = QProcessEnvironment::systemEnvironment().value("PATH");
    if (!systemPath.isEmpty()) {
        paths << systemPath.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    }

    // 2. Common Package Manager & Standard Installation Paths
#ifdef Q_OS_WIN
    QString localData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QString programFiles = qgetenv("ProgramFiles");
    if (programFiles.isEmpty()) programFiles = "C:/Program Files";
    QString programData = qgetenv("ProgramData");
    if (programData.isEmpty()) programData = "C:/ProgramData";
    
    // winget
    if (!localData.isEmpty()) {
        paths << QDir(localData).filePath("Microsoft/WindowsApps");
    }
    // scoop
    paths << QDir(QDir::homePath()).filePath("scoop/shims");
    // chocolatey
    paths << QDir(programData).filePath("chocolatey/bin");
    
    // Standard Program Files fallbacks
    paths << QDir(programFiles).filePath("ffmpeg/bin");
    paths << QDir(programFiles).filePath("aria2");
    paths << QDir(programFiles).filePath("yt-dlp");
#else
    // macOS / Linux package managers
    paths << "/usr/local/bin";                 // Homebrew (Intel) / standard source installs
    paths << "/opt/homebrew/bin";              // Homebrew (Apple Silicon)
    paths << "/opt/local/bin";                 // MacPorts
    paths << QDir(QDir::homePath()).filePath(".local/bin"); // pip user installs / pipx
#endif

    return paths;
}

QString BinaryFinder::findBinary(const QString& binaryName) {
    QString executableName = binaryName;
#ifdef Q_OS_WIN
    if (!executableName.endsWith(".exe", Qt::CaseInsensitive)) {
        executableName += ".exe";
    }
#endif

    QStringList searchPaths = getExtendedSearchPaths();
    QString foundPath = QStandardPaths::findExecutable(executableName, searchPaths);

    return QDir::toNativeSeparators(foundPath);
}

QMap<QString, QString> BinaryFinder::findAllBinaries() {
    QMap<QString, QString> results;
    QStringList trackedBinaries = {"yt-dlp", "ffmpeg", "ffprobe", "gallery-dl", "deno", "aria2c"};

    for (const QString& bin : trackedBinaries) {
        results[bin] = findBinary(bin);
    }

    return results;
}