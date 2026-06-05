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

    QGroupBox *audioGroup = new QGroupBox(tr("Default Audio Settings"), this);
    audioGroup->setToolTip(tr("Set the default audio-only download options, or switch to runtime selection to choose exact formats when each download is queued."));
    QFormLayout *audioLayout = new QFormLayout(audioGroup);

    m_audioQualityCombo = new QComboBox(this);
    m_audioQualityCombo->setToolTip(tr("Pick the default audio quality. Choose 'Select at Runtime' to hide the rest of these defaults and pick exact formats when you queue a download."));
    m_audioQualityCombo->addItems({tr("Select at Runtime"), QStringLiteral("best"), QStringLiteral("320k"), QStringLiteral("256k"), QStringLiteral("192k"), QStringLiteral("128k"), QStringLiteral("96k"), QStringLiteral("64k"), QStringLiteral("32k"), QStringLiteral("worst")});
    QLabel *qualityLabel = new QLabel(tr("Quality:"), this);
    qualityLabel->setToolTip(m_audioQualityCombo->toolTip());
    audioLayout->addRow(qualityLabel, m_audioQualityCombo);

    m_audioCodecLabel = new QLabel(tr("Codec:"), this);
    m_audioCodecLabel->setToolTip(tr("Choose the default audio codec used when runtime selection is off."));
    m_audioCodecCombo = new QComboBox(this);
    m_audioCodecCombo->setToolTip(tr("Choose the audio format (codec). Opus is modern and efficient, MP3 is very common, FLAC is for lossless quality."));
    m_audioCodecCombo->addItems({tr("Default"), QStringLiteral("Opus"), QStringLiteral("AAC"), QStringLiteral("Vorbis"), QStringLiteral("MP3"), QStringLiteral("FLAC"), QStringLiteral("WAV"), QStringLiteral("ALAC"), QStringLiteral("AC3"), QStringLiteral("EAC3"), QStringLiteral("DTS"), QStringLiteral("PCM")});
    audioLayout->addRow(m_audioCodecLabel, m_audioCodecCombo);

    m_audioExtLabel = new QLabel(tr("Extension:"), this);
    m_audioExtLabel->setToolTip(tr("Select the default file extension used when runtime selection is off."));
    m_audioExtCombo = new QComboBox(this);
    m_audioExtCombo->setToolTip(tr("Select the file type for your audio... This changes automatically based on the audio codec."));
    m_audioExtCombo->addItems({QStringLiteral("mp3"), QStringLiteral("m4a"), QStringLiteral("opus"), QStringLiteral("wav"), QStringLiteral("flac")});
    audioLayout->addRow(m_audioExtLabel, m_audioExtCombo);

    m_runtimeHintLabel = new QLabel(tr("Runtime selection mode is enabled. Quality, codec, extension, and stream choices will be picked from the available formats when you queue a download."), this);
    m_runtimeHintLabel->setWordWrap(true);
    m_runtimeHintLabel->setToolTip(tr("This mode uses a runtime format picker instead of the defaults below."));
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

    m_audioQualityCombo->setCurrentText(m_configManager->get(QStringLiteral("Audio"), QStringLiteral("audio_quality"), m_configManager->getDefault(QStringLiteral("Audio"), QStringLiteral("audio_quality"))).toString());
    m_audioCodecCombo->setCurrentText(m_configManager->get(QStringLiteral("Audio"), QStringLiteral("audio_codec"), m_configManager->getDefault(QStringLiteral("Audio"), QStringLiteral("audio_codec"))).toString());
    m_audioExtCombo->setCurrentText(m_configManager->get(QStringLiteral("Audio"), QStringLiteral("audio_extension"), m_configManager->getDefault(QStringLiteral("Audio"), QStringLiteral("audio_extension"))).toString());
    updateAudioOptions();
}

void AudioSettingsPage::onAudioQualityChanged(const QString &text) { m_configManager->set(QStringLiteral("Audio"), QStringLiteral("audio_quality"), text); }
void AudioSettingsPage::onAudioCodecChanged(const QString &text) { m_configManager->set(QStringLiteral("Audio"), QStringLiteral("audio_codec"), text); }
void AudioSettingsPage::onAudioExtChanged(const QString &text) { m_configManager->set(QStringLiteral("Audio"), QStringLiteral("audio_extension"), text); }

void AudioSettingsPage::handleConfigSettingChanged(const QString &section, const QString &key, const QVariant &value) {
    if (section == QStringLiteral("Audio")) {
        if (key == QStringLiteral("quality") || key == QStringLiteral("audio_quality")) {
            QSignalBlocker b(m_audioQualityCombo);
            m_audioQualityCombo->setCurrentText(value.toString());
            updateAudioOptions();
        } else if (key == QStringLiteral("codec") || key == QStringLiteral("audio_codec")) {
            QSignalBlocker b(m_audioCodecCombo);
            m_audioCodecCombo->setCurrentText(value.toString());
            updateAudioOptions();
        } else if (key == QStringLiteral("extension") || key == QStringLiteral("audio_extension")) {
            QSignalBlocker b(m_audioExtCombo);
            m_audioExtCombo->setCurrentText(value.toString());
        }
    }
}

bool AudioSettingsPage::isRuntimeSelectionMode() const {
    return m_audioQualityCombo && m_audioQualityCombo->currentText() == tr("Select at Runtime");
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
    bool isDefaultCodec = (selectedAudioCodec == tr("Default"));
    m_audioExtLabel->setVisible(!isDefaultCodec);
    m_audioExtCombo->setVisible(!isDefaultCodec);
    if (isDefaultCodec) return;

    QString currentExt = m_audioExtCombo->currentText();
    {
        QSignalBlocker b(m_audioExtCombo);
        m_audioExtCombo->clear();
        
        if (selectedAudioCodec == QStringLiteral("AAC")) m_audioExtCombo->addItems({QStringLiteral("m4a"), QStringLiteral("aac")});
        else if (selectedAudioCodec == QStringLiteral("Opus")) m_audioExtCombo->addItem(QStringLiteral("opus"));
        else if (selectedAudioCodec == QStringLiteral("Vorbis")) m_audioExtCombo->addItem(QStringLiteral("ogg"));
        else if (selectedAudioCodec == QStringLiteral("MP3")) m_audioExtCombo->addItem(QStringLiteral("mp3"));
        else if (selectedAudioCodec == QStringLiteral("FLAC")) m_audioExtCombo->addItem(QStringLiteral("flac"));
        else if (selectedAudioCodec == QStringLiteral("WAV") || selectedAudioCodec == QStringLiteral("PCM")) m_audioExtCombo->addItem(QStringLiteral("wav"));
        else if (selectedAudioCodec == QStringLiteral("ALAC")) m_audioExtCombo->addItems({QStringLiteral("m4a"), QStringLiteral("alac")});
        else if (selectedAudioCodec == QStringLiteral("AC3") || selectedAudioCodec == QStringLiteral("EAC3") || selectedAudioCodec == QStringLiteral("DTS")) m_audioExtCombo->addItems({QStringLiteral("ac3"), QStringLiteral("eac3"), QStringLiteral("dts")});
        else m_audioExtCombo->addItems({QStringLiteral("mp3"), QStringLiteral("m4a"), QStringLiteral("opus"), QStringLiteral("wav"), QStringLiteral("flac")});

        if (m_audioExtCombo->findText(currentExt) != -1) m_audioExtCombo->setCurrentText(currentExt);
        else m_audioExtCombo->setCurrentIndex(0);
    }
    if (m_audioExtCombo->currentText() != currentExt) onAudioExtChanged(m_audioExtCombo->currentText());
}
