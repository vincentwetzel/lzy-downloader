#pragma once
#include <QWidget>
#include <QVariant>

class ConfigManager;
class QLineEdit;
class QComboBox;
class QPushButton;

class OutputTemplatesPage : public QWidget {
    Q_OBJECT
public:
    explicit OutputTemplatesPage(ConfigManager *configManager, QWidget *parent = nullptr);
public slots:
    void loadSettings();
private slots:
    void validateAndSaveVideoTemplate();
    void validateAndSaveAudioTemplate();
    void validateAndSaveGalleryDlTemplate();
    void handleConfigSettingChanged(const QString &section, const QString &key, const QVariant &value);
private:
    ConfigManager *m_configManager;
    QLineEdit *m_videoOutputTemplateInput;
    QComboBox *m_videoTemplateTokensCombo;
    QPushButton *m_saveVideoTemplateButton;
    QLineEdit *m_audioOutputTemplateInput;
    QComboBox *m_audioTemplateTokensCombo;
    QPushButton *m_saveAudioTemplateButton;
    QLineEdit *m_galleryDlOutputTemplateInput;
    QComboBox *m_galleryDlTemplateTokensCombo;
    QPushButton *m_saveGalleryDlTemplateButton;
};