#include "DownloadOptionsPage.h"
#include "core/ConfigManager.h"
#include "core/ProcessUtils.h"
#include "ui/ToggleSwitch.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStringList>

namespace {
struct CutEncoderOption {
    const char *id;
    const char *label;
};

const CutEncoderOption kCutEncoderOptions[] = {
    {"cpu", "Software encoder (default)"},
    {"nvenc_h264", "NVIDIA NVENC H.264"},
    {"qsv_h264", "Intel Quick Sync H.264"},
    {"amf_h264", "AMD AMF H.264"},
    {"videotoolbox_h264", "Apple VideoToolbox H.264"},
    {"custom", "Custom FFmpeg output args"},
};

QString requiredFfmpegEncoder(const QString &id)
{
    if (id == "nvenc_h264") return "h264_nvenc";
    if (id == "qsv_h264") return "h264_qsv";
    if (id == "amf_h264") return "h264_amf";
    if (id == "videotoolbox_h264") return "h264_videotoolbox";
    return QString();
}
}

DownloadOptionsPage::DownloadOptionsPage(ConfigManager *configManager, QWidget *parent)
    : QWidget(parent),
      m_configManager(configManager),
      m_ffmpegCutEncoderCombo(nullptr),
      m_ffmpegCutCustomArgsInput(nullptr),
      m_ffmpegEncoderProbe(new QProcess(this)),
      m_gpuProbe(new QProcess(this)),
      m_ffmpegEncoderProbeFinished(false),
      m_gpuProbeFinished(false) {
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget *scrollWidget = new QWidget(scrollArea);
    QVBoxLayout *contentLayout = new QVBoxLayout(scrollWidget);
    contentLayout->setContentsMargins(0, 0, 8, 0);
    contentLayout->setSpacing(12);

    m_externalDownloaderCombo = new QComboBox(this);
    m_externalDownloaderCombo->addItem("yt-dlp (default)", "ytdlp");
    m_externalDownloaderCombo->addItem("aria2c", "aria2c");
    m_externalDownloaderCombo->setToolTip("Choose the downloader to use for downloads.\n"
                                          "yt-dlp: Default downloader built into yt-dlp.\n"
                                          "aria2c: External downloader for faster multi-connection downloads.");
    
    m_sponsorBlockCheck = new ToggleSwitch(this);
    m_sponsorBlockCheck->setToolTip("Automatically remove or skip sponsored segments using the SponsorBlock API.");
    m_embedChaptersCheck = new ToggleSwitch(this);
    m_embedChaptersCheck->setToolTip("Embed chapter markers into the video file if available.");
    m_splitChaptersCheck = new ToggleSwitch(this);
    m_splitChaptersCheck->setToolTip("Split the video into separate files based on its chapters.");
    m_downloadSectionsCheck = new ToggleSwitch(this);
    m_downloadSectionsCheck->setToolTip("Prompt to download only a specific time range or chapter instead of the full video.");
    m_ffmpegCutEncoderCombo = new QComboBox(this);
    m_ffmpegCutEncoderCombo->setToolTip("Choose the FFmpeg video encoder yt-dlp uses when accurate SponsorBlock or section cuts require re-encoding. Hardware presets favor speed so users can leave SponsorBlock enabled for large videos.");
    m_ffmpegCutCustomArgsInput = new QLineEdit(this);
    m_ffmpegCutCustomArgsInput->setPlaceholderText("e.g., -c:v h264_nvenc -preset p1 -cq 24 -multipass disabled");
    m_ffmpegCutCustomArgsInput->setToolTip("Optional FFmpeg output arguments for yt-dlp's cut-normalization pass. Used only when the encoder mode is Custom.");
    populateFfmpegCutEncoderCombo();
    m_singleLineCommandPreviewCheck = new ToggleSwitch(this);
    m_singleLineCommandPreviewCheck->setToolTip("Display the yt-dlp command preview as a single scrolling line instead of wrapping.");
    m_restrictFilenamesCheck = new ToggleSwitch(this);
    m_restrictFilenamesCheck->setToolTip("Restrict filenames to ASCII characters, avoiding spaces and special characters.");

    m_prefixPlaylistIndicesCheck = new ToggleSwitch(this);
    m_prefixPlaylistIndicesCheck->setToolTip("Automatically add a numbered prefix (e.g., '01 - ') to files downloaded from a playlist to preserve their original order.");

    m_autoClearCompletedCheck = new ToggleSwitch(this);
    m_autoClearCompletedCheck->setToolTip("Automatically remove downloads from the Active list once they finish successfully.");
    m_autoPasteModeCombo = new QComboBox(this);
    m_autoPasteModeCombo->addItems({
        "Disabled",
        "Auto-paste on app focus (no enqueue)",
        "Auto-paste on new URL in clipboard (no enqueue)",
        "Auto-paste & enqueue on app focus",
        "Auto-paste & enqueue on new URL in clipboard"
    });
    m_autoPasteModeCombo->setToolTip("Controls when URLs are auto-pasted from clipboard.\n"
                                      "Enqueue modes will only add NEW URLs (duplicates are prevented).");

    m_geoProxyInput = new QLineEdit(this);
    m_geoProxyInput->setPlaceholderText("e.g., http://proxy.server:port");
    m_geoProxyInput->setToolTip("Use this proxy server for geo-verification if a video is restricted in your region.");

    auto addFormRow = [&](QFormLayout *formLayout, const QString& labelText, QWidget* field) {
        QLabel* label = new QLabel(labelText, this);
        label->setToolTip(field->toolTip());
        formLayout->addRow(label, field);
    };

    QGroupBox *engineGroup = new QGroupBox("Downloader", scrollWidget);
    engineGroup->setToolTip("Choose the download engine and network options.");
    QFormLayout *engineLayout = new QFormLayout(engineGroup);
    addFormRow(engineLayout, "Downloader engine:", m_externalDownloaderCombo);
    addFormRow(engineLayout, "Geo-verification proxy:", m_geoProxyInput);
    contentLayout->addWidget(engineGroup);

    QGroupBox *automationGroup = new QGroupBox("Queue & Clipboard", scrollWidget);
    automationGroup->setToolTip("Small workflow helpers for adding and clearing downloads.");
    QFormLayout *automationLayout = new QFormLayout(automationGroup);
    addFormRow(automationLayout, "Auto-paste URL behavior:", m_autoPasteModeCombo);
    addFormRow(automationLayout, "Auto-clear completed downloads", m_autoClearCompletedCheck);
    contentLayout->addWidget(automationGroup);

    QGroupBox *chaptersGroup = new QGroupBox("Chapters, Sections & SponsorBlock", scrollWidget);
    chaptersGroup->setToolTip("Control chapter markers, chapter splitting, selected section downloads, and SponsorBlock cuts.");
    QFormLayout *chaptersLayout = new QFormLayout(chaptersGroup);
    addFormRow(chaptersLayout, "Enable SponsorBlock", m_sponsorBlockCheck);
    addFormRow(chaptersLayout, "Embed video chapters", m_embedChaptersCheck);
    addFormRow(chaptersLayout, "Split chapters into separate files", m_splitChaptersCheck);
    addFormRow(chaptersLayout, "Ask for sections before download", m_downloadSectionsCheck);
    addFormRow(chaptersLayout, "FFmpeg cut encoder:", m_ffmpegCutEncoderCombo);
    addFormRow(chaptersLayout, "Custom cut encoder args:", m_ffmpegCutCustomArgsInput);
    contentLayout->addWidget(chaptersGroup);

    QGroupBox *filenameGroup = new QGroupBox("Display & Filenames", scrollWidget);
    filenameGroup->setToolTip("Control command preview display and filename compatibility.");
    QFormLayout *filenameLayout = new QFormLayout(filenameGroup);
    addFormRow(filenameLayout, "Single-line command preview", m_singleLineCommandPreviewCheck);
    addFormRow(filenameLayout, "Restrict filenames", m_restrictFilenamesCheck);
    addFormRow(filenameLayout, "Prefix playlist indices", m_prefixPlaylistIndicesCheck);
    contentLayout->addWidget(filenameGroup);

    contentLayout->addStretch();
    scrollArea->setWidget(scrollWidget);
    layout->addWidget(scrollArea);

    connect(m_externalDownloaderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DownloadOptionsPage::onExternalDownloaderChanged);
    connect(m_sponsorBlockCheck, &ToggleSwitch::toggled, this, &DownloadOptionsPage::onSponsorBlockToggled);
    connect(m_embedChaptersCheck, &ToggleSwitch::toggled, this, &DownloadOptionsPage::onEmbedChaptersToggled);
    connect(m_splitChaptersCheck, &ToggleSwitch::toggled, this, &DownloadOptionsPage::onSplitChaptersToggled);
    connect(m_downloadSectionsCheck, &ToggleSwitch::toggled, this, &DownloadOptionsPage::onDownloadSectionsToggled);
    connect(m_ffmpegCutEncoderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DownloadOptionsPage::onFfmpegCutEncoderChanged);
    connect(m_ffmpegCutCustomArgsInput, &QLineEdit::editingFinished, this, &DownloadOptionsPage::onFfmpegCutCustomArgsChanged);
    connect(m_autoPasteModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DownloadOptionsPage::onAutoPasteModeChanged);
    connect(m_singleLineCommandPreviewCheck, &ToggleSwitch::toggled, this, &DownloadOptionsPage::onSingleLineCommandPreviewToggled);
    connect(m_restrictFilenamesCheck, &ToggleSwitch::toggled, this, &DownloadOptionsPage::onRestrictFilenamesToggled);
    connect(m_prefixPlaylistIndicesCheck, &ToggleSwitch::toggled, this, [this](bool c) {
        m_configManager->set("DownloadOptions", "prefix_playlist_indices", c);
    });
    connect(m_autoClearCompletedCheck, &ToggleSwitch::toggled, this, &DownloadOptionsPage::onAutoClearCompletedToggled);
    connect(m_geoProxyInput, &QLineEdit::editingFinished, this, &DownloadOptionsPage::onGeoProxyChanged);
    connect(m_configManager, &ConfigManager::settingChanged, this, &DownloadOptionsPage::handleConfigSettingChanged);

    connect(m_ffmpegEncoderProbe, &QProcess::readyReadStandardOutput, this, [this]() {
        m_ffmpegEncoderProbeOutput += QString::fromUtf8(m_ffmpegEncoderProbe->readAllStandardOutput());
    });
    connect(m_ffmpegEncoderProbe, &QProcess::readyReadStandardError, this, [this]() {
        m_ffmpegEncoderProbeOutput += QString::fromUtf8(m_ffmpegEncoderProbe->readAllStandardError());
    });
    connect(m_ffmpegEncoderProbe, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this]() {
        m_ffmpegEncoderProbeFinished = true;
        maybeApplyHardwareEncoderProbe();
    });
    connect(m_ffmpegEncoderProbe, &QProcess::errorOccurred, this, [this]() {
        m_ffmpegEncoderProbeFinished = true;
        maybeApplyHardwareEncoderProbe();
    });

    connect(m_gpuProbe, &QProcess::readyReadStandardOutput, this, [this]() {
        m_gpuProbeOutput += QString::fromUtf8(m_gpuProbe->readAllStandardOutput());
    });
    connect(m_gpuProbe, &QProcess::readyReadStandardError, this, [this]() {
        m_gpuProbeOutput += QString::fromUtf8(m_gpuProbe->readAllStandardError());
    });
    connect(m_gpuProbe, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this]() {
        m_gpuProbeFinished = true;
        maybeApplyHardwareEncoderProbe();
    });
    connect(m_gpuProbe, &QProcess::errorOccurred, this, [this]() {
        m_gpuProbeFinished = true;
        maybeApplyHardwareEncoderProbe();
    });

    startHardwareEncoderProbe();
}

