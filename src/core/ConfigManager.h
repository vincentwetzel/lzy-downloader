#pragma once

#include <QObject>
#include <QVariant>

class QSettings;

/**
 * @class ConfigManager
 * @brief Manages application settings using QSettings.
 *
 * This class provides a centralized interface for accessing and modifying application
 * settings, which are stored in an INI file. It handles default values, data types,
 * and ensures that settings are loaded from and saved to the correct location.
 * It also supports legacy settings migration and in-memory configurations for testing.
 */
class ConfigManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Constructs a ConfigManager for the primary application settings file.
     * @param fileName The name of the settings file (e.g., "settings.ini").
     * @param parent The parent QObject.
     */
    explicit ConfigManager(const QString &fileName, QObject *parent = nullptr);

    /**
     * @brief Constructs a ConfigManager for a custom file path or for testing purposes.
     * @param customPath The absolute path to the settings file, or ":memory:" for an in-memory store.
     * @param forTesting If true, enables test-specific behavior like in-memory storage.
     * @param parent The parent QObject.
     */
    explicit ConfigManager(const QString &customPath, bool forTesting, QObject *parent = nullptr);

    /**
     * @brief Retrieves a setting value.
     * @param section The section of the setting (e.g., "General").
     * @param key The key of the setting (e.g., "theme").
     * @param defaultValue A fallback value to return if the setting is not found.
     * @return The setting value as a QVariant. Returns `defaultValue` if the key does not exist.
     */
    [[nodiscard]] QVariant get(const QString &section, const QString &key, const QVariant &defaultValue = QVariant()) const;

    /**
     * @brief Sets a setting value.
     * @param section The section of the setting.
     * @param key The key of the setting.
     * @param value The value to store.
     * @return True on success.
     */
    bool set(const QString &section, const QString &key, const QVariant &value);

    /**
     * @brief Removes a setting key.
     * @param section The section of the setting.
     * @param key The key to remove.
     */
    void remove(const QString &section, const QString &key);

    /**
     * @brief Flushes any unsaved changes to the persistent storage.
     *
     * QSettings often saves changes asynchronously. Call this to force a write.
     */
    void save();

    /**
     * @brief Gets the absolute path to the directory containing the settings file.
     * @return The configuration directory path.
     */
    [[nodiscard]] QString getConfigDir() const;

    /**
     * @brief Populates the settings with the application's default values.
     *
     * This is useful for initial setup or after a reset.
     */
    void setDefaults();

    /**
     * @brief Retrieves a default value for a specific setting.
     * @param section The section of the setting.
     * @param key The key of the setting.
     * @return The default value as a QVariant. Returns an invalid QVariant if not found.
     */
    [[nodiscard]] QVariant getDefault(const QString &section, const QString &key) const;

    /**
     * @brief Resets all settings to their default values.
     *
     * This function clears all settings and re-applies the defaults, while preserving
     * certain critical user settings like download paths.
     */
    void resetToDefaults();

signals:
    /**
     * @brief Emitted when a setting has been changed via the `set` or `remove` method.
     * @param section The section of the changed setting.
     * @param key The key of the changed setting.
     * @param value The new value. An invalid QVariant if the setting was removed.
     */
    void settingChanged(const QString &section, const QString &key, const QVariant &value);

    /**
     * @brief Emitted after `resetToDefaults` has completed.
     */
    void settingsReset();

private:
    void initializeDefaultSettings();
    void cleanUpLegacyKeys();
    void commonInitialization(); // New method for shared initialization logic

    QSettings *m_settings;
    QMap<QString, QMap<QString, QVariant>> m_defaultSettings;
};
