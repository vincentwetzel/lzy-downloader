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

    QGroupBox *livestreamGroup = new QGroupBox(tr("Livestream Settings"), this);
    livestreamGroup->setToolTip(tr("Set the default options for downloading livestreams, including scheduled streams."));
    QFormLayout *formLayout = new QFormLayout(livestreamGroup);
    formLayout->setSpacing(10);

    m_liveFromStartCheck = new ToggleSwitch(this);
    m_liveFromStartCheck->setToolTip(tr("If enabled, downloads the livestream from the start instead of the current live edge (where supported)."));
    QLabel *liveFromStartLabel = new QLabel(tr("Record from beginning of broadcast"), this);
    liveFromStartLabel->setWordWrap(true);
    liveFromStartLabel->setToolTip(m_liveFromStartCheck->toolTip());
    formLayout->addRow(liveFromStartLabel, m_liveFromStartCheck);

    QHBoxLayout *waitLayout = new QHBoxLayout();
    waitLayout->setContentsMargins(0, 0, 0, 0);
    m_waitForVideoCheck = new ToggleSwitch(this);
    m_waitForVideoCheck->setToolTip(tr("If the livestream hasn't started yet, keep retrying instead of failing immediately."));
    
    m_waitMinSpin = new QSpinBox(this);
    m_waitMinSpin->setRange(1, 3600);
    m_waitMinSpin->setSuffix(tr(" s"));
    m_waitMinSpin->setToolTip(tr("Minimum seconds to wait between retries."));
    
    m_waitMaxSpin = new QSpinBox(this);
    m_waitMaxSpin->setRange(1, 3600);
    m_waitMaxSpin->setSuffix(tr(" s"));
    m_waitMaxSpin->setToolTip(tr("Maximum seconds to wait between retries."));
    
    QLabel *waitLabel = new QLabel(tr("Wait for video to start"), this);
    waitLabel->setWordWrap(true);
    waitLabel->setToolTip(m_waitForVideoCheck->toolTip());
    waitLayout->addWidget(waitLabel);
    waitLayout->addWidget(m_waitForVideoCheck);
    waitLayout->addSpacing(15);
    QLabel *minLabel = new QLabel(tr("Min:"), this);
    minLabel->setToolTip(tr("Minimum seconds to wait between retries."));
    waitLayout->addWidget(minLabel);
    waitLayout->addWidget(m_waitMinSpin);
    QLabel *maxLabel = new QLabel(tr("Max:"), this);
    maxLabel->setToolTip(tr("Maximum seconds to wait between retries."));
    waitLayout->addWidget(maxLabel);
    waitLayout->addWidget(m_waitMaxSpin);
    waitLayout->addStretch();
    
    formLayout->addRow("", waitLayout);

    m_usePartCheck = new ToggleSwitch(this);
    m_usePartCheck->setToolTip(tr("Resume interrupted livestream downloads using .part files."));
    QLabel *usePartLabel = new QLabel(tr("Use .part files"), this);
    usePartLabel->setToolTip(m_usePartCheck->toolTip());
    formLayout->addRow(usePartLabel, m_usePartCheck);

    m_downloadAsCombo = new QComboBox(this);
    m_downloadAsCombo->addItems({QStringLiteral("MPEG-TS"), QStringLiteral("MKV")});
    m_downloadAsCombo->setToolTip(tr("Preferred container format for downloading the livestream."));
    QLabel *downloadAsLabel = new QLabel(tr("Download As:"), this);
    downloadAsLabel->setToolTip(m_downloadAsCombo->toolTip());
    formLayout->addRow(downloadAsLabel, m_downloadAsCombo);

    m_qualityCombo = new QComboBox(this);
    m_qualityCombo->addItems({QStringLiteral("best"), QStringLiteral("1080p"), QStringLiteral("720p"), QStringLiteral("480p"), QStringLiteral("360p"), QStringLiteral("worst")});
    m_qualityCombo->setToolTip(tr("Target resolution for the livestream."));
    QLabel *qualityLabel = new QLabel(tr("Quality:"), this);
    qualityLabel->setToolTip(m_qualityCombo->toolTip());
    formLayout->addRow(qualityLabel, m_qualityCombo);

    m_convertToCombo = new QComboBox(this);
    m_convertToCombo->addItems({tr("None"), QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("flv"), QStringLiteral("webm"), QStringLiteral("avi"), QStringLiteral("mov")});
    m_convertToCombo->setToolTip(tr("Automatically convert the livestream to this format using FFmpeg after the download finishes."));
    QLabel *convertToLabel = new QLabel(tr("Convert To:"), this);
    convertToLabel->setToolTip(m_convertToCombo->toolTip());
    formLayout->addRow(convertToLabel, m_convertToCombo);

    mainLayout->addWidget(livestreamGroup);
    mainLayout->addStretch();

    // Connect signals for auto-saving
    connect(m_liveFromStartCheck, &ToggleSwitch::toggled, this, [this](bool checked) { m_configManager->set(QStringLiteral("Livestream"), QStringLiteral("live_from_start"), checked); });
    connect(m_waitForVideoCheck, &ToggleSwitch::toggled, this, [this](bool checked) { m_configManager->set(QStringLiteral("Livestream"), QStringLiteral("wait_for_video"), checked); });
    connect(m_waitMinSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        m_configManager->set(QStringLiteral("Livestream"), QStringLiteral("wait_for_video_min"), value);
        if (m_waitMaxSpin->value() < value) {
            m_waitMaxSpin->setValue(value);
        }
    });
    connect(m_waitMaxSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        m_configManager->set(QStringLiteral("Livestream"), QStringLiteral("wait_for_video_max"), value);
        if (m_waitMinSpin->value() > value) {
            m_waitMinSpin->setValue(value);
        }
    });
    connect(m_usePartCheck, &ToggleSwitch::toggled, this, [this](bool checked) { m_configManager->set(QStringLiteral("Livestream"), QStringLiteral("use_part"), checked); });
    connect(m_downloadAsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { m_configManager->set(QStringLiteral("Livestream"), QStringLiteral("download_as"), m_downloadAsCombo->currentText()); });
    connect(m_qualityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { m_configManager->set(QStringLiteral("Livestream"), QStringLiteral("quality"), m_qualityCombo->currentText()); });
    connect(m_convertToCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { m_configManager->set(QStringLiteral("Livestream"), QStringLiteral("convert_to"), m_convertToCombo->currentText()); });
    
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

    m_liveFromStartCheck->setChecked(m_configManager->get(QStringLiteral("Livestream"), QStringLiteral("live_from_start"), false).toBool());
    m_waitForVideoCheck->setChecked(m_configManager->get(QStringLiteral("Livestream"), QStringLiteral("wait_for_video"), true).toBool());
    m_waitMinSpin->setValue(m_configManager->get(QStringLiteral("Livestream"), QStringLiteral("wait_for_video_min"), 60).toInt());
    m_waitMaxSpin->setValue(m_configManager->get(QStringLiteral("Livestream"), QStringLiteral("wait_for_video_max"), 300).toInt());
    m_usePartCheck->setChecked(m_configManager->get(QStringLiteral("Livestream"), QStringLiteral("use_part"), true).toBool());
    m_downloadAsCombo->setCurrentText(m_configManager->get(QStringLiteral("Livestream"), QStringLiteral("download_as"), QStringLiteral("MPEG-TS")).toString());
    m_qualityCombo->setCurrentText(m_configManager->get(QStringLiteral("Livestream"), QStringLiteral("quality"), QStringLiteral("best")).toString());
    m_convertToCombo->setCurrentText(m_configManager->get(QStringLiteral("Livestream"), QStringLiteral("convert_to"), tr("None")).toString());
    
    m_waitMinSpin->setEnabled(m_waitForVideoCheck->isChecked());
    m_waitMaxSpin->setEnabled(m_waitForVideoCheck->isChecked());
}

