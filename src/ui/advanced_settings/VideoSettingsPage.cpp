#include "VideoSettingsPage.h"
#include "core/ConfigManager.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QSignalBlocker>

namespace {
QString canonicalVideoCodecValue(QString codec)
{
    codec = codec.trimmed();
    if (codec.compare("H.264", Qt::CaseInsensitive) == 0) {
        return "H.264 (AVC)";
    }
    if (codec.compare("H.265", Qt::CaseInsensitive) == 0) {
        return "H.265 (HEVC)";
    }
    return codec;
}
}

VideoSettingsPage::VideoSettingsPage(ConfigManager *configManager, QWidget *parent)
    : QWidget(parent), m_configManager(configManager) {
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QGroupBox *videoGroup = new QGroupBox("Default Video Settings", this);
    videoGroup->setToolTip("Set the default video download options, or switch to runtime selection to choose formats when each download is added.");
    QFormLayout *videoLayout = new QFormLayout(videoGroup);

    m_videoQualityCombo = new QComboBox(this);
    m_videoQualityCombo->setToolTip("Pick the default picture quality for video downloads. Choose 'Select at Runtime' to hide the rest of these defaults and pick exact formats when you queue a download.");
    m_videoQualityCombo->addItems({"Select at Runtime", "best", "2160p", "1440p", "1080p", "720p", "480p", "360p", "240p", "144p", "worst"});
    QLabel *qualityLabel = new QLabel("Quality:", this);
    qualityLabel->setToolTip(m_videoQualityCombo->toolTip());
    videoLayout->addRow(qualityLabel, m_videoQualityCombo);

    m_videoCodecLabel = new QLabel("Codec:", this);
    m_videoCodecLabel->setToolTip("Choose the default video codec used when runtime selection is off.");
    m_videoCodecCombo = new QComboBox(this);
    m_videoCodecCombo->setToolTip("Choose the video format (codec). This affects file size and compatibility. H.264 is common, H.265 is newer and smaller, AV1/VP9 are often used for web videos.");
    m_videoCodecCombo->addItems({"Default", "H.264 (AVC)", "H.265 (HEVC)", "VP9", "AV1", "ProRes (Archive)", "Theora"});
    videoLayout->addRow(m_videoCodecLabel, m_videoCodecCombo);

    m_videoExtLabel = new QLabel("Extension:", this);
    m_videoExtLabel->setToolTip("Select the file type for your video... changes automatically based on codec.");
    m_videoExtCombo = new QComboBox(this);
    m_videoExtCombo->setToolTip("Select the file type for your video... changes automatically based on codec.");
    m_videoExtCombo->addItems({"mp4", "mkv", "webm"});
    videoLayout->addRow(m_videoExtLabel, m_videoExtCombo);

    m_videoAudioCodecLabel = new QLabel("Audio Codec:", this);
    m_videoAudioCodecLabel->setToolTip("Choose the default audio codec used inside video downloads when runtime selection is off.");
    m_videoAudioCodecCombo = new QComboBox(this);
    m_videoAudioCodecCombo->setToolTip("Choose the audio format (codec) that will be included in your video file.");
    m_videoAudioCodecCombo->addItems({"Default", "AAC", "Opus", "Vorbis", "MP3", "FLAC", "PCM"});
    videoLayout->addRow(m_videoAudioCodecLabel, m_videoAudioCodecCombo);

    m_runtimeHintLabel = new QLabel("Runtime selection mode is enabled. Quality, codec, extension, and stream choices will be picked from the available formats when you queue a download.", this);
    m_runtimeHintLabel->setWordWrap(true);
    m_runtimeHintLabel->setToolTip("This mode uses a runtime format picker instead of the defaults below.");
    videoLayout->addRow(QString(), m_runtimeHintLabel);

    layout->addWidget(videoGroup);
    layout->addStretch();

    connect(m_videoQualityCombo, &QComboBox::currentTextChanged, this, &VideoSettingsPage::onVideoQualityChanged);
    connect(m_videoCodecCombo, &QComboBox::currentTextChanged, this, &VideoSettingsPage::onVideoCodecChanged);
    connect(m_videoExtCombo, &QComboBox::currentTextChanged, this, &VideoSettingsPage::onVideoExtChanged);
    connect(m_videoAudioCodecCombo, &QComboBox::currentTextChanged, this, &VideoSettingsPage::onVideoAudioCodecChanged);
    connect(m_configManager, &ConfigManager::settingChanged, this, &VideoSettingsPage::handleConfigSettingChanged);
}

void VideoSettingsPage::loadSettings() {
    QSignalBlocker b1(m_videoQualityCombo);
    QSignalBlocker b2(m_videoCodecCombo);
    QSignalBlocker b3(m_videoExtCombo);
    QSignalBlocker b4(m_videoAudioCodecCombo);

    m_videoQualityCombo->setCurrentText(m_configManager->get("Video", "video_quality", m_configManager->getDefault("Video", "video_quality")).toString());
    m_videoCodecCombo->setCurrentText(canonicalVideoCodecValue(m_configManager->get("Video", "video_codec", m_configManager->getDefault("Video", "video_codec")).toString()));
    m_videoExtCombo->setCurrentText(m_configManager->get("Video", "video_extension", m_configManager->getDefault("Video", "video_extension")).toString());
    m_videoAudioCodecCombo->setCurrentText(m_configManager->get("Video", "video_audio_codec", m_configManager->getDefault("Video", "video_audio_codec")).toString());
    updateVideoOptions();
}

