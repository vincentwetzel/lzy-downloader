#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QObject>
#include <QSettings>
#include <QVariant>

class ConfigManager : public QObject {
    Q_OBJECT

public:
    explicit ConfigManager(const QString &fileName, QObject *parent = nullptr); // Original, now explicit about fileName
    explicit ConfigManager(const QString &customPath, bool forTesting, QObject *parent = nullptr); // New, for custom paths or testing
    QVariant get(const QString &section, const QString &key, const QVariant &defaultValue = QVariant());
    bool set(const QString &section, const QString &key, const QVariant &value);
    void remove(const QString &section, const QString &key);
    void save();
    QString getConfigDir() const;
    void setDefaults();
    QVariant getDefault(const QString &section, const QString &key);
    void resetToDefaults();

signals:
    void settingChanged(const QString &section, const QString &key, const QVariant &value);
    void settingsReset();

private:
    void initializeDefaultSettings();
    void cleanUpLegacyKeys();
    void commonInitialization(); // New method for shared initialization logic

    QSettings *m_settings;
    QMap<QString, QMap<QString, QVariant>> m_defaultSettings;
};

#endif // CONFIGMANAGER_H
