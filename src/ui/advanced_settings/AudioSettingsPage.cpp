#include "AudioSettingsPage.h"
#include "core/ConfigManager.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QSignalBlocker>

AudioSettingsPage::AudioSettingsPage(ConfigManager *configManager, QWidget *parent)
    : QWidget(parent), m_configManager(configManager) {
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QGroupBox *audioGroup = new QGroupBox("Default Audio Settings", this);
    audioGroup->setToolTip("Set the default audio-only download options, or switch to runtime selection to choose exact formats when each download is queued.");
    QFormLayout *audioLayout = new QFormLayout(audioGroup);

    m_audioQualityCombo = new QComboBox(this);
    m_audioQualityCombo->setToolTip("Pick the default audio quality. Choose 'Select at Runtime' to hide the rest of these defaults and pick exact formats when you queue a download.");
    m_audioQualityCombo->addItems({"Select at Runtime", "best", "320k", "256k", "192k", "128k", "96k", "64k", "32k", "worst"});
    QLabel *qualityLabel = new QLabel("Quality:", this);
    qualityLabel->setToolTip(m_audioQualityCombo->toolTip());
    audioLayout->addRow(qualityLabel, m_audioQualityCombo);

    m_audioCodecLabel = new QLabel("Codec:", this);
    m_audioCodecLabel->setToolTip("Choose the default audio codec used when runtime selection is off.");
    m_audioCodecCombo = new QComboBox(this);
    m_audioCodecCombo->setToolTip("Choose the audio format (codec). Opus is modern and efficient, MP3 is very common, FLAC is for lossless quality.");
    m_audioCodecCombo->addItems({"Default", "Opus", "AAC", "Vorbis", "MP3", "FLAC", "WAV", "ALAC", "AC3", "EAC3", "DTS", "PCM"});
    audioLayout->addRow(m_audioCodecLabel, m_audioCodecCombo);

    m_audioExtLabel = new QLabel("Extension:", this);
    m_audioExtLabel->setToolTip("Select the default file extension used when runtime selection is off.");
    m_audioExtCombo = new QComboBox(this);
    m_audioExtCombo->setToolTip("Select the file type for your audio... This changes automatically based on the audio codec.");
    m_audioExtCombo->addItems({"mp3", "m4a", "opus", "wav", "flac"});
    audioLayout->addRow(m_audioExtLabel, m_audioExtCombo);

    m_runtimeHintLabel = new QLabel("Runtime selection mode is enabled. Quality, codec, extension, and stream choices will be picked from the available formats when you queue a download.", this);
    m_runtimeHintLabel->setWordWrap(true);
    m_runtimeHintLabel->setToolTip("This mode uses a runtime format picker instead of the defaults below.");
    audioLayout->addRow(QString(), m_runtimeHintLabel);

    layout->addWidget(audioGroup);
    layout->addStretch();

    connect(m_audioQualityCombo, &QComboBox::currentTextChanged, this, &AudioSettingsPage::onAudioQualityChanged);
    connect(m_audioCodecCombo, &QComboBox::currentTextChanged, this, &AudioSettingsPage::onAudioCodecChanged);
    connect(m_audioExtCombo, &QComboBox::currentTextChanged, this, &AudioSettingsPage::onAudioExtChanged);
    connect(m_configManager, &ConfigManager::settingChanged, this, &AudioSettingsPage::handleConfigSettingChanged);
}

void AudioSettingsPage::loadSettings() {
    QSignalBlocker b1(m_audioQualityCombo);
    QSignalBlocker b2(m_audioCodecCombo);
    QSignalBlocker b3(m_audioExtCombo);

    m_audioQualityCombo->setCurrentText(m_configManager->get("Audio", "audio_quality", m_configManager->getDefault("Audio", "audio_quality")).toString());
    m_audioCodecCombo->setCurrentText(m_configManager->get("Audio", "audio_codec", m_configManager->getDefault("Audio", "audio_codec")).toString());
    m_audioExtCombo->setCurrentText(m_configManager->get("Audio", "audio_extension", m_configManager->getDefault("Audio", "audio_extension")).toString());
    updateAudioOptions();
}