void DownloadOptionsPage::loadSettings() {
    QSignalBlocker b1(m_externalDownloaderCombo);
    QSignalBlocker b2(m_sponsorBlockCheck);
    QSignalBlocker b3(m_embedChaptersCheck);
    QSignalBlocker b4(m_autoPasteModeCombo);
    QSignalBlocker b5(m_singleLineCommandPreviewCheck);
    QSignalBlocker b6(m_restrictFilenamesCheck);
    QSignalBlocker b7(m_splitChaptersCheck);
    QSignalBlocker b8(m_geoProxyInput);
    QSignalBlocker b9(m_autoClearCompletedCheck);    
    QSignalBlocker b10(m_downloadSectionsCheck);
    QSignalBlocker b11(m_ffmpegCutEncoderCombo);
    QSignalBlocker b12(m_ffmpegCutCustomArgsInput);
    QSignalBlocker b13(m_prefixPlaylistIndicesCheck);

    ProcessUtils::FoundBinary aria2Binary = ProcessUtils::findBinary("aria2c", m_configManager);
    bool aria2Available = (aria2Binary.source != "Not Found" && aria2Binary.source != "Invalid Custom" && !aria2Binary.path.isEmpty());
    
    bool useAria2c = m_configManager->get("Metadata", "use_aria2c", false).toBool();
    if (!aria2Available) {
        m_externalDownloaderCombo->setVisible(false);
        m_externalDownloaderCombo->setCurrentIndex(0); // yt-dlp
        if (useAria2c) {
            m_configManager->set("Metadata", "use_aria2c", false); // Auto-correct stale config
        }
    } else {
        m_externalDownloaderCombo->setVisible(true);
        m_externalDownloaderCombo->setCurrentIndex(useAria2c ? 1 : 0);
    }
    m_sponsorBlockCheck->setChecked(m_configManager->get("General", "sponsorblock", false).toBool());
    m_embedChaptersCheck->setChecked(m_configManager->get("Metadata", "embed_chapters", true).toBool());
    m_splitChaptersCheck->setChecked(m_configManager->get("DownloadOptions", "split_chapters", false).toBool());
    m_downloadSectionsCheck->setChecked(m_configManager->get("DownloadOptions", "download_sections_enabled", false).toBool());
    const QString cutEncoder = m_configManager->get("DownloadOptions", "ffmpeg_cut_encoder", "cpu").toString();
    int cutEncoderIndex = m_ffmpegCutEncoderCombo->findData(cutEncoder);
    if (cutEncoderIndex < 0) cutEncoderIndex = 0;
    m_ffmpegCutEncoderCombo->setCurrentIndex(cutEncoderIndex);
    m_ffmpegCutCustomArgsInput->setText(m_configManager->get("DownloadOptions", "ffmpeg_cut_custom_args", "").toString());
    m_ffmpegCutCustomArgsInput->setEnabled(m_ffmpegCutEncoderCombo->currentData().toString() == "custom");
    m_autoClearCompletedCheck->setChecked(m_configManager->get("DownloadOptions", "auto_clear_completed", false).toBool());
    m_autoPasteModeCombo->setCurrentIndex(m_configManager->get("General", "auto_paste_mode", 0).toInt());
    m_singleLineCommandPreviewCheck->setChecked(m_configManager->get("General", "single_line_preview", false).toBool());
    m_restrictFilenamesCheck->setChecked(m_configManager->get("General", "restrict_filenames", false).toBool());
    m_prefixPlaylistIndicesCheck->setChecked(m_configManager->get("DownloadOptions", "prefix_playlist_indices", false).toBool());

    m_geoProxyInput->setText(m_configManager->get("DownloadOptions", "geo_verification_proxy", "").toString());
}

