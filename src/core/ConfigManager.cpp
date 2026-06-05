#include "ConfigManager.h"
#include <QDir>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QFile>
#include <QSettings>
#include <QDebug>

// Original constructor (now explicit about fileName)
ConfigManager::ConfigManager(const QString &fileName, QObject *parent)
    : QObject(parent) {
    // Determine the OS-native user data directory (e.g., %LOCALAPPDATA%\LzyDownloader)
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir dir(configDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    QString actualConfigPath = dir.filePath(fileName);

    // Server/headless mode used to store user preferences under Server/settings.ini.
    // Preferences now have one source of truth in the main app-local settings file;
    // keep Server/ for runtime state only.
    const QString obsoleteServerPath = QDir(dir.filePath(QStringLiteral("Server"))).filePath(fileName);
    if (!QFile::exists(actualConfigPath) && QFile::exists(obsoleteServerPath)) {
        if (QFile::copy(obsoleteServerPath, actualConfigPath)) {
            qInfo() << "Migrated obsolete server settings file to shared settings file:" << actualConfigPath;
        } else {
            qWarning() << "Failed to migrate obsolete server settings file:" << obsoleteServerPath
                       << "to" << actualConfigPath;
        }
    }

    // Seamlessly migrate legacy settings.ini from the application directory if present
    const QString legacyPath = QDir(QCoreApplication::applicationDirPath()).filePath(fileName);
    if (QFile::exists(legacyPath) && !QFile::exists(actualConfigPath)) {
        QFile::copy(legacyPath, actualConfigPath);
    }

    m_settings = new QSettings(actualConfigPath, QSettings::IniFormat, this);
    commonInitialization();
}

// New constructor for custom paths or testing
ConfigManager::ConfigManager(const QString &customPath, bool forTesting, QObject *parent)
    : QObject(parent) {
    if (forTesting) {
        if (customPath == QStringLiteral(":memory:")) {
            m_settings = new QSettings(QString(), QSettings::IniFormat, this); // In-memory
            qDebug() << "ConfigManager using in-memory settings for testing.";
        } else if (!customPath.isEmpty()) {
            m_settings = new QSettings(customPath, QSettings::IniFormat, this); // Temporary file path
            qDebug() << "ConfigManager using custom test path:" << customPath;
        } else {
            // Fallback for empty customPath in test mode, default to in-memory
            m_settings = new QSettings(QString(), QSettings::IniFormat, this);
            qWarning() << "ConfigManager test constructor called with empty customPath. Using in-memory settings.";
        }
    } else {
        // This constructor is primarily for testing or explicit custom paths.
        // If 'forTesting' is false, 'customPath' should be a valid file path.
        if (customPath.isEmpty()) {
            qCritical() << "ConfigManager custom constructor called with empty customPath and forTesting=false. This is likely an error.";
            // Fallback to default settings.ini to prevent crash, but log error
            QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
            QDir dir(configDir);
            if (!dir.exists()) { dir.mkpath("."); }
            QString defaultPath = dir.filePath(QStringLiteral("settings.ini"));
            m_settings = new QSettings(defaultPath, QSettings::IniFormat, this);
        } else {
            m_settings = new QSettings(customPath, QSettings::IniFormat, this);
        }
        qDebug() << "ConfigManager using custom path (not for testing):" << customPath;
    }
    commonInitialization();
}

void ConfigManager::commonInitialization() {
    initializeDefaultSettings();
    cleanUpLegacyKeys();

    // Enforce max concurrency cap of 4 on startup to prevent accidental aggressive spam
    const QString maxThreads = m_settings->value(QStringLiteral("General/max_threads"), QStringLiteral("4")).toString();
    bool isInt = false;
    const int threads = maxThreads.toInt(&isInt);
    if (isInt && threads > 4) {
        m_settings->setValue(QStringLiteral("General/max_threads"), QStringLiteral("4"));
        m_settings->sync();
    }

    // Always reset 'exit_after' to false on startup
    if (m_settings->value(QStringLiteral("General/exit_after"), false).toBool()) {
        m_settings->setValue(QStringLiteral("General/exit_after"), false);
        m_settings->sync();
    }

    // Enforce safe minimums for livestream wait intervals to prevent IP bans
    int waitMin = m_settings->value(QStringLiteral("Livestream/wait_for_video_min"), 60).toInt();
    int waitMax = m_settings->value(QStringLiteral("Livestream/wait_for_video_max"), 300).toInt();
    bool changedWait = false;
    if (waitMin < 15) {
        waitMin = 15;
        m_settings->setValue(QStringLiteral("Livestream/wait_for_video_min"), waitMin);
        changedWait = true;
    }
    if (waitMax <= waitMin) {
        m_settings->setValue(QStringLiteral("Livestream/wait_for_video_max"), waitMin + 45);
        changedWait = true;
    }
    if (changedWait) {
        m_settings->sync();
    }
}

void ConfigManager::initializeDefaultSettings() {
    m_defaultSettings[QStringLiteral("General")][QStringLiteral("output_template")] = QStringLiteral("%(title)s [%(uploader)s][%(upload_date>%Y-%m-%d)s][%(id)s].%(ext)s");
    m_defaultSettings[QStringLiteral("General")][QStringLiteral("output_template_video")] = QStringLiteral("%(title)s [%(uploader)s][%(upload_date>%Y-%m-%d)s][%(id)s].%(ext)s");
    m_defaultSettings[QStringLiteral("General")][QStringLiteral("output_template_audio")] = QStringLiteral("%(title)s [%(uploader)s][%(upload_date>%Y-%m-%d)s][%(id)s].%(ext)s");
    m_defaultSettings[QStringLiteral("General")][QStringLiteral("gallery_output_template")] = QStringLiteral("{category}/{id}_{filename}.{extension}");
    m_defaultSettings[QStringLiteral("General")][QStringLiteral("theme")] = QStringLiteral("System");
    m_defaultSettings[QStringLiteral("General")][QStringLiteral("cookies_from_browser")] = QStringLiteral("None");
    m_defaultSettings[QStringLiteral("General")][QStringLiteral("gallery_cookies_from_browser")] = QStringLiteral("None");
    m_defaultSettings[QStringLiteral("General")][QStringLiteral("sponsorblock")] = false;
    m_defaultSettings[QStringLiteral("General")][QStringLiteral("auto_paste_mode")] = 0; // Changed from auto_paste_on_focus (bool) to auto_paste_mode (int)
    m_defaultSettings[QStringLiteral("General")][QStringLiteral("single_line_preview")] = false;
    m_defaultSettings[QStringLiteral("General")][QStringLiteral("restrict_filenames")] = false;
    m_defaultSettings[QStringLiteral("General")][QStringLiteral("max_threads")] = QStringLiteral("4");
    m_defaultSettings[QStringLiteral("General")][QStringLiteral("playlist_logic")] = QStringLiteral("Ask");
    m_defaultSettings[QStringLiteral("General")][QStringLiteral("rate_limit")] = QStringLiteral("Unlimited");
    m_defaultSettings[QStringLiteral("General")][QStringLiteral("override_archive")] = false;
    m_defaultSettings[QStringLiteral("General")][QStringLiteral("exit_after")] = false;
    m_defaultSettings[QStringLiteral("General")][QStringLiteral("language")] = QStringLiteral("🇺🇸 English");
    m_defaultSettings[QStringLiteral("General")][QStringLiteral("show_debug_console")] = false;
    m_defaultSettings[QStringLiteral("General")][QStringLiteral("warn_stable_yt_dlp")] = true;
    m_defaultSettings[QStringLiteral("General")][QStringLiteral("enable_local_api")] = false;
    m_defaultSettings[QStringLiteral("General")][QStringLiteral("enable_local_api_server")] = false; // Fallback for UI naming differences
    m_defaultSettings[QStringLiteral("Video")][QStringLiteral("video_quality")] = QStringLiteral("best");
    m_defaultSettings[QStringLiteral("Video")][QStringLiteral("video_codec")] = QStringLiteral("H.264 (AVC)");
    m_defaultSettings[QStringLiteral("Video")][QStringLiteral("video_extension")] = QStringLiteral("mp4");
    m_defaultSettings[QStringLiteral("Video")][QStringLiteral("video_audio_codec")] = QStringLiteral("AAC");
    m_defaultSettings[QStringLiteral("Video")][QStringLiteral("video_multistreams")] = QStringLiteral("Default Stream");
    m_defaultSettings[QStringLiteral("Audio")][QStringLiteral("audio_quality")] = QStringLiteral("best");
    m_defaultSettings[QStringLiteral("Audio")][QStringLiteral("audio_codec")] = QStringLiteral("Opus");
    m_defaultSettings[QStringLiteral("Audio")][QStringLiteral("audio_extension")] = QStringLiteral("opus");
    m_defaultSettings[QStringLiteral("Audio")][QStringLiteral("audio_multistreams")] = QStringLiteral("Default Stream");
    m_defaultSettings[QStringLiteral("Metadata")][QStringLiteral("use_aria2c")] = false;
    m_defaultSettings[QStringLiteral("Metadata")][QStringLiteral("embed_chapters")] = true;
    m_defaultSettings[QStringLiteral("Metadata")][QStringLiteral("embed_metadata")] = true;
    m_defaultSettings[QStringLiteral("Metadata")][QStringLiteral("embed_thumbnail")] = true;
    m_defaultSettings[QStringLiteral("Metadata")][QStringLiteral("high_quality_thumbnail")] = true;
    m_defaultSettings[QStringLiteral("Metadata")][QStringLiteral("convert_thumbnail_to")] = QStringLiteral("jpg");
    m_defaultSettings[QStringLiteral("Metadata")][QStringLiteral("crop_artwork_to_square")] = true;
    m_defaultSettings[QStringLiteral("Metadata")][QStringLiteral("generate_folder_jpg")] = false;
    m_defaultSettings[QStringLiteral("Subtitles")][QStringLiteral("languages")] = QStringLiteral("en");
    m_defaultSettings[QStringLiteral("Subtitles")][QStringLiteral("embed_subtitles")] = true;
    m_defaultSettings[QStringLiteral("Subtitles")][QStringLiteral("write_subtitles")] = false;
    m_defaultSettings[QStringLiteral("Subtitles")][QStringLiteral("write_auto_subtitles")] = true;
    m_defaultSettings[QStringLiteral("Subtitles")][QStringLiteral("format")] = QStringLiteral("srt");
    m_defaultSettings[QStringLiteral("DownloadOptions")][QStringLiteral("split_chapters")] = false;
    m_defaultSettings[QStringLiteral("DownloadOptions")][QStringLiteral("download_sections_enabled")] = false;
    m_defaultSettings[QStringLiteral("DownloadOptions")][QStringLiteral("ffmpeg_cut_encoder")] = QStringLiteral("cpu");
    m_defaultSettings[QStringLiteral("DownloadOptions")][QStringLiteral("ffmpeg_cut_custom_args")] = QString();
    m_defaultSettings[QStringLiteral("DownloadOptions")][QStringLiteral("auto_clear_completed")] = false;
    m_defaultSettings[QStringLiteral("DownloadOptions")][QStringLiteral("geo_verification_proxy")] = QString();
    m_defaultSettings[QStringLiteral("DownloadOptions")][QStringLiteral("prefix_playlist_indices")] = false;
    m_defaultSettings[QStringLiteral("Livestream")][QStringLiteral("live_from_start")] = false;
    m_defaultSettings[QStringLiteral("Livestream")][QStringLiteral("wait_for_video")] = true; // Wait for scheduled streams by default
    m_defaultSettings[QStringLiteral("Livestream")][QStringLiteral("wait_for_video_min")] = 60; // Wait at least 1 minute between checks
    m_defaultSettings[QStringLiteral("Livestream")][QStringLiteral("wait_for_video_max")] = 300; // Wait at most 5 minutes between checks
    m_defaultSettings[QStringLiteral("Livestream")][QStringLiteral("use_part")] = true;
    m_defaultSettings[QStringLiteral("Livestream")][QStringLiteral("download_as")] = QStringLiteral("MPEG-TS");
    m_defaultSettings[QStringLiteral("Livestream")][QStringLiteral("quality")] = QStringLiteral("best");
    m_defaultSettings[QStringLiteral("Livestream")][QStringLiteral("convert_to")] = QStringLiteral("None");
}

void ConfigManager::cleanUpLegacyKeys() {
    // These top-level sections are allowed to have dynamic/user-defined keys
    const QStringList dynamicGroups = {QStringLiteral("SortingRules"), QStringLiteral("MainWindow"), QStringLiteral("UI"), QStringLiteral("Geometry"), QStringLiteral("Paths"), QStringLiteral("Binaries"), QStringLiteral("LocalApi")};
    
    const QStringList allKeys = m_settings->allKeys();
    bool keysRemoved = false;

    for (const QString &fullKey : allKeys) {
        const QStringList parts = fullKey.split(QLatin1Char('/'));
        if (parts.isEmpty()) continue;

        const QString section = parts.first();
        const QString key = parts.mid(1).join(QLatin1Char('/'));

        // 1. Preserve explicitly dynamic groups completely (case-insensitive)
        bool inDynamic = false;
        for (const QString &dg : dynamicGroups) {
            if (section.compare(dg, Qt::CaseInsensitive) == 0) {
                inDynamic = true;
                break;
            }
        }
        if (inDynamic) continue;

        // 2. Preserve keys that exist in our strict defaults map (case-insensitive)
        bool found = false;
        for (auto it = m_defaultSettings.constBegin(); it != m_defaultSettings.constEnd(); ++it) {
            if (it.key().compare(section, Qt::CaseInsensitive) == 0) {
                for (auto it2 = it.value().constBegin(); it2 != it.value().constEnd(); ++it2) {
                    if (it2.key().compare(key, Qt::CaseInsensitive) == 0) {
                        found = true;
                        break;
                    }
                }
            }
            if (found) break;
        }
        
        if (found) continue;

        // 3. It's a legacy or dead key, nuke it
        m_settings->remove(fullKey);
        keysRemoved = true;
    }

    if (keysRemoved) {
        m_settings->sync();
    }
}

QVariant ConfigManager::get(const QString &section, const QString &key, const QVariant &defaultValue) {
    // The fallback for the QSettings object should be our application's default,
    // which in turn can have a fallback to the function's default parameter.
    QVariant appDefault = getDefault(section, key);
    QVariant finalFallback = appDefault.isValid() ? appDefault : defaultValue;
    return m_settings->value(section + QLatin1Char('/') + key, finalFallback);
}

bool ConfigManager::set(const QString &section, const QString &key, const QVariant &value) {
    QString fullKey = section + QLatin1Char('/') + key;
    if (m_settings->contains(fullKey) && m_settings->value(fullKey) == value) {
        return true;
    }
    m_settings->setValue(fullKey, value);
    emit settingChanged(section, key, value);
    return true;
}

void ConfigManager::remove(const QString &section, const QString &key) {
    QString fullKey = section + QLatin1Char('/') + key;
    if (m_settings->contains(fullKey)) {
        m_settings->remove(fullKey);
        emit settingChanged(section, key, QVariant());
    }
}

void ConfigManager::save() {
    m_settings->sync();
}

QString ConfigManager::getConfigDir() const {
    return QFileInfo(m_settings->fileName()).absolutePath();
}

void ConfigManager::setDefaults() {
    for (auto it = m_defaultSettings.constBegin(); it != m_defaultSettings.constEnd(); ++it) {
        for (auto it2 = it.value().constBegin(); it2 != it.value().constEnd(); ++it2) {
            set(it.key(), it2.key(), it2.value());
        }
    }
}

QVariant ConfigManager::getDefault(const QString &section, const QString &key) {
    return m_defaultSettings.value(section, {}).value(key);
}

void ConfigManager::resetToDefaults() {
    // --- 1. Define what to preserve ---
    const QList<QPair<QString, QString>> keysToPreserve = {
        {QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory")},
        {QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory")},
        {QStringLiteral("General"), QStringLiteral("theme")},
        {QStringLiteral("General"), QStringLiteral("output_template")},
        {QStringLiteral("General"), QStringLiteral("output_template_video")},
        {QStringLiteral("General"), QStringLiteral("output_template_audio")},
        {QStringLiteral("General"), QStringLiteral("gallery_output_template")},
        {QStringLiteral("Binaries"), QStringLiteral("yt-dlp_path")},
        {QStringLiteral("Binaries"), QStringLiteral("ffmpeg_path")},
        {QStringLiteral("Binaries"), QStringLiteral("ffprobe_path")},
        {QStringLiteral("Binaries"), QStringLiteral("gallery-dl_path")},
        {QStringLiteral("Binaries"), QStringLiteral("aria2c_path")},
        {QStringLiteral("Binaries"), QStringLiteral("deno_path")}
    };
    const QStringList groupsToPreserve = {QStringLiteral("SortingRules"), QStringLiteral("MainWindow"), QStringLiteral("UI"), QStringLiteral("Geometry")};

    // --- 2. Preserve individual keys ---
    QMap<QString, QVariant> preservedValues;
    for (const auto& keyPair : keysToPreserve) {
        QVariant value = get(keyPair.first, keyPair.second);
        if (!value.isNull() && !value.toString().isEmpty()) {
            preservedValues.insert(QStringLiteral("%1/%2").arg(keyPair.first, keyPair.second), value);
        }
    }

    // --- 3. Preserve whole groups ---
    QMap<QString, QVariantMap> preservedGroups;
    for (const QString& groupName : groupsToPreserve) {
        QVariantMap groupValues;
        m_settings->beginGroup(groupName);
        for (const QString &key : m_settings->childKeys()) {
            groupValues[key] = m_settings->value(key);
        }
        m_settings->endGroup();
        if (!groupValues.isEmpty()) {
            preservedGroups.insert(groupName, groupValues);
        }
    }

    // --- 4. Clear and apply defaults ---
    m_settings->clear();
    m_settings->sync();

    bool oldState = blockSignals(true);
    setDefaults();

    // --- 5. Restore preserved values ---
    for (auto it = preservedValues.constBegin(); it != preservedValues.constEnd(); ++it) {
        m_settings->setValue(it.key(), it.value());
    }

    for (auto it = preservedGroups.constBegin(); it != preservedGroups.constEnd(); ++it) {
        m_settings->beginGroup(it.key());
        const QVariantMap& groupValues = it.value();
        for (auto it2 = groupValues.constBegin(); it2 != groupValues.constEnd(); ++it2) {
            m_settings->setValue(it2.key(), it2.value());
        }
        m_settings->endGroup();
    }

    blockSignals(oldState);

    save();
    emit settingsReset();
}