void AudioSettingsPage::onAudioQualityChanged(const QString &text) { m_configManager->set("Audio", "audio_quality", text); }
void AudioSettingsPage::onAudioCodecChanged(const QString &text) { m_configManager->set("Audio", "audio_codec", text); }
void AudioSettingsPage::onAudioExtChanged(const QString &text) { m_configManager->set("Audio", "audio_extension", text); }

void AudioSettingsPage::handleConfigSettingChanged(const QString &section, const QString &key, const QVariant &value) {
    if (section == "Audio") {
        if (key == "quality" || key == "audio_quality") {
            QSignalBlocker b(m_audioQualityCombo);
            m_audioQualityCombo->setCurrentText(value.toString());
            updateAudioOptions();
        } else if (key == "codec" || key == "audio_codec") {
            QSignalBlocker b(m_audioCodecCombo);
            m_audioCodecCombo->setCurrentText(value.toString());
            updateAudioOptions();
        } else if (key == "extension" || key == "audio_extension") {
            QSignalBlocker b(m_audioExtCombo);
            m_audioExtCombo->setCurrentText(value.toString());
        }
    }
}

bool AudioSettingsPage::isRuntimeSelectionMode() const {
    return m_audioQualityCombo && m_audioQualityCombo->currentText() == "Select at Runtime";
}

void AudioSettingsPage::updateAudioOptions() {
    const bool runtimeSelection = isRuntimeSelectionMode();
    m_audioCodecLabel->setVisible(!runtimeSelection);
    m_audioCodecCombo->setVisible(!runtimeSelection);
    m_audioExtLabel->setVisible(!runtimeSelection);
    m_audioExtCombo->setVisible(!runtimeSelection);
    m_runtimeHintLabel->setVisible(runtimeSelection);

    if (runtimeSelection) {
        return;
    }

    QString selectedAudioCodec = m_audioCodecCombo->currentText();
    bool isDefaultCodec = (selectedAudioCodec == "Default");
    m_audioExtLabel->setVisible(!isDefaultCodec);
    m_audioExtCombo->setVisible(!isDefaultCodec);
    if (isDefaultCodec) return;

    QString currentExt = m_audioExtCombo->currentText();
    {
        QSignalBlocker b(m_audioExtCombo);
        m_audioExtCombo->clear();
        
        if (selectedAudioCodec == "AAC") m_audioExtCombo->addItems({"m4a", "aac"});
        else if (selectedAudioCodec == "Opus") m_audioExtCombo->addItem("opus");
        else if (selectedAudioCodec == "Vorbis") m_audioExtCombo->addItem("ogg");
        else if (selectedAudioCodec == "MP3") m_audioExtCombo->addItem("mp3");
        else if (selectedAudioCodec == "FLAC") m_audioExtCombo->addItem("flac");
        else if (selectedAudioCodec == "WAV" || selectedAudioCodec == "PCM") m_audioExtCombo->addItem("wav");
        else if (selectedAudioCodec == "ALAC") m_audioExtCombo->addItems({"m4a", "alac"});
        else if (selectedAudioCodec == "AC3" || selectedAudioCodec == "EAC3" || selectedAudioCodec == "DTS") m_audioExtCombo->addItems({"ac3", "eac3", "dts"});
        else m_audioExtCombo->addItems({"mp3", "m4a", "opus", "wav", "flac"});

        if (m_audioExtCombo->findText(currentExt) != -1) m_audioExtCombo->setCurrentText(currentExt);
        else m_audioExtCombo->setCurrentIndex(0);
    }
    if (m_audioExtCombo->currentText() != currentExt) onAudioExtChanged(m_audioExtCombo->currentText());
}
