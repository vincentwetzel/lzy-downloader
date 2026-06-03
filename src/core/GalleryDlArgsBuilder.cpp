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
    
    // Fallback to yt-dlp's browser cookie setting if the gallery-specific one is not set
    if (cookiesBrowser == QStringLiteral("None")) {
        cookiesBrowser = m_configManager->get(QStringLiteral("General"), QStringLiteral("cookies_from_browser"), QStringLiteral("None")).toString();
    }

    if (cookiesBrowser != QStringLiteral("None")) {
        args << QStringLiteral("--cookies-from-browser") << cookiesBrowser.toLower();
    }

    // Rate Limit
    QString rateLimit = options.value(QStringLiteral("rate_limit"), QStringLiteral("Unlimited")).toString();
    if (rateLimit == QStringLiteral("Unlimited")) {
        rateLimit = m_configManager->get(QStringLiteral("General"), QStringLiteral("rate_limit"), QStringLiteral("Unlimited")).toString();
    }

    if (rateLimit != QStringLiteral("Unlimited")) {
        QString formattedRate = rateLimit;
        formattedRate.replace(QLatin1String(" MB/s"), QLatin1String("M")).replace(QLatin1String(" KB/s"), QLatin1String("K")).replace(QLatin1Char(' '), QString());
        args << QStringLiteral("--limit-rate") << formattedRate;
    }

    // Override duplicate download check
    if (options.value(QStringLiteral("override_archive"), false).toBool()) {
        args << QStringLiteral("--no-skip");
    }

    // Restrict filenames
    if (m_configManager->get(QStringLiteral("General"), QStringLiteral("restrict_filenames"), false).toBool()) {
        args << QStringLiteral("--restrict-filenames");
    }

    args << url;

    return args;
}
