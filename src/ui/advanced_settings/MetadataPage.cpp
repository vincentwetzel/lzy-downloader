#include "MetadataPage.h"
#include "core/ConfigManager.h"
#include "ui/ToggleSwitch.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QLabel>
#include <QSignalBlocker>

MetadataPage::MetadataPage(ConfigManager *configManager, QWidget *parent)
    : QWidget(parent), m_configManager(configManager) {
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QGroupBox *metadataGroup = new QGroupBox("Metadata / Thumbnails", this);
    metadataGroup->setToolTip("Control how metadata and thumbnails are handled for downloaded files.");
    QFormLayout *metadataLayout = new QFormLayout(metadataGroup);

    m_embedMetadataCheck = new ToggleSwitch(this);
    m_embedMetadataCheck->setToolTip("Embed media metadata (title, artist, etc.) into the final downloaded file.");
    m_embedThumbnailCheck = new ToggleSwitch(this);
    m_embedThumbnailCheck->setToolTip("Embed the downloaded thumbnail/artwork directly into the media file.");
    m_highQualityThumbnailCheck = new ToggleSwitch(this);
    m_highQualityThumbnailCheck->setToolTip("Request a higher quality thumbnail extraction when converting artwork.");
    m_cropThumbnailCheck = new ToggleSwitch(this);
    m_cropThumbnailCheck->setToolTip("Crop rectangular video thumbnails into a 1:1 square for better audio-player compatibility.");
    m_generateFolderJpgCheck = new ToggleSwitch(this);
    m_generateFolderJpgCheck->setToolTip("Save the playlist's thumbnail as 'folder.jpg' in the output directory alongside the audio files.");
    m_forcePlaylistAsAlbumSwitch = new ToggleSwitch(this);
    m_convertThumbnailsCombo = new QComboBox(this);
    m_convertThumbnailsCombo->setToolTip("Convert the downloaded thumbnail to a specific image format.");
    m_convertThumbnailsCombo->addItems({"None", "jpg", "png"});

    m_forcePlaylistAsAlbumSwitch->setToolTip("When enabled for audio playlist downloads, this forces the 'album' metadata tag\n"
                                             "to be the playlist's title and sets the 'album_artist' tag to 'Various Artists'.\n\n"
                                             "This ensures local music players group all tracks from the playlist into a single, cohesive album.");

    auto addFormRow = [&](const QString& labelText, QWidget* field) {
        QLabel* label = new QLabel(labelText, this);
        label->setToolTip(field->toolTip());
        metadataLayout->addRow(label, field);
    };

    addFormRow("Embed metadata", m_embedMetadataCheck);
    addFormRow("Embed thumbnail", m_embedThumbnailCheck);
    addFormRow("Use high-quality thumbnail converter", m_highQualityThumbnailCheck);
    addFormRow("Crop audio thumbnails to square", m_cropThumbnailCheck);
    addFormRow("Generate folder.jpg for audio playlists", m_generateFolderJpgCheck);
    addFormRow("Force Playlist as Single Album", m_forcePlaylistAsAlbumSwitch);
    addFormRow("Convert thumbnails to:", m_convertThumbnailsCombo);

    layout->addWidget(metadataGroup);
    layout->addStretch();

    connect(m_embedMetadataCheck, &ToggleSwitch::toggled, this, &MetadataPage::onEmbedMetadataToggled);
    connect(m_embedThumbnailCheck, &ToggleSwitch::toggled, this, &MetadataPage::onEmbedThumbnailToggled);
    connect(m_highQualityThumbnailCheck, &ToggleSwitch::toggled, this, &MetadataPage::onHighQualityThumbnailToggled);
    connect(m_cropThumbnailCheck, &ToggleSwitch::toggled, this, &MetadataPage::onCropThumbnailToggled);
    connect(m_generateFolderJpgCheck, &ToggleSwitch::toggled, this, &MetadataPage::onGenerateFolderJpgToggled);
    connect(m_forcePlaylistAsAlbumSwitch, &ToggleSwitch::toggled, this, &MetadataPage::onForcePlaylistAsAlbumToggled);
    connect(m_convertThumbnailsCombo, &QComboBox::currentTextChanged, this, &MetadataPage::onConvertThumbnailsChanged);
    connect(m_configManager, &ConfigManager::settingChanged, this, &MetadataPage::handleConfigSettingChanged);
}

