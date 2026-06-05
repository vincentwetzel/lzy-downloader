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
    if (codec.compare(QStringLiteral("H.264"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("H.264 (AVC)");
    }
    if (codec.compare(QStringLiteral("H.265"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("H.265 (HEVC)");
    }
    return codec;
}
}

VideoSettingsPage::VideoSettingsPage(ConfigManager *configManager, QWidget *parent)
    : QWidget(parent), m_configManager(configManager) {
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QGroupBox *videoGroup = new QGroupBox(tr("Default Video Settings"), this);
    videoGroup->setToolTip(tr("Set defaults for video downloads. Choose Select at Runtime when you want to pick exact streams for each queued URL."));
    QFormLayout *videoLayout = new QFormLayout(videoGroup);

    m_videoQualityCombo = new QComboBox(this);
    m_videoQualityCombo->setToolTip(tr("Choose the target video quality. 'best' lets yt-dlp choose the best available stream; Select at Runtime opens a format picker when you queue a URL."));
    m_videoQualityCombo->addItems({tr("Select at Runtime"), QStringLiteral("best"), QStringLiteral("2160p"), QStringLiteral("1440p"), QStringLiteral("1080p"), QStringLiteral("720p"), QStringLiteral("480p"), QStringLiteral("360p"), QStringLiteral("240p"), QStringLiteral("144p"), QStringLiteral("worst")});
    QLabel *qualityLabel = new QLabel(tr("Quality:"), this);
    qualityLabel->setToolTip(m_videoQualityCombo->toolTip());
    videoLayout->addRow(qualityLabel, m_videoQualityCombo);

    m_videoCodecLabel = new QLabel(tr("Codec:"), this);
    m_videoCodecLabel->setToolTip(tr("Choose the preferred video codec when runtime selection is off."));
    m_videoCodecCombo = new QComboBox(this);
    m_videoCodecCombo->setToolTip(tr("Choose the preferred video codec. H.264 is the safest for compatibility; H.265 and AV1 can be smaller but may not play everywhere."));
    m_videoCodecCombo->addItems({tr("Default"), QStringLiteral("H.264 (AVC)"), QStringLiteral("H.265 (HEVC)"), QStringLiteral("VP9"), QStringLiteral("AV1"), QStringLiteral("ProRes (Archive)"), QStringLiteral("Theora")});
    videoLayout->addRow(m_videoCodecLabel, m_videoCodecCombo);

    m_videoExtLabel = new QLabel(tr("Extension:"), this);
    m_videoExtLabel->setToolTip(tr("Choose the output container for the selected video codec."));
    m_videoExtCombo = new QComboBox(this);
    m_videoExtCombo->setToolTip(tr("Choose the final video file container. Available choices update automatically based on the selected codec."));
    m_videoExtCombo->addItems({QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("webm")});
    videoLayout->addRow(m_videoExtLabel, m_videoExtCombo);

    m_videoAudioCodecLabel = new QLabel(tr("Audio Codec:"), this);
    m_videoAudioCodecLabel->setToolTip(tr("Choose the preferred audio codec to include inside video downloads."));
    m_videoAudioCodecCombo = new QComboBox(this);
    m_videoAudioCodecCombo->setToolTip(tr("Choose the audio codec included in the final video file. Default lets yt-dlp pick the best compatible stream."));
    m_videoAudioCodecCombo->addItems({tr("Default"), QStringLiteral("AAC"), QStringLiteral("Opus"), QStringLiteral("Vorbis"), QStringLiteral("MP3"), QStringLiteral("FLAC"), QStringLiteral("PCM")});
    videoLayout->addRow(m_videoAudioCodecLabel, m_videoAudioCodecCombo);

    m_runtimeHintLabel = new QLabel(tr("Runtime selection mode is enabled. Quality, codec, extension, and stream choices will be picked from the available formats when you queue a download."), this);
    m_runtimeHintLabel->setWordWrap(true);
    m_runtimeHintLabel->setToolTip(tr("This mode uses a runtime format picker instead of the defaults below."));
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

    m_videoQualityCombo->setCurrentText(m_configManager->get(QStringLiteral("Video"), QStringLiteral("video_quality"), m_configManager->getDefault(QStringLiteral("Video"), QStringLiteral("video_quality"))).toString());
    m_videoCodecCombo->setCurrentText(canonicalVideoCodecValue(m_configManager->get(QStringLiteral("Video"), QStringLiteral("video_codec"), m_configManager->getDefault(QStringLiteral("Video"), QStringLiteral("video_codec"))).toString()));
    m_videoExtCombo->setCurrentText(m_configManager->get(QStringLiteral("Video"), QStringLiteral("video_extension"), m_configManager->getDefault(QStringLiteral("Video"), QStringLiteral("video_extension"))).toString());
    m_videoAudioCodecCombo->setCurrentText(m_configManager->get(QStringLiteral("Video"), QStringLiteral("video_audio_codec"), m_configManager->getDefault(QStringLiteral("Video"), QStringLiteral("video_audio_codec"))).toString());
    updateVideoOptions();
}

void VideoSettingsPage::onVideoQualityChanged(const QString &text) { m_configManager->set(QStringLiteral("Video"), QStringLiteral("video_quality"), text); }
void VideoSettingsPage::onVideoCodecChanged(const QString &text) { m_configManager->set(QStringLiteral("Video"), QStringLiteral("video_codec"), text); }
void VideoSettingsPage::onVideoExtChanged(const QString &text) { m_configManager->set(QStringLiteral("Video"), QStringLiteral("video_extension"), text); }
void VideoSettingsPage::onVideoAudioCodecChanged(const QString &text) { m_configManager->set(QStringLiteral("Video"), QStringLiteral("video_audio_codec"), text); }

void VideoSettingsPage::handleConfigSettingChanged(const QString &section, const QString &key, const QVariant &value) {
    if (section == QStringLiteral("Video")) {
        if (key == QStringLiteral("quality") || key == QStringLiteral("video_quality")) {
            QSignalBlocker b(m_videoQualityCombo);
            m_videoQualityCombo->setCurrentText(value.toString());
            updateVideoOptions();
        } else if (key == QStringLiteral("codec") || key == QStringLiteral("video_codec")) {
            QSignalBlocker b(m_videoCodecCombo);
            m_videoCodecCombo->setCurrentText(canonicalVideoCodecValue(value.toString()));
            updateVideoOptions();
        } else if (key == QStringLiteral("extension") || key == QStringLiteral("video_extension")) {
            QSignalBlocker b(m_videoExtCombo);
            m_videoExtCombo->setCurrentText(value.toString());
        } else if (key == QStringLiteral("audio_codec") || key == QStringLiteral("video_audio_codec")) {
            QSignalBlocker b(m_videoAudioCodecCombo);
            m_videoAudioCodecCombo->setCurrentText(value.toString());
        }
    }
}

bool VideoSettingsPage::isRuntimeSelectionMode() const {
    return m_videoQualityCombo && m_videoQualityCombo->currentText() == tr("Select at Runtime");
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
    bool isDefaultCodec = (selectedVideoCodec == tr("Default"));
    m_videoExtLabel->setVisible(!isDefaultCodec);
    m_videoExtCombo->setVisible(!isDefaultCodec);

    QString currentExt = m_videoExtCombo->currentText();
    if (!isDefaultCodec) {
        {
            QSignalBlocker b(m_videoExtCombo);
            m_videoExtCombo->clear();
            
            if (selectedVideoCodec == QStringLiteral("AV1") || selectedVideoCodec == QStringLiteral("VP9")) m_videoExtCombo->addItems({QStringLiteral("webm"), QStringLiteral("mkv")});
            else if (selectedVideoCodec == QStringLiteral("H.264 (AVC)") || selectedVideoCodec == QStringLiteral("H.265 (HEVC)")) m_videoExtCombo->addItems({QStringLiteral("mp4"), QStringLiteral("mkv")});
            else if (selectedVideoCodec == QStringLiteral("ProRes (Archive)")) m_videoExtCombo->addItem(QStringLiteral("mov"));
            else if (selectedVideoCodec == QStringLiteral("Theora")) m_videoExtCombo->addItem(QStringLiteral("ogv"));
            else m_videoExtCombo->addItems({QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("webm")});

            if (m_videoExtCombo->findText(currentExt) != -1) m_videoExtCombo->setCurrentText(currentExt);
            else m_videoExtCombo->setCurrentIndex(0);
        }
        if (m_videoExtCombo->currentText() != currentExt) onVideoExtChanged(m_videoExtCombo->currentText());
    }

    QString currentAudioCodec = m_videoAudioCodecCombo->currentText();
    {
        QSignalBlocker b(m_videoAudioCodecCombo);
        m_videoAudioCodecCombo->clear();
        
        if (selectedVideoCodec == QStringLiteral("AV1") || selectedVideoCodec == QStringLiteral("VP9")) m_videoAudioCodecCombo->addItems({tr("Default"), QStringLiteral("Opus"), QStringLiteral("Vorbis"), QStringLiteral("AAC")});
        else if (selectedVideoCodec == QStringLiteral("H.264 (AVC)") || selectedVideoCodec == QStringLiteral("H.265 (HEVC)")) m_videoAudioCodecCombo->addItems({tr("Default"), QStringLiteral("AAC"), QStringLiteral("MP3"), QStringLiteral("FLAC"), QStringLiteral("PCM")});
        else if (selectedVideoCodec == QStringLiteral("ProRes (Archive)")) m_videoAudioCodecCombo->addItems({tr("Default"), QStringLiteral("PCM"), QStringLiteral("AAC")});
        else if (selectedVideoCodec == QStringLiteral("Theora")) m_videoAudioCodecCombo->addItems({tr("Default"), QStringLiteral("Vorbis")});
        else m_videoAudioCodecCombo->addItems({tr("Default"), QStringLiteral("AAC"), QStringLiteral("Opus"), QStringLiteral("Vorbis"), QStringLiteral("MP3"), QStringLiteral("FLAC"), QStringLiteral("PCM")});

        if (m_videoAudioCodecCombo->findText(currentAudioCodec) != -1) m_videoAudioCodecCombo->setCurrentText(currentAudioCodec);
        else m_videoAudioCodecCombo->setCurrentIndex(0);
    }
    if (m_videoAudioCodecCombo->currentText() != currentAudioCodec) onVideoAudioCodecChanged(m_videoAudioCodecCombo->currentText());
}
