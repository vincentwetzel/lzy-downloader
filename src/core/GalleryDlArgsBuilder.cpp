#include "GalleryDlArgsBuilder.h"
#include "core/ProcessUtils.h"

#include <QDir>
#include <QStandardPaths>

GalleryDlArgsBuilder::GalleryDlArgsBuilder(ConfigManager *configManager)
    : m_configManager(configManager) {
}

QStringList GalleryDlArgsBuilder::build(const QString &url, const QVariantMap &options) {
    QStringList args;
    args << QStringLiteral("--verbose");
    args << url;

    // Output path - gallery-dl's --directory sets the base download directory
    QString tempPath = m_configManager->get(QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory")).toString();
    if (tempPath.isEmpty()) {
        tempPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("LzyDownloader"));
    }
    tempPath = QDir(tempPath).filePath(options.value(QStringLiteral("id")).toString());
    QDir().mkpath(tempPath);
    args << QStringLiteral("--directory") << tempPath;

    // Filename template - gallery-dl's -f supports full path templates with '/'
    // It creates subdirectories relative to --directory automatically
    QString filenameTemplate = m_configManager->get(QStringLiteral("General"), QStringLiteral("gallery_output_template"), QStringLiteral("{category}/{subcategory}/{id}_{filename}.{extension}")).toString();
    args << QStringLiteral("-f") << filenameTemplate;

    // Cookies
    QString cookiesBrowser = m_configManager->get(QStringLiteral("General"), QStringLiteral("gallery_cookies_from_browser"), QStringLiteral("None")).toString();
    if (cookiesBrowser != QStringLiteral("None")) {
        args << QStringLiteral("--cookies-from-browser") << cookiesBrowser.toLower();
    }

    // External Downloader
    if (m_configManager->get(QStringLiteral("Metadata"), QStringLiteral("use_aria2c"), false).toBool()
        && ProcessUtils::findBinary(QStringLiteral("aria2c"), m_configManager).source != QStringLiteral("Not Found")) {
        args << QStringLiteral("-o") << QStringLiteral("downloader.program=aria2c");
    }

    // Rate Limit
    QString rateLimit = m_configManager->get(QStringLiteral("General"), QStringLiteral("rate_limit"), QStringLiteral("Unlimited")).toString();
    if (rateLimit != QStringLiteral("Unlimited")) {
        args << QStringLiteral("--limit-rate") << rateLimit.split(QLatin1Char(' ')).first();
    }

    // Override duplicate download check
    if (options.value(QStringLiteral("override_archive"), false).toBool()) {
        args << QStringLiteral("--no-skip");
    }

    // Restrict filenames
    if (m_configManager->get(QStringLiteral("General"), QStringLiteral("restrict_filenames"), false).toBool()) {
        args << QStringLiteral("--windows-filenames");
    }

    return args;
}