void VideoSettingsPage::onVideoQualityChanged(const QString &text) { m_configManager->set("Video", "video_quality", text); }
void VideoSettingsPage::onVideoCodecChanged(const QString &text) { m_configManager->set("Video", "video_codec", text); }
void VideoSettingsPage::onVideoExtChanged(const QString &text) { m_configManager->set("Video", "video_extension", text); }
void VideoSettingsPage::onVideoAudioCodecChanged(const QString &text) { m_configManager->set("Video", "video_audio_codec", text); }

void VideoSettingsPage::handleConfigSettingChanged(const QString &section, const QString &key, const QVariant &value) {
    if (section == "Video") {
        if (key == "quality" || key == "video_quality") {
            QSignalBlocker b(m_videoQualityCombo);
            m_videoQualityCombo->setCurrentText(value.toString());
            updateVideoOptions();
        } else if (key == "codec" || key == "video_codec") {
            QSignalBlocker b(m_videoCodecCombo);
            m_videoCodecCombo->setCurrentText(canonicalVideoCodecValue(value.toString()));
            updateVideoOptions();
        } else if (key == "extension" || key == "video_extension") {
            QSignalBlocker b(m_videoExtCombo);
            m_videoExtCombo->setCurrentText(value.toString());
        } else if (key == "audio_codec" || key == "video_audio_codec") {
            QSignalBlocker b(m_videoAudioCodecCombo);
            m_videoAudioCodecCombo->setCurrentText(value.toString());
        }
    }
}

bool VideoSettingsPage::isRuntimeSelectionMode() const {
    return m_videoQualityCombo && m_videoQualityCombo->currentText() == "Select at Runtime";
}

void VideoSettingsPage::updateVideoOptions() {
    const bool runtimeSelection = isRuntimeSelectionMode();
    m_videoCodecLabel->setVisible(!runtimeSelection);
    m_videoCodecCombo->setVisible(!runtimeSelection);
    m_videoExtLabel->setVisible(!runtimeSelection);
    m_videoExtCombo->setVisible(!runtimeSelection);
    m_videoAudioCodecLabel->setVisible(!runtimeSelection);
    m_videoAudioCodecCombo->setVisible(!runtimeSelection);
    m_runtimeHintLabel->setVisible(runtimeSelection);

    if (runtimeSelection) {
        return;
    }

    QString selectedVideoCodec = m_videoCodecCombo->currentText();
    bool isDefaultCodec = (selectedVideoCodec == "Default");
    m_videoExtLabel->setVisible(!isDefaultCodec);
    m_videoExtCombo->setVisible(!isDefaultCodec);

    QString currentExt = m_videoExtCombo->currentText();
    if (!isDefaultCodec) {
        {
            QSignalBlocker b(m_videoExtCombo);
            m_videoExtCombo->clear();
            
            if (selectedVideoCodec == "AV1" || selectedVideoCodec == "VP9") m_videoExtCombo->addItems({"webm", "mkv"});
            else if (selectedVideoCodec == "H.264 (AVC)" || selectedVideoCodec == "H.265 (HEVC)") m_videoExtCombo->addItems({"mp4", "mkv"});
            else if (selectedVideoCodec == "ProRes (Archive)") m_videoExtCombo->addItem("mov");
            else if (selectedVideoCodec == "Theora") m_videoExtCombo->addItem("ogv");
            else m_videoExtCombo->addItems({"mp4", "mkv", "webm"});

            if (m_videoExtCombo->findText(currentExt) != -1) m_videoExtCombo->setCurrentText(currentExt);
            else m_videoExtCombo->setCurrentIndex(0);
        }
        if (m_videoExtCombo->currentText() != currentExt) onVideoExtChanged(m_videoExtCombo->currentText());
    }

    QString currentAudioCodec = m_videoAudioCodecCombo->currentText();
    {
        QSignalBlocker b(m_videoAudioCodecCombo);
        m_videoAudioCodecCombo->clear();
        
        if (selectedVideoCodec == "AV1" || selectedVideoCodec == "VP9") m_videoAudioCodecCombo->addItems({"Default", "Opus", "Vorbis", "AAC"});
        else if (selectedVideoCodec == "H.264 (AVC)" || selectedVideoCodec == "H.265 (HEVC)") m_videoAudioCodecCombo->addItems({"Default", "AAC", "MP3", "FLAC", "PCM"});
        else if (selectedVideoCodec == "ProRes (Archive)") m_videoAudioCodecCombo->addItems({"Default", "PCM", "AAC"});
        else if (selectedVideoCodec == "Theora") m_videoAudioCodecCombo->addItems({"Default", "Vorbis"});
        else m_videoAudioCodecCombo->addItems({"Default", "AAC", "Opus", "Vorbis", "MP3", "FLAC", "PCM"});

        if (m_videoAudioCodecCombo->findText(currentAudioCodec) != -1) m_videoAudioCodecCombo->setCurrentText(currentAudioCodec);
        else m_videoAudioCodecCombo->setCurrentIndex(0);
    }
    if (m_videoAudioCodecCombo->currentText() != currentAudioCodec) onVideoAudioCodecChanged(m_videoAudioCodecCombo->currentText());
}
