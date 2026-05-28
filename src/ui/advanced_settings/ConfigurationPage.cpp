#include "ConfigurationPage.h"
#include "core/ConfigManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QSignalBlocker>

ConfigurationPage::ConfigurationPage(ConfigManager *configManager, QWidget *parent)
    : QWidget(parent), m_configManager(configManager) {
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QGroupBox *configGroup = new QGroupBox("Configuration", this);
    configGroup->setToolTip("General application settings including download locations and theme.");
    QFormLayout *configLayout = new QFormLayout(configGroup);

    m_completedDirInput = new QLineEdit(this);
    m_completedDirInput->setReadOnly(true);
    m_completedDirInput->setToolTip("This is where your finished downloads will be saved. Click 'Browse' to change it.");
    m_browseCompletedBtn = new QPushButton("Browse...", this);
    m_browseCompletedBtn->setToolTip("Click to choose a different folder for your completed downloads.");
    QHBoxLayout *completedLayout = new QHBoxLayout();
    completedLayout->addWidget(m_completedDirInput);
    completedLayout->addWidget(m_browseCompletedBtn);
    QLabel *completedLabel = new QLabel("Output folder:", this);
    completedLabel->setToolTip(m_completedDirInput->toolTip());
    configLayout->addRow(completedLabel, completedLayout);

    m_tempDirInput = new QLineEdit(this);
    m_tempDirInput->setReadOnly(true);
    m_tempDirInput->setToolTip("This is a temporary folder used during downloads. You usually don't need to change this.");
    m_browseTempBtn = new QPushButton("Browse...", this);
    m_browseTempBtn->setToolTip("Click to choose a different temporary folder for downloads.");
    QHBoxLayout *tempLayout = new QHBoxLayout();
    tempLayout->addWidget(m_tempDirInput);
    tempLayout->addWidget(m_browseTempBtn);
    QLabel *tempLabel = new QLabel("Temporary folder:", this);
    tempLabel->setToolTip(m_tempDirInput->toolTip());
    configLayout->addRow(tempLabel, tempLayout);

    m_themeCombo = new QComboBox(this);
    m_themeCombo->setToolTip("Choose the visual style of the application: 'System' (matches your computer's setting), 'Light', or 'Dark'.");
    m_themeCombo->addItems({"System", "Light", "Dark"});
    QLabel *themeLabel = new QLabel("Theme:", this);
    themeLabel->setToolTip(m_themeCombo->toolTip());
    configLayout->addRow(themeLabel, m_themeCombo);

    m_enableApiServerCheck = new QCheckBox("Enable Local API Server", this);
    m_enableApiServerCheck->setToolTip("Allows external applications (like a local Discord bot) to send download links directly to this app via port 8765.");
    configLayout->addRow("", m_enableApiServerCheck);

    layout->addWidget(configGroup);
    layout->addStretch();

    connect(m_browseCompletedBtn, &QPushButton::clicked, this, &ConfigurationPage::selectCompletedDir);
    connect(m_browseTempBtn, &QPushButton::clicked, this, &ConfigurationPage::selectTempDir);
    connect(m_themeCombo, &QComboBox::currentTextChanged, this, &ConfigurationPage::onThemeChanged);
    connect(m_enableApiServerCheck, &QCheckBox::stateChanged, this, &ConfigurationPage::onEnableApiServerToggled);
    connect(m_configManager, &ConfigManager::settingChanged, this, &ConfigurationPage::handleConfigSettingChanged);
}

void ConfigurationPage::loadSettings() {
    QSignalBlocker b1(m_completedDirInput);
    QSignalBlocker b2(m_tempDirInput);
    QSignalBlocker b3(m_themeCombo);
    QSignalBlocker b4(m_enableApiServerCheck);

    m_completedDirInput->setText(m_configManager->get("Paths", "completed_downloads_directory").toString());
    m_tempDirInput->setText(m_configManager->get("Paths", "temporary_downloads_directory").toString());
    m_themeCombo->setCurrentText(m_configManager->get("General", "theme", "System").toString());
    m_enableApiServerCheck->setChecked(m_configManager->get("General", "enable_local_api", false).toBool());
}

void ConfigurationPage::selectCompletedDir() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Completed Downloads Directory", m_completedDirInput->text());
    if (!dir.isEmpty()) {
        m_configManager->set("Paths", "completed_downloads_directory", dir);
        m_configManager->save();
    }
}

void ConfigurationPage::selectTempDir() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Temporary Downloads Directory", m_tempDirInput->text());
    if (!dir.isEmpty()) {
        m_configManager->set("Paths", "temporary_downloads_directory", dir);
        m_configManager->save();
    }
}

void ConfigurationPage::onThemeChanged(const QString &text) {
    m_configManager->set("General", "theme", text);
    m_configManager->save();
    emit themeChanged(text);
}

void ConfigurationPage::onEnableApiServerToggled(int state) {
    m_configManager->set("General", "enable_local_api", state == Qt::Checked);
    m_configManager->save();
}

void ConfigurationPage::handleConfigSettingChanged(const QString &section, const QString &key, const QVariant &value) {
    if (section == "Paths") {
        if (key == "completed_downloads_directory") m_completedDirInput->setText(value.toString());
        else if (key == "temporary_downloads_directory") m_tempDirInput->setText(value.toString());
    } else if (section == "General" && key == "theme") {
        if (m_themeCombo->currentText() != value.toString()) {
            QSignalBlocker b(m_themeCombo);
            m_themeCombo->setCurrentText(value.toString());
            emit themeChanged(value.toString());
        }
    }
    else if (section == "General" && key == "enable_local_api") {
        QSignalBlocker b(m_enableApiServerCheck);
        m_enableApiServerCheck->setChecked(value.toBool());
    }
}