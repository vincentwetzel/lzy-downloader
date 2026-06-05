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

    QGroupBox *configGroup = new QGroupBox(tr("Configuration"), this);
    configGroup->setToolTip(tr("Set the folders LzyDownloader uses, the app theme, and optional local API access."));
    QFormLayout *configLayout = new QFormLayout(configGroup);

    m_completedDirInput = new QLineEdit(this);
    m_completedDirInput->setReadOnly(true);
    m_completedDirInput->setToolTip(tr("Finished downloads are moved here after the temporary file is verified."));
    m_browseCompletedBtn = new QPushButton(tr("Browse..."), this);
    m_browseCompletedBtn->setToolTip(tr("Click to choose a different folder for your completed downloads."));
    QHBoxLayout *completedLayout = new QHBoxLayout();
    completedLayout->addWidget(m_completedDirInput);
    completedLayout->addWidget(m_browseCompletedBtn);
    QLabel *completedLabel = new QLabel(tr("Output folder:"), this);
    completedLabel->setToolTip(m_completedDirInput->toolTip());
    configLayout->addRow(completedLabel, completedLayout);

    m_tempDirInput = new QLineEdit(this);
    m_tempDirInput->setReadOnly(true);
    m_tempDirInput->setToolTip(tr("Downloads are written here first, then verified and moved to the output folder. Use a fast drive with enough free space."));
    m_browseTempBtn = new QPushButton(tr("Browse..."), this);
    m_browseTempBtn->setToolTip(tr("Click to choose a different temporary folder for downloads."));
    QHBoxLayout *tempLayout = new QHBoxLayout();
    tempLayout->addWidget(m_tempDirInput);
    tempLayout->addWidget(m_browseTempBtn);
    QLabel *tempLabel = new QLabel(tr("Temporary folder:"), this);
    tempLabel->setToolTip(m_tempDirInput->toolTip());
    configLayout->addRow(tempLabel, tempLayout);

    m_themeCombo = new QComboBox(this);
    m_themeCombo->setToolTip(tr("Choose the visual style of the application: 'System' (matches your computer's setting), 'Light', or 'Dark'."));
    m_themeCombo->addItems({tr("System"), tr("Light"), tr("Dark")});
    QLabel *themeLabel = new QLabel(tr("Theme:"), this);
    themeLabel->setToolTip(m_themeCombo->toolTip());
    configLayout->addRow(themeLabel, m_themeCombo);

    m_enableApiServerCheck = new QCheckBox(tr("Enable Local API Server"), this);
    m_enableApiServerCheck->setToolTip(tr("Allow trusted local apps to enqueue downloads through 127.0.0.1:8765 using the app's API token."));
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

    m_completedDirInput->setText(m_configManager->get(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory")).toString());
    m_tempDirInput->setText(m_configManager->get(QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory")).toString());
    m_themeCombo->setCurrentText(m_configManager->get(QStringLiteral("General"), QStringLiteral("theme"), tr("System")).toString());
    m_enableApiServerCheck->setChecked(m_configManager->get(QStringLiteral("General"), QStringLiteral("enable_local_api"), false).toBool());
}

void ConfigurationPage::selectCompletedDir() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Completed Downloads Directory"), m_completedDirInput->text());
    if (!dir.isEmpty()) {
        m_configManager->set(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory"), dir);
        m_configManager->save();
    }
}

void ConfigurationPage::selectTempDir() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Temporary Downloads Directory"), m_tempDirInput->text());
    if (!dir.isEmpty()) {
        m_configManager->set(QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory"), dir);
        m_configManager->save();
    }
}

void ConfigurationPage::onThemeChanged(const QString &text) {
    m_configManager->set(QStringLiteral("General"), QStringLiteral("theme"), text);
    m_configManager->save();
    emit themeChanged(text);
}

void ConfigurationPage::onEnableApiServerToggled(int state) {
    m_configManager->set(QStringLiteral("General"), QStringLiteral("enable_local_api"), state == Qt::Checked);
    m_configManager->save();
}

void ConfigurationPage::handleConfigSettingChanged(const QString &section, const QString &key, const QVariant &value) {
    if (section == QStringLiteral("Paths")) {
        if (key == QStringLiteral("completed_downloads_directory")) m_completedDirInput->setText(value.toString());
        else if (key == QStringLiteral("temporary_downloads_directory")) m_tempDirInput->setText(value.toString());
    } else if (section == QStringLiteral("General") && key == QStringLiteral("theme")) {
        if (m_themeCombo->currentText() != value.toString()) {
            QSignalBlocker b(m_themeCombo);
            m_themeCombo->setCurrentText(value.toString());
            emit themeChanged(value.toString());
        }
    }
    else if (section == QStringLiteral("General") && key == QStringLiteral("enable_local_api")) {
        QSignalBlocker b(m_enableApiServerCheck);
        m_enableApiServerCheck->setChecked(value.toBool());
    }
}
