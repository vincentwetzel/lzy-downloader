#include "BrowserUtils.h"
#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace BrowserUtils {

QStringList getInstalledBrowsers() {
    QStringList browsers;

    auto checkBrowser = [&](const QString &browserId,
                            const QStringList &paths,
                            const QStringList &executables) {
        for (const QString& path : paths) {
            if (!path.isEmpty() && QFile::exists(path)) {
                browsers << browserId;
                return;
            }
        }

        for (const QString &executable : executables) {
            if (!QStandardPaths::findExecutable(executable).isEmpty()) {
                browsers << browserId;
                return;
            }
        }
    };

#ifdef Q_OS_WIN
    const QString programFiles = qEnvironmentVariable("ProgramFiles");
    const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)");
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");

    const auto under = [](const QString &root, const QString &relative) {
        return root.isEmpty() ? QString{} : QDir(root).filePath(relative);
    };

    checkBrowser("chrome", {
                     under(programFiles, "Google/Chrome/Application/chrome.exe"),
                     under(programFilesX86, "Google/Chrome/Application/chrome.exe")
                 }, {"chrome.exe", "google-chrome.exe"});

    checkBrowser("firefox", {
                     under(programFiles, "Mozilla Firefox/firefox.exe"),
                     under(programFilesX86, "Mozilla Firefox/firefox.exe")
                 }, {"firefox.exe"});

    checkBrowser("edge", {
                     under(programFiles, "Microsoft/Edge/Application/msedge.exe"),
                     under(programFilesX86, "Microsoft/Edge/Application/msedge.exe")
                 }, {"msedge.exe"});

    checkBrowser("opera", {
                     under(localAppData, "Programs/Opera/launcher.exe")
                 }, {"opera.exe"});

    checkBrowser("brave", {
                     under(programFiles, "BraveSoftware/Brave-Browser/Application/brave.exe"),
                     under(programFilesX86, "BraveSoftware/Brave-Browser/Application/brave.exe")
                 }, {"brave.exe"});

    checkBrowser("vivaldi", {
                     under(localAppData, "Vivaldi/Application/vivaldi.exe")
                 }, {"vivaldi.exe"});
#elif defined(Q_OS_MACOS)
    const QString userApplications = QDir::home().filePath("Applications");
    const auto appExecutable = [&](const QString &application,
                                   const QString &executable) {
        return QStringList{
            QDir("/Applications").filePath(application + ".app/Contents/MacOS/" + executable),
            QDir(userApplications).filePath(application + ".app/Contents/MacOS/" + executable)
        };
    };

    checkBrowser("chrome", appExecutable("Google Chrome", "Google Chrome"),
                 {"google-chrome"});
    checkBrowser("firefox", appExecutable("Firefox", "firefox"), {"firefox"});
    checkBrowser("edge", appExecutable("Microsoft Edge", "Microsoft Edge"),
                 {"microsoft-edge"});
    checkBrowser("opera", appExecutable("Opera", "launcher"), {"opera"});
    checkBrowser("brave", appExecutable("Brave Browser", "Brave Browser"),
                 {"brave-browser"});
    checkBrowser("vivaldi", appExecutable("Vivaldi", "Vivaldi"), {"vivaldi"});
#else
    checkBrowser("chrome", {},
                 {"google-chrome", "google-chrome-stable", "chromium", "chromium-browser"});
    checkBrowser("firefox", {}, {"firefox"});
    checkBrowser("edge", {}, {"microsoft-edge", "microsoft-edge-stable"});
    checkBrowser("opera", {}, {"opera"});
    checkBrowser("brave", {}, {"brave-browser", "brave"});
    checkBrowser("vivaldi", {}, {"vivaldi", "vivaldi-stable"});
#endif

    return browsers;
}

}