void MetadataPage::loadSettings() {
    QSignalBlocker b1(m_embedMetadataCheck);
    QSignalBlocker b2(m_embedThumbnailCheck);
    QSignalBlocker b3(m_highQualityThumbnailCheck);
    QSignalBlocker b4(m_convertThumbnailsCombo);
    QSignalBlocker b5(m_cropThumbnailCheck);
    QSignalBlocker b6(m_generateFolderJpgCheck);
    QSignalBlocker b7(m_forcePlaylistAsAlbumSwitch);

    m_embedMetadataCheck->setChecked(m_configManager->get("Metadata", "embed_metadata", true).toBool());
    m_embedThumbnailCheck->setChecked(m_configManager->get("Metadata", "embed_thumbnail", true).toBool());
    m_highQualityThumbnailCheck->setChecked(m_configManager->get("Metadata", "high_quality_thumbnail", true).toBool());
    m_cropThumbnailCheck->setChecked(m_configManager->get("Metadata", "crop_artwork_to_square", true).toBool());
    m_generateFolderJpgCheck->setChecked(m_configManager->get("Metadata", "generate_folder_jpg", false).toBool());
    m_forcePlaylistAsAlbumSwitch->setChecked(m_configManager->get("Metadata", "force_playlist_as_album", false).toBool());
    m_convertThumbnailsCombo->setCurrentText(m_configManager->get("Metadata", "convert_thumbnail_to", "jpg").toString());
}
void MetadataPage::onEmbedMetadataToggled(bool c) { m_configManager->set("Metadata", "embed_metadata", c); }
void MetadataPage::onEmbedThumbnailToggled(bool c) { m_configManager->set("Metadata", "embed_thumbnail", c); }
void MetadataPage::onHighQualityThumbnailToggled(bool c) { m_configManager->set("Metadata", "high_quality_thumbnail", c); }
void MetadataPage::onCropThumbnailToggled(bool c) { m_configManager->set("Metadata", "crop_artwork_to_square", c); }
void MetadataPage::onGenerateFolderJpgToggled(bool c) { m_configManager->set("Metadata", "generate_folder_jpg", c); }
void MetadataPage::onForcePlaylistAsAlbumToggled(bool c) { m_configManager->set("Metadata", "force_playlist_as_album", c); }
void MetadataPage::onConvertThumbnailsChanged(const QString &text) { m_configManager->set("Metadata", "convert_thumbnail_to", text); }

void MetadataPage::handleConfigSettingChanged(const QString &section, const QString &key, const QVariant &value) {
    if (section != "Metadata") return;
    
    if (key == "embed_metadata") { QSignalBlocker b(m_embedMetadataCheck); m_embedMetadataCheck->setChecked(value.toBool()); }
    else if (key == "embed_thumbnail") { QSignalBlocker b(m_embedThumbnailCheck); m_embedThumbnailCheck->setChecked(value.toBool()); }
    else if (key == "high_quality_thumbnail") { QSignalBlocker b(m_highQualityThumbnailCheck); m_highQualityThumbnailCheck->setChecked(value.toBool()); }
    else if (key == "crop_artwork_to_square") { QSignalBlocker b(m_cropThumbnailCheck); m_cropThumbnailCheck->setChecked(value.toBool()); }
    else if (key == "generate_folder_jpg") { QSignalBlocker b(m_generateFolderJpgCheck); m_generateFolderJpgCheck->setChecked(value.toBool()); }
    else if (key == "force_playlist_as_album") { QSignalBlocker b(m_forcePlaylistAsAlbumSwitch); m_forcePlaylistAsAlbumSwitch->setChecked(value.toBool()); }
    else if (key == "convert_thumbnail_to") { QSignalBlocker b(m_convertThumbnailsCombo); m_convertThumbnailsCombo->setCurrentText(value.toString()); }
}