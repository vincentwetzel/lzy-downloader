#pragma once

#include <QObject>
#include <QTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>

class ConfigManager;
class ToggleSwitch;

class StartTabUiBuilder : public QObject
{
    Q_OBJECT
public:
    explicit StartTabUiBuilder(ConfigManager *configManager, QObject *parent = nullptr);
    void build(QWidget *parentWidget, QVBoxLayout *mainLayout);
    void applyCommandPreviewStyleSheet(QTextEdit* commandPreview);
    void applyUrlInputStyleSheet(QTextEdit* urlInput);
    QTextEdit* urlInput() const { return m_urlInput; }
    QPushButton* downloadButton() const { return m_downloadButton; }
    QComboBox* downloadTypeCombo() const { return m_downloadTypeCombo; }
    QComboBox* playlistLogicCombo() const { return m_playlistLogicCombo; }
    QComboBox* maxConcurrentCombo() const { return m_maxConcurrentCombo; }
    QComboBox* rateLimitCombo() const { return m_rateLimitCombo; }
    ToggleSwitch* overrideDuplicateCheck() const { return m_overrideDuplicateCheck; }
    QTextEdit* commandPreview() const { return m_commandPreview; }
    QPushButton* openDownloadsFolderButton() const { return m_openDownloadsFolderButton; }
private:
    ConfigManager *m_configManager;
    QTextEdit *m_urlInput;
    QPushButton *m_downloadButton;
    QComboBox *m_downloadTypeCombo;
    QComboBox *m_playlistLogicCombo;
    QComboBox *m_maxConcurrentCombo;
    QComboBox *m_rateLimitCombo;
    ToggleSwitch *m_overrideDuplicateCheck;
    QTextEdit *m_commandPreview;
    QPushButton *m_openDownloadsFolderButton;
};