#include "LivestreamSettingsPage.h"
#include "core/ConfigManager.h"
#include "ui/ToggleSwitch.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QGroupBox>
#include <QSignalBlocker>

LivestreamSettingsPage::LivestreamSettingsPage(ConfigManager *configManager, QWidget *parent)
    : QWidget(parent), m_configManager(configManager) {
    setupUI();
}

void LivestreamSettingsPage::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(15);

    QGroupBox *livestreamGroup = new QGroupBox("Livestream Settings", this);
    livestreamGroup->setToolTip("Set the default options for downloading livestreams, including scheduled streams.");
    QFormLayout *formLayout = new QFormLayout(livestreamGroup);
    formLayout->setSpacing(10);

    m_liveFromStartCheck = new ToggleSwitch(this);
    m_liveFromStartCheck->setToolTip("If enabled, downloads the livestream from the start instead of the current live edge (where supported).");
    QLabel *liveFromStartLabel = new QLabel("Record from beginning of broadcast", this);
    liveFromStartLabel->setWordWrap(true);
    liveFromStartLabel->setToolTip(m_liveFromStartCheck->toolTip());
    formLayout->addRow(liveFromStartLabel, m_liveFromStartCheck);

    QHBoxLayout *waitLayout = new QHBoxLayout();
    waitLayout->setContentsMargins(0, 0, 0, 0);
    m_waitForVideoCheck = new ToggleSwitch(this);
    m_waitForVideoCheck->setToolTip("If the livestream hasn't started yet, keep retrying instead of failing immediately.");
    
    m_waitMinSpin = new QSpinBox(this);
    m_waitMinSpin->setRange(1, 3600);
    m_waitMinSpin->setSuffix(" s");
    m_waitMinSpin->setToolTip("Minimum seconds to wait between retries.");
    
    m_waitMaxSpin = new QSpinBox(this);
    m_waitMaxSpin->setRange(1, 3600);
    m_waitMaxSpin->setSuffix(" s");
    m_waitMaxSpin->setToolTip("Maximum seconds to wait between retries.");
    
    QLabel *waitLabel = new QLabel("Wait for video to start", this);
    waitLabel->setWordWrap(true);
    waitLabel->setToolTip(m_waitForVideoCheck->toolTip());
    waitLayout->addWidget(waitLabel);
    waitLayout->addWidget(m_waitForVideoCheck);
    waitLayout->addSpacing(15);
    QLabel *minLabel = new QLabel("Min:", this);
    minLabel->setToolTip("Minimum seconds to wait between retries.");
    waitLayout->addWidget(minLabel);
    waitLayout->addWidget(m_waitMinSpin);
    QLabel *maxLabel = new QLabel("Max:", this);
    maxLabel->setToolTip("Maximum seconds to wait between retries.");
    waitLayout->addWidget(maxLabel);
    waitLayout->addWidget(m_waitMaxSpin);
    waitLayout->addStretch();
    
    formLayout->addRow("", waitLayout);

    m_usePartCheck = new ToggleSwitch(this);
    m_usePartCheck->setToolTip("Resume interrupted livestream downloads using .part files.");
    QLabel *usePartLabel = new QLabel("Use .part files", this);
    usePartLabel->setToolTip(m_usePartCheck->toolTip());
    formLayout->addRow(usePartLabel, m_usePartCheck);

    m_downloadAsCombo = new QComboBox(this);
    m_downloadAsCombo->addItems({"MPEG-TS", "MKV"});
    m_downloadAsCombo->setToolTip("Preferred container format for downloading the livestream.");
    QLabel *downloadAsLabel = new QLabel("Download As:", this);
    downloadAsLabel->setToolTip(m_downloadAsCombo->toolTip());
    formLayout->addRow(downloadAsLabel, m_downloadAsCombo);

    m_qualityCombo = new QComboBox(this);
    m_qualityCombo->addItems({"best", "1080p", "720p", "480p", "360p", "worst"});
    m_qualityCombo->setToolTip("Target resolution for the livestream.");
    QLabel *qualityLabel = new QLabel("Quality:", this);
    qualityLabel->setToolTip(m_qualityCombo->toolTip());
    formLayout->addRow(qualityLabel, m_qualityCombo);

    m_convertToCombo = new QComboBox(this);
    m_convertToCombo->addItems({"None", "mp4", "mkv", "flv", "webm", "avi", "mov"});
    m_convertToCombo->setToolTip("Automatically convert the livestream to this format using FFmpeg after the download finishes.");
    QLabel *convertToLabel = new QLabel("Convert To:", this);
    convertToLabel->setToolTip(m_convertToCombo->toolTip());
    formLayout->addRow(convertToLabel, m_convertToCombo);

    mainLayout->addWidget(livestreamGroup);
    mainLayout->addStretch();

    // Connect signals for auto-saving
    connect(m_liveFromStartCheck, &ToggleSwitch::toggled, this, &LivestreamSettingsPage::saveSettings);
    connect(m_waitForVideoCheck, &ToggleSwitch::toggled, this, [this](bool checked) {
        m_waitMinSpin->setEnabled(checked);
        m_waitMaxSpin->setEnabled(checked);
        saveSettings();
    });
    connect(m_waitMinSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &LivestreamSettingsPage::saveSettings);
    connect(m_waitMaxSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &LivestreamSettingsPage::saveSettings);
    connect(m_usePartCheck, &ToggleSwitch::toggled, this, &LivestreamSettingsPage::saveSettings);
    connect(m_downloadAsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LivestreamSettingsPage::saveSettings);
    connect(m_qualityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LivestreamSettingsPage::saveSettings);
    connect(m_convertToCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LivestreamSettingsPage::saveSettings);
    
    connect(m_configManager, &ConfigManager::settingChanged, this, &LivestreamSettingsPage::handleConfigSettingChanged);
}

