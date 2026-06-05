#pragma once

#include <QWidget>
#include <QVariantMap>

class ConfigManager;
class ToggleSwitch;
class QComboBox;
class QSpinBox;

class LivestreamSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit LivestreamSettingsPage(ConfigManager *configManager, QWidget *parent = nullptr);

public slots:
    Q_INVOKABLE void loadSettings();

private slots:
    void saveSettings();
    void handleConfigSettingChanged(const QString &section, const QString &key, const QVariant &value);

private:
    ConfigManager *m_configManager;

    ToggleSwitch *m_liveFromStartCheck;
    ToggleSwitch *m_waitForVideoCheck;
    QSpinBox *m_waitMinSpin;
    QSpinBox *m_waitMaxSpin;
    ToggleSwitch *m_usePartCheck;
    
    QComboBox *m_downloadAsCombo;
    QComboBox *m_qualityCombo;
    QComboBox *m_convertToCombo;
    
    void setupUI();
};