void DownloadOptionsPage::onExternalDownloaderChanged(int index) {
    bool useAria2c = (index == 1);
    m_configManager->set("Metadata", "use_aria2c", useAria2c);
}
void DownloadOptionsPage::onSponsorBlockToggled(bool c) { m_configManager->set("General", "sponsorblock", c); }
void DownloadOptionsPage::onEmbedChaptersToggled(bool c) { m_configManager->set("Metadata", "embed_chapters", c); }
void DownloadOptionsPage::onSplitChaptersToggled(bool c) { m_configManager->set("DownloadOptions", "split_chapters", c); }
void DownloadOptionsPage::onDownloadSectionsToggled(bool c) { m_configManager->set("DownloadOptions", "download_sections_enabled", c); }
void DownloadOptionsPage::onFfmpegCutEncoderChanged(int index) {
    const QString encoder = m_ffmpegCutEncoderCombo->itemData(index).toString();
    m_ffmpegCutCustomArgsInput->setEnabled(encoder == "custom");
    m_configManager->set("DownloadOptions", "ffmpeg_cut_encoder", encoder);
}
void DownloadOptionsPage::onFfmpegCutCustomArgsChanged() { m_configManager->set("DownloadOptions", "ffmpeg_cut_custom_args", m_ffmpegCutCustomArgsInput->text().trimmed()); }
void DownloadOptionsPage::onAutoPasteModeChanged(int index) { m_configManager->set("General", "auto_paste_mode", index); }
void DownloadOptionsPage::onSingleLineCommandPreviewToggled(bool c) { m_configManager->set("General", "single_line_preview", c); }
void DownloadOptionsPage::onRestrictFilenamesToggled(bool c) { m_configManager->set("General", "restrict_filenames", c); }
void DownloadOptionsPage::onAutoClearCompletedToggled(bool c) { m_configManager->set("DownloadOptions", "auto_clear_completed", c); }
void DownloadOptionsPage::onGeoProxyChanged() { m_configManager->set("DownloadOptions", "geo_verification_proxy", m_geoProxyInput->text()); }

