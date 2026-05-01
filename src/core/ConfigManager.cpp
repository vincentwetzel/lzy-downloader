#include "ConfigManager.h"
#include <QDir>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QFile>
#include <QDebug>

ConfigManager::ConfigManager(const QString &filePath, QObject *parent)
    : QObject(parent) {
    // Determine the OS-native user data directory (e.g., %LOCALAPPDATA%\LzyDownloader)
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir dir(configDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    QString configPath = dir.filePath(filePath);

    // Server/headless mode used to store user preferences under Server/settings.ini.
    // Preferences now have one source of truth in the main app-local settings file;
    // keep Server/ for runtime state only.
    QString obsoleteServerPath = QDir(dir.filePath("Server")).filePath(filePath);
    if (!QFile::exists(configPath) && QFile::exists(obsoleteServerPath)) {
        if (QFile::copy(obsoleteServerPath, configPath)) {
            qInfo() << "Migrated obsolete server settings file to shared settings file:" << configPath;
        } else {
            qWarning() << "Failed to migrate obsolete server settings file:" << obsoleteServerPath
                       << "to" << configPath;
        }
    }

    // Seamlessly migrate legacy settings.ini from the application directory if present
    QString legacyPath = QDir(QCoreApplication::applicationDirPath()).filePath(filePath);
    if (QFile::exists(legacyPath) && !QFile::exists(configPath)) {
        QFile::copy(legacyPath, configPath);
    }

    m_settings = new QSettings(configPath, QSettings::IniFormat, this);
    initializeDefaultSettings();
    cleanUpLegacyKeys();

    // Enforce max concurrency cap of 4 on startup to prevent accidental aggressive spam
    QString maxThreads = m_settings->value("General/max_threads", "4").toString();
    bool isInt;
    int threads = maxThreads.toInt(&isInt);
    if (isInt && threads > 4) {
        m_settings->setValue("General/max_threads", "4");
        m_settings->sync();
    }

    // Always reset 'exit_after' to false on startup
    if (m_settings->value("General/exit_after", false).toBool()) {
        m_settings->setValue("General/exit_after", false);
        m_settings->sync();
    }
}

void ConfigManager::initializeDefaultSettings() {
    m_defaultSettings["General"]["output_template"] = "%(title)s [%(uploader)s][%(upload_date>%Y-%m-%d)s][%(id)s].%(ext)s";
    m_defaultSettings["General"]["output_template_video"] = "%(title)s [%(uploader)s][%(upload_date>%Y-%m-%d)s][%(id)s].%(ext)s";
    m_defaultSettings["General"]["output_template_audio"] = "%(title)s [%(uploader)s][%(upload_date>%Y-%m-%d)s][%(id)s].%(ext)s";
    m_defaultSettings["General"]["gallery_output_template"] = "{category}/{id}_{filename}.{extension}";
    m_defaultSettings["General"]["theme"] = "System";
    m_defaultSettings["General"]["cookies_from_browser"] = "None";
    m_defaultSettings["General"]["gallery_cookies_from_browser"] = "None";
    m_defaultSettings["General"]["sponsorblock"] = false;
    m_defaultSettings["General"]["auto_paste_mode"] = 0; // Changed from auto_paste_on_focus (bool) to auto_paste_mode (int)
    m_defaultSettings["General"]["single_line_preview"] = false;
    m_defaultSettings["General"]["restrict_filenames"] = false;
    m_defaultSettings["General"]["max_threads"] = "4";
    m_defaultSettings["General"]["playlist_logic"] = "Ask";
    m_defaultSettings["General"]["rate_limit"] = "Unlimited";
    m_defaultSettings["General"]["override_archive"] = false;
    m_defaultSettings["General"]["exit_after"] = false;
    m_defaultSettings["General"]["language"] = "🇺🇸 English";
    m_defaultSettings["General"]["show_debug_console"] = false;
    m_defaultSettings["General"]["warn_stable_yt_dlp"] = true;
    m_defaultSettings["General"]["enable_local_api"] = false;
    m_defaultSettings["General"]["enable_local_api_server"] = false; // Fallback for UI naming differences
    m_defaultSettings["Video"]["video_quality"] = "best";
    m_defaultSettings["Video"]["video_codec"] = "H.264 (AVC)";
    m_defaultSettings["Video"]["video_extension"] = "mp4";
    m_defaultSettings["Video"]["video_audio_codec"] = "AAC";
    m_defaultSettings["Video"]["video_multistreams"] = "Default Stream";
    m_defaultSettings["Audio"]["audio_quality"] = "best";
    m_defaultSettings["Audio"]["audio_codec"] = "Opus";
    m_defaultSettings["Audio"]["audio_extension"] = "opus";
    m_defaultSettings["Audio"]["audio_multistreams"] = "Default Stream";
    m_defaultSettings["Metadata"]["use_aria2c"] = false;
    m_defaultSettings["Metadata"]["embed_chapters"] = true;
    m_defaultSettings["Metadata"]["embed_metadata"] = true;
    m_defaultSettings["Metadata"]["embed_thumbnail"] = true;
    m_defaultSettings["Metadata"]["high_quality_thumbnail"] = true;
    m_defaultSettings["Metadata"]["convert_thumbnail_to"] = "jpg";
    m_defaultSettings["Metadata"]["crop_artwork_to_square"] = true;
    m_defaultSettings["Metadata"]["generate_folder_jpg"] = false;
    m_defaultSettings["Subtitles"]["languages"] = "en";
    m_defaultSettings["Subtitles"]["embed_subtitles"] = true;
    m_defaultSettings["Subtitles"]["write_subtitles"] = false;
    m_defaultSettings["Subtitles"]["write_auto_subtitles"] = true;
    m_defaultSettings["Subtitles"]["format"] = "srt";
    m_defaultSettings["DownloadOptions"]["split_chapters"] = false;
    m_defaultSettings["DownloadOptions"]["download_sections_enabled"] = false;
    m_defaultSettings["DownloadOptions"]["ffmpeg_cut_encoder"] = "cpu";
    m_defaultSettings["DownloadOptions"]["ffmpeg_cut_custom_args"] = "";
    m_defaultSettings["DownloadOptions"]["auto_clear_completed"] = false;
    m_defaultSettings["DownloadOptions"]["geo_verification_proxy"] = "";
    m_defaultSettings["DownloadOptions"]["prefix_playlist_indices"] = false;
    m_defaultSettings["Livestream"]["live_from_start"] = false;
    m_defaultSettings["Livestream"]["wait_for_video"] = true; // Wait for scheduled streams by default
    m_defaultSettings["Livestream"]["wait_for_video_min"] = 60; // Wait at least 1 minute between checks
    m_defaultSettings["Livestream"]["wait_for_video_max"] = 300; // Wait at most 5 minutes between checks
    m_defaultSettings["Livestream"]["use_part"] = true;
    m_defaultSettings["Livestream"]["download_as"] = "MPEG-TS";
    m_defaultSettings["Livestream"]["quality"] = "best";
    m_defaultSettings["Livestream"]["convert_to"] = "None";
}

void ConfigManager::cleanUpLegacyKeys() {
    // These top-level sections are allowed to have dynamic/user-defined keys
    const QStringList dynamicGroups = {"SortingRules", "MainWindow", "UI", "Geometry", "Paths", "Binaries", "LocalApi"};
    
    QStringList allKeys = m_settings->allKeys();
    bool keysRemoved = false;

    for (const QString &fullKey : allKeys) {
        QStringList parts = fullKey.split('/');
        if (parts.isEmpty()) continue;

        QString section = parts.first();
        QString key = parts.mid(1).join('/');

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
    return m_settings->value(section + "/" + key, finalFallback);
}

bool ConfigManager::set(const QString &section, const QString &key, const QVariant &value) {
    QString fullKey = section + "/" + key;
    if (m_settings->contains(fullKey) && m_settings->value(fullKey) == value) {
        return true;
    }
    m_settings->setValue(fullKey, value);
    emit settingChanged(section, key, value);
    return true;
}

void ConfigManager::remove(const QString &section, const QString &key) {
    QString fullKey = section + "/" + key;
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
        {"Paths", "completed_downloads_directory"},
        {"Paths", "temporary_downloads_directory"},
        {"General", "theme"},
        {"General", "output_template"},
        {"General", "output_template_video"},
        {"General", "output_template_audio"},
        {"General", "gallery_output_template"},
        {"Binaries", "yt-dlp_path"},
        {"Binaries", "ffmpeg_path"},
        {"Binaries", "ffprobe_path"},
        {"Binaries", "gallery-dl_path"},
        {"Binaries", "aria2c_path"},
        {"Binaries", "deno_path"}
    };
    const QStringList groupsToPreserve = {"SortingRules", "MainWindow", "UI", "Geometry"};

    // --- 2. Preserve individual keys ---
    QMap<QString, QVariant> preservedValues;
    for (const auto& keyPair : keysToPreserve) {
        QVariant value = get(keyPair.first, keyPair.second);
        if (!value.isNull() && !value.toString().isEmpty()) {
            preservedValues.insert(keyPair.first + "/" + keyPair.second, value);
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