void LivestreamSettingsPage::loadSettings() {
    QSignalBlocker b1(m_liveFromStartCheck);
    QSignalBlocker b2(m_waitForVideoCheck);
    QSignalBlocker b3(m_waitMinSpin);
    QSignalBlocker b4(m_waitMaxSpin);
    QSignalBlocker b5(m_usePartCheck);
    QSignalBlocker b6(m_downloadAsCombo);
    QSignalBlocker b7(m_qualityCombo);
    QSignalBlocker b8(m_convertToCombo);

    m_liveFromStartCheck->setChecked(m_configManager->get("Livestream", "live_from_start", false).toBool());
    m_waitForVideoCheck->setChecked(m_configManager->get("Livestream", "wait_for_video", true).toBool());
    m_waitMinSpin->setValue(m_configManager->get("Livestream", "wait_for_video_min", 60).toInt());
    m_waitMaxSpin->setValue(m_configManager->get("Livestream", "wait_for_video_max", 300).toInt());
    m_usePartCheck->setChecked(m_configManager->get("Livestream", "use_part", true).toBool());
    m_downloadAsCombo->setCurrentText(m_configManager->get("Livestream", "download_as", "MPEG-TS").toString());
    m_qualityCombo->setCurrentText(m_configManager->get("Livestream", "quality", "best").toString());
    m_convertToCombo->setCurrentText(m_configManager->get("Livestream", "convert_to", "None").toString());
    
    m_waitMinSpin->setEnabled(m_waitForVideoCheck->isChecked());
    m_waitMaxSpin->setEnabled(m_waitForVideoCheck->isChecked());
}

void LivestreamSettingsPage::saveSettings() {
    m_configManager->set("Livestream", "live_from_start", m_liveFromStartCheck->isChecked());
    m_configManager->set("Livestream", "wait_for_video", m_waitForVideoCheck->isChecked());
    m_configManager->set("Livestream", "wait_for_video_min", m_waitMinSpin->value());
    m_configManager->set("Livestream", "wait_for_video_max", m_waitMaxSpin->value());
    m_configManager->set("Livestream", "use_part", m_usePartCheck->isChecked());
    m_configManager->set("Livestream", "download_as", m_downloadAsCombo->currentText());
    m_configManager->set("Livestream", "quality", m_qualityCombo->currentText());
    m_configManager->set("Livestream", "convert_to", m_convertToCombo->currentText());
}

void LivestreamSettingsPage::handleConfigSettingChanged(const QString &section, const QString &key, const QVariant &value) {
    if (section == "Livestream") {
        QSignalBlocker b1(m_liveFromStartCheck);
        QSignalBlocker b2(m_waitForVideoCheck);
        QSignalBlocker b3(m_waitMinSpin);
        QSignalBlocker b4(m_waitMaxSpin);
        QSignalBlocker b5(m_usePartCheck);
        QSignalBlocker b6(m_downloadAsCombo);
        QSignalBlocker b7(m_qualityCombo);
        QSignalBlocker b8(m_convertToCombo);

        if (key == "live_from_start") m_liveFromStartCheck->setChecked(value.toBool());
        else if (key == "wait_for_video") {
            m_waitForVideoCheck->setChecked(value.toBool());
            m_waitMinSpin->setEnabled(value.toBool());
            m_waitMaxSpin->setEnabled(value.toBool());
        }
        else if (key == "wait_for_video_min") m_waitMinSpin->setValue(value.toInt());
        else if (key == "wait_for_video_max") m_waitMaxSpin->setValue(value.toInt());
        else if (key == "use_part") m_usePartCheck->setChecked(value.toBool());
        else if (key == "download_as") m_downloadAsCombo->setCurrentText(value.toString());
        else if (key == "quality") m_qualityCombo->setCurrentText(value.toString());
        else if (key == "convert_to") m_convertToCombo->setCurrentText(value.toString());
    }
}