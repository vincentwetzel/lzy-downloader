#include "BrowserUtils.h"
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QProcess>

namespace BrowserUtils {

QStringList getInstalledBrowsers() {
    QStringList browsers;

    // Common paths on Windows
    // Note: This is a basic check. A more robust check would query the registry.

    QString programFiles = qgetenv("ProgramFiles");
    QString programFilesX86 = qgetenv("ProgramFiles(x86)");
    QString localAppData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);

    auto checkBrowser = [&](const QString& browserId, const QStringList& paths) {
        for (const QString& path : paths) {
            if (QFile::exists(path)) {
                browsers << browserId;
                break;
            }
        }
    };

    checkBrowser("chrome", {
        QDir(programFiles).filePath("Google/Chrome/Application/chrome.exe"),
        QDir(programFilesX86).filePath("Google/Chrome/Application/chrome.exe")
    });

    checkBrowser("firefox", {
        QDir(programFiles).filePath("Mozilla Firefox/firefox.exe"),
        QDir(programFilesX86).filePath("Mozilla Firefox/firefox.exe")
    });

    checkBrowser("edge", {
        QDir(programFiles).filePath("Microsoft/Edge/Application/msedge.exe"),
        QDir(programFilesX86).filePath("Microsoft/Edge/Application/msedge.exe")
    });

    checkBrowser("opera", {
        QDir(localAppData).filePath("Programs/Opera/launcher.exe")
    });

    checkBrowser("brave", {
        QDir(programFiles).filePath("BraveSoftware/Brave-Browser/Application/brave.exe"),
        QDir(programFilesX86).filePath("BraveSoftware/Brave-Browser/Application/brave.exe")
    });

    checkBrowser("vivaldi", {
        QDir(localAppData).filePath("Vivaldi/Application/vivaldi.exe")
    });

    return browsers;
}

}
