#include "GalleryDlArgsBuilder.h"
#include "core/ProcessUtils.h"

#include <QDir>
#include <QStandardPaths>

GalleryDlArgsBuilder::GalleryDlArgsBuilder(ConfigManager *configManager)
    : m_configManager(configManager) {
}

QStringList GalleryDlArgsBuilder::build(const QString &url, const QVariantMap &options) {
    QStringList args;
    args << "--verbose";
    args << url;

    // Output path - gallery-dl's --directory sets the base download directory
    QString tempPath = m_configManager->get("Paths", "temporary_downloads_directory").toString();
    if (tempPath.isEmpty()) {
        tempPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("LzyDownloader");
    }
    tempPath = QDir(tempPath).filePath(options.value("id").toString());
    QDir().mkpath(tempPath);
    args << "--directory" << tempPath;

    // Filename template - gallery-dl's -f supports full path templates with '/'
    // It creates subdirectories relative to --directory automatically
    QString filenameTemplate = m_configManager->get("General", "gallery_output_template", "{category}/{subcategory}/{id}_{filename}.{extension}").toString();
    args << "-f" << filenameTemplate;

    // Cookies
    QString cookiesBrowser = m_configManager->get("General", "gallery_cookies_from_browser", "None").toString();
    if (cookiesBrowser != "None") {
        args << "--cookies-from-browser" << cookiesBrowser.toLower();
    }

    // External Downloader
    if (m_configManager->get("Metadata", "use_aria2c", false).toBool()
        && ProcessUtils::findBinary("aria2c", m_configManager).source != "Not Found") {
        args << "-o" << "downloader.program=aria2c";
    }

    // Rate Limit
    QString rateLimit = m_configManager->get("General", "rate_limit", "Unlimited").toString();
    if (rateLimit != "Unlimited") {
        args << "--limit-rate" << rateLimit.split(' ').first();
    }

    // Override duplicate download check
    if (options.value("override_archive", false).toBool()) {
        args << "--no-skip";
    }

    // Restrict filenames
    if (m_configManager->get("General", "restrict_filenames", false).toBool()) {
        args << "--windows-filenames";
    }

    return args;
}