void DownloadOptionsPage::populateFfmpegCutEncoderCombo(const QStringList &visibleEncoderIds) {
    const QString selectedEncoder = m_ffmpegCutEncoderCombo
        ? m_ffmpegCutEncoderCombo->currentData().toString()
        : m_configManager->get("DownloadOptions", "ffmpeg_cut_encoder", "cpu").toString();
    QSignalBlocker blocker(m_ffmpegCutEncoderCombo);
    m_ffmpegCutEncoderCombo->clear();

    for (const CutEncoderOption &option : kCutEncoderOptions) {
        const QString id = QString::fromLatin1(option.id);
        const bool alwaysVisible = (id == "cpu" || id == "custom");
        if (alwaysVisible || visibleEncoderIds.contains(id)) {
            m_ffmpegCutEncoderCombo->addItem(QString::fromLatin1(option.label), id);
        }
    }

    int index = m_ffmpegCutEncoderCombo->findData(selectedEncoder);
    if (index < 0) {
        index = m_ffmpegCutEncoderCombo->findData("cpu");
        if (selectedEncoder != "cpu" && !selectedEncoder.isEmpty()) {
            m_configManager->set("DownloadOptions", "ffmpeg_cut_encoder", "cpu");
        }
    }
    m_ffmpegCutEncoderCombo->setCurrentIndex(index < 0 ? 0 : index);
    if (m_ffmpegCutCustomArgsInput) {
        m_ffmpegCutCustomArgsInput->setEnabled(m_ffmpegCutEncoderCombo->currentData().toString() == "custom");
    }
}

