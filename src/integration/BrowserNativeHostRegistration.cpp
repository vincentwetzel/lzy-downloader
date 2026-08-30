#include "BrowserNativeHostRegistration.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace {

constexpr auto kHostName = "com.lzydownloader.browser";

#ifdef LZY_BROWSER_EXTENSION_ID
#define LZY_STRINGIFY_VALUE(value) #value
#define LZY_STRINGIFY(value) LZY_STRINGIFY_VALUE(value)
const auto kBrowserExtensionId = QStringLiteral(LZY_STRINGIFY(LZY_BROWSER_EXTENSION_ID));
#undef LZY_STRINGIFY
#undef LZY_STRINGIFY_VALUE
#endif

QString shellQuote(const QString &value)
{
    QString quoted = value;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral("'") + quoted + QLatin1Char('\'');
}

#ifdef LZY_BROWSER_EXTENSION_ID
bool writeManifest(const QString &path, const QString &hostPath)
{
    const QJsonObject manifest{
        {QStringLiteral("name"), QString::fromLatin1(kHostName)},
        {QStringLiteral("description"), QStringLiteral("LzyDownloader local browser companion")},
        {QStringLiteral("path"), hostPath},
        {QStringLiteral("type"), QStringLiteral("stdio")},
        {QStringLiteral("allowed_origins"), QJsonArray{
            QStringLiteral("chrome-extension://") + kBrowserExtensionId + QStringLiteral("/")
        }}
    };

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)
        || file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        qWarning() << "[BrowserNativeHostRegistration] Failed to write manifest:" << path
                   << file.errorString();
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}
#endif

QString hostPathForRuntime()
{
    const QString applicationDir = QCoreApplication::applicationDirPath();
    const QString hostName =
#ifdef Q_OS_WIN
        QStringLiteral("LzyDownloaderBrowserHost.exe");
#else
        QStringLiteral("LzyDownloaderBrowserHost");
#endif
    const QString hostPath = QDir(applicationDir).filePath(hostName);

#ifdef Q_OS_LINUX
    // An AppImage mount disappears when the application exits. Keep a small
    // launcher in persistent app data that extracts the bundled host for each
    // browser invocation instead of registering the transient mount path.
    const QString appImageEnv = QProcessEnvironment::systemEnvironment().value(QStringLiteral("APPIMAGE"));
    const QString appImage = appImageEnv.isEmpty() ? QString() : QFileInfo(appImageEnv).absoluteFilePath();
    if (!appImage.isEmpty() && QFile::exists(appImage)) {
        const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (dataDir.isEmpty() || !QDir().mkpath(dataDir)) {
            qWarning() << "[BrowserNativeHostRegistration] Could not create AppImage host directory:" << dataDir;
            return {};
        }

        const QString wrapperPath = QDir(dataDir).filePath(QStringLiteral("browser-host-wrapper.sh"));
        const QString wrapper = QStringLiteral(
            "#!/bin/sh\n"
            "set -eu\n"
            "appimage=%1\n"
            "temp_dir=\"$(mktemp -d \"${TMPDIR:-/tmp}/lzy-downloader-browser-host.XXXXXX\")\"\n"
            "trap 'rm -rf \"$temp_dir\"' EXIT HUP INT TERM\n"
            "(cd \"$temp_dir\" && \"$appimage\" --appimage-extract >/dev/null)\n"
            "\"$temp_dir/squashfs-root/usr/bin/LzyDownloaderBrowserHost\" \"$@\"\n"
            "status=$?\n"
            "rm -rf \"$temp_dir\"\n"
            "exit $status\n").arg(shellQuote(appImage));
        QSaveFile wrapperFile(wrapperPath);
        if (!wrapperFile.open(QIODevice::WriteOnly | QIODevice::Text)
            || wrapperFile.write(wrapper.toUtf8()) < 0
            || !wrapperFile.commit()) {
            qWarning() << "[BrowserNativeHostRegistration] Failed to write AppImage host wrapper:" << wrapperPath;
            return {};
        }
        QFile::setPermissions(wrapperPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        return wrapperPath;
    }
#endif

    return QFile::exists(hostPath) ? hostPath : QString();
}

QStringList manifestDirectories()
{
#if defined(Q_OS_MACOS)
    const QString chromeData = QDir::home().filePath(QStringLiteral("Library/Application Support"));
    return {
        QDir(chromeData).filePath(QStringLiteral("Google/Chrome/NativeMessagingHosts")),
        QDir(chromeData).filePath(QStringLiteral("Chromium/NativeMessagingHosts"))
    };
#elif defined(Q_OS_LINUX)
    QString configHome = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (configHome.isEmpty() || !QDir::isAbsolutePath(configHome)) {
        configHome = QDir::home().filePath(QStringLiteral(".config"));
    }
    return {
        QDir(configHome).filePath(QStringLiteral("google-chrome/NativeMessagingHosts")),
        QDir(configHome).filePath(QStringLiteral("chromium/NativeMessagingHosts"))
    };
#else
    return {};
#endif
}

} // namespace

namespace BrowserNativeHostRegistration {

void registerHostIfConfigured()
{
#ifndef LZY_BROWSER_EXTENSION_ID
    return;
#else
    const QString hostPath = hostPathForRuntime();
    if (hostPath.isEmpty()) {
        qWarning() << "[BrowserNativeHostRegistration] Configured browser host executable was not found.";
        return;
    }

    const QString manifestFileName = QString::fromLatin1(kHostName) + QStringLiteral(".json");
#ifdef Q_OS_WIN
    QString manifestPath = QDir(QCoreApplication::applicationDirPath()).filePath(manifestFileName);
    if (!writeManifest(manifestPath, hostPath)) {
        const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (dataDir.isEmpty() || !QDir().mkpath(dataDir)) {
            qWarning() << "[BrowserNativeHostRegistration] Failed to create fallback manifest directory:" << dataDir;
            return;
        }
        manifestPath = QDir(dataDir).filePath(manifestFileName);
        if (!writeManifest(manifestPath, hostPath)) {
            return;
        }
    }

    for (const QString &browser : {QStringLiteral("Google\\Chrome"), QStringLiteral("Chromium")}) {
        QSettings registry(
            QStringLiteral("HKEY_CURRENT_USER\\Software\\%1\\NativeMessagingHosts\\%2")
                .arg(browser, QString::fromLatin1(kHostName)),
            QSettings::NativeFormat);
        registry.setValue(QStringLiteral("."), manifestPath);
        registry.sync();
        if (registry.status() != QSettings::NoError) {
            qWarning() << "[BrowserNativeHostRegistration] Failed to register" << browser
                       << registry.status();
        }
    }
#else
    for (const QString &directory : manifestDirectories()) {
        if (!QDir().mkpath(directory)) {
            qWarning() << "[BrowserNativeHostRegistration] Failed to create browser manifest directory:" << directory;
            continue;
        }
        writeManifest(QDir(directory).filePath(manifestFileName), hostPath);
    }
#endif
#endif
}

} // namespace BrowserNativeHostRegistration