void LivestreamSettingsPage::saveSettings() {
    // Redundant: Settings are auto-saved via signals in setupUI() when changed
}

void LivestreamSettingsPage::handleConfigSettingChanged(const QString &section, const QString &key, const QVariant &value) {
    if (section == QStringLiteral("Livestream")) {
        QSignalBlocker b1(m_liveFromStartCheck);
        QSignalBlocker b2(m_waitForVideoCheck);
        QSignalBlocker b3(m_waitMinSpin);
        QSignalBlocker b4(m_waitMaxSpin);
        QSignalBlocker b5(m_usePartCheck);
        QSignalBlocker b6(m_downloadAsCombo);
        QSignalBlocker b7(m_qualityCombo);
        QSignalBlocker b8(m_convertToCombo);

        if (key == QStringLiteral("live_from_start")) m_liveFromStartCheck->setChecked(value.toBool());
        else if (key == QStringLiteral("wait_for_video")) {
            m_waitForVideoCheck->setChecked(value.toBool());
            m_waitMinSpin->setEnabled(value.toBool());
            m_waitMaxSpin->setEnabled(value.toBool());
        }
        else if (key == QStringLiteral("wait_for_video_min")) {
            m_waitMinSpin->setValue(value.toInt());
            if (m_waitMaxSpin->value() < value.toInt()) {
                m_waitMaxSpin->setValue(value.toInt());
                m_configManager->set(QStringLiteral("Livestream"), QStringLiteral("wait_for_video_max"), value.toInt());
            }
        }
        else if (key == QStringLiteral("wait_for_video_max")) {
            m_waitMaxSpin->setValue(value.toInt());
            if (m_waitMinSpin->value() > value.toInt()) {
                m_waitMinSpin->setValue(value.toInt());
                m_configManager->set(QStringLiteral("Livestream"), QStringLiteral("wait_for_video_min"), value.toInt());
            }
        }
        else if (key == QStringLiteral("use_part")) m_usePartCheck->setChecked(value.toBool());
        else if (key == QStringLiteral("download_as")) m_downloadAsCombo->setCurrentText(value.toString());
        else if (key == QStringLiteral("quality")) m_qualityCombo->setCurrentText(value.toString());
        else if (key == QStringLiteral("convert_to")) m_convertToCombo->setCurrentText(value.toString());
    }
}