void DownloadOptionsPage::startHardwareEncoderProbe() {
    m_ffmpegEncoderProbeOutput.clear();
    m_gpuProbeOutput.clear();
    m_ffmpegEncoderProbeFinished = false;
    m_gpuProbeFinished = false;

    const QString ffmpegPath = ProcessUtils::findBinary("ffmpeg", m_configManager).path;
    if (ffmpegPath.isEmpty()) {
        m_ffmpegEncoderProbeFinished = true;
    } else {
        m_ffmpegEncoderProbe->start(ffmpegPath, {"-hide_banner", "-encoders"});
    }

#if defined(Q_OS_WIN)
    m_gpuProbe->start("powershell", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", "Get-CimInstance Win32_VideoController | Select-Object -ExpandProperty Name"});
#elif defined(Q_OS_MACOS)
    m_gpuProbe->start("sh", {"-c", "system_profiler SPDisplaysDataType 2>/dev/null | grep 'Chipset Model'"});
#else
    m_gpuProbe->start("sh", {"-c", "lspci 2>/dev/null | grep -Ei 'vga|3d|display'"});
#endif
}

void DownloadOptionsPage::maybeApplyHardwareEncoderProbe() {
    if (!m_ffmpegEncoderProbeFinished || !m_gpuProbeFinished) {
        return;
    }

    const QString encoderOutput = m_ffmpegEncoderProbeOutput.toLower();
    const QString gpuOutput = m_gpuProbeOutput.toLower();
    QStringList visibleEncoders;

    const auto ffmpegHasEncoder = [&](const QString &id) {
        const QString encoderName = requiredFfmpegEncoder(id);
        return encoderName.isEmpty() || encoderOutput.contains(encoderName);
    };

    if (ffmpegHasEncoder("nvenc_h264") && gpuOutput.contains("nvidia")) {
        visibleEncoders << "nvenc_h264";
    }
    if (ffmpegHasEncoder("qsv_h264") && gpuOutput.contains("intel")) {
        visibleEncoders << "qsv_h264";
    }
    if (ffmpegHasEncoder("amf_h264") && (gpuOutput.contains("amd") || gpuOutput.contains("radeon") || gpuOutput.contains("advanced micro devices"))) {
        visibleEncoders << "amf_h264";
    }
#if defined(Q_OS_MACOS)
    if (ffmpegHasEncoder("videotoolbox_h264")) {
        visibleEncoders << "videotoolbox_h264";
    }
#endif

    populateFfmpegCutEncoderCombo(visibleEncoders);
}

void DownloadOptionsPage::handleConfigSettingChanged(const QString &section, const QString &key, const QVariant &value) {
    if (section == "General") {
        if (key == "sponsorblock") m_sponsorBlockCheck->setChecked(value.toBool());
        else if (key == "auto_paste_mode") m_autoPasteModeCombo->setCurrentIndex(value.toInt());
        else if (key == "single_line_preview") m_singleLineCommandPreviewCheck->setChecked(value.toBool());
        else if (key == "restrict_filenames") m_restrictFilenamesCheck->setChecked(value.toBool());
    } else if (section == "Metadata") {
        if (key == "use_aria2c") {
            bool useAria2c = value.toBool();
            m_externalDownloaderCombo->setCurrentIndex(useAria2c ? 1 : 0);
        }
        else if (key == "embed_chapters") m_embedChaptersCheck->setChecked(value.toBool());
    } else if (section == "DownloadOptions") {
        if (key == "split_chapters") m_splitChaptersCheck->setChecked(value.toBool());
        else if (key == "auto_clear_completed") m_autoClearCompletedCheck->setChecked(value.toBool());
        else if (key == "download_sections_enabled") m_downloadSectionsCheck->setChecked(value.toBool());
        else if (key == "ffmpeg_cut_encoder") {
            const QString encoder = value.toString();
            int index = m_ffmpegCutEncoderCombo->findData(encoder);
            if (index < 0) index = 0;
            m_ffmpegCutEncoderCombo->setCurrentIndex(index);
            m_ffmpegCutCustomArgsInput->setEnabled(m_ffmpegCutEncoderCombo->currentData().toString() == "custom");
        }
        else if (key == "ffmpeg_cut_custom_args") m_ffmpegCutCustomArgsInput->setText(value.toString());
        else if (key == "geo_verification_proxy") m_geoProxyInput->setText(value.toString());
        else if (key == "prefix_playlist_indices") {
            QSignalBlocker b(m_prefixPlaylistIndicesCheck);
            m_prefixPlaylistIndicesCheck->setChecked(value.toBool());
        }
    }
}
