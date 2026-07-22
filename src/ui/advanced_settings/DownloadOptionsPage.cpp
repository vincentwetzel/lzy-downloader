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
    if (id == QStringLiteral("nvenc_h264")) return QStringLiteral("h264_nvenc");
    if (id == QStringLiteral("qsv_h264")) return QStringLiteral("h264_qsv");
    if (id == QStringLiteral("amf_h264")) return QStringLiteral("h264_amf");
    if (id == QStringLiteral("videotoolbox_h264")) return QStringLiteral("h264_videotoolbox");
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
    m_externalDownloaderCombo->addItem(tr("yt-dlp (default)"), QStringLiteral("ytdlp"));
    m_externalDownloaderCombo->addItem(tr("aria2c"), QStringLiteral("aria2c"));
    m_externalDownloaderCombo->setToolTip(tr("Choose the transfer engine.\n"
                                          "yt-dlp: Reliable default downloader.\n"
                                          "aria2c: Optional external downloader that can be faster on some sites by using multiple connections."));
    
    m_sponsorBlockCheck = new ToggleSwitch(this);
    m_sponsorBlockCheck->setToolTip(tr("Ask yt-dlp to remove supported SponsorBlock segments, such as sponsor reads, when segment data is available."));
    m_embedChaptersCheck = new ToggleSwitch(this);
    m_embedChaptersCheck->setToolTip(tr("Store chapter markers inside the final video file when the site provides them."));
    m_splitChaptersCheck = new ToggleSwitch(this);
    m_splitChaptersCheck->setToolTip(tr("Create one output file per chapter instead of one full-length file when chapter data is available."));
    m_downloadSectionsCheck = new ToggleSwitch(this);
    m_downloadSectionsCheck->setToolTip(tr("Before each video download, ask whether to download only selected time ranges or chapters."));
    m_ffmpegCutEncoderCombo = new QComboBox(this);
    m_ffmpegCutEncoderCombo->setToolTip(tr("Choose the FFmpeg encoder used when accurate SponsorBlock or section cuts require re-encoding. Hardware choices appear only when detected locally."));
    m_ffmpegCutCustomArgsInput = new QLineEdit(this);
    m_ffmpegCutCustomArgsInput->setPlaceholderText(tr("e.g., -c:v h264_nvenc -preset p1 -cq 24 -multipass disabled"));
    m_ffmpegCutCustomArgsInput->setToolTip(tr("Advanced: FFmpeg output arguments used only when FFmpeg cut encoder is set to Custom. Leave blank unless you know the exact encoder flags you want."));
    populateFfmpegCutEncoderCombo();
    m_singleLineCommandPreviewCheck = new ToggleSwitch(this);
    m_singleLineCommandPreviewCheck->setToolTip(tr("Show the yt-dlp command preview as one horizontal line instead of wrapping across multiple lines."));
    m_restrictFilenamesCheck = new ToggleSwitch(this);
    m_restrictFilenamesCheck->setToolTip(tr("Ask yt-dlp to use simpler ASCII-only filenames with fewer special characters. Helpful for older tools and network drives."));

    m_prefixPlaylistIndicesCheck = new ToggleSwitch(this);
    m_prefixPlaylistIndicesCheck->setToolTip(tr("Add a playlist number prefix such as '01 - ' so downloaded playlist files stay in source order."));

    m_autoClearCompletedCheck = new ToggleSwitch(this);
    m_autoClearCompletedCheck->setToolTip(tr("Remove successful downloads from the Active Downloads list automatically. Download History still keeps completed entries."));
    m_autoPasteModeCombo = new QComboBox(this);
    m_autoPasteModeCombo->addItems({
        tr("Disabled"),
        tr("Auto-paste on app focus (no enqueue)"),
        tr("Auto-paste on new URL in clipboard (no enqueue)"),
        tr("Auto-paste & enqueue on app focus"),
        tr("Auto-paste & enqueue on new URL in clipboard")
    });
    m_autoPasteModeCombo->setToolTip(tr("Choose when clipboard URLs are copied into the Start tab.\n"
                                      "Enqueue modes also add new URLs to the queue automatically; duplicates are skipped."));

    m_geoProxyInput = new QLineEdit(this);
    m_geoProxyInput->setPlaceholderText(QStringLiteral("e.g., http://proxy.server:port"));
    m_geoProxyInput->setToolTip(tr("Optional proxy used only for yt-dlp geo-verification checks, for example http://host:port. Leave blank for normal downloads."));

    auto addFormRow = [&](QFormLayout *formLayout, const QString& labelText, QWidget* field) {
        QLabel* label = new QLabel(labelText, this);
        label->setToolTip(field->toolTip());
        formLayout->addRow(label, field);
    };

    QGroupBox *engineGroup = new QGroupBox(tr("Downloader"), scrollWidget);
    engineGroup->setToolTip(tr("Choose the transfer engine and optional network settings used by yt-dlp."));
    QFormLayout *engineLayout = new QFormLayout(engineGroup);
    addFormRow(engineLayout, tr("Downloader engine:"), m_externalDownloaderCombo);
    addFormRow(engineLayout, tr("Geo-verification proxy:"), m_geoProxyInput);
    contentLayout->addWidget(engineGroup);

    QGroupBox *automationGroup = new QGroupBox(tr("Queue & Clipboard"), scrollWidget);
    automationGroup->setToolTip(tr("Set clipboard automation and whether completed downloads leave the Active Downloads list automatically."));
    QFormLayout *automationLayout = new QFormLayout(automationGroup);
    addFormRow(automationLayout, tr("Auto-paste URL behavior:"), m_autoPasteModeCombo);
    addFormRow(automationLayout, tr("Auto-clear completed downloads"), m_autoClearCompletedCheck);
    contentLayout->addWidget(automationGroup);

    QGroupBox *chaptersGroup = new QGroupBox(tr("Chapters, Sections & SponsorBlock"), scrollWidget);
    chaptersGroup->setToolTip(tr("Control chapter markers, chapter-based splitting, selected section downloads, and SponsorBlock cutting."));
    QFormLayout *chaptersLayout = new QFormLayout(chaptersGroup);
    addFormRow(chaptersLayout, tr("Enable SponsorBlock"), m_sponsorBlockCheck);
    addFormRow(chaptersLayout, tr("Embed video chapters"), m_embedChaptersCheck);
    addFormRow(chaptersLayout, tr("Split chapters into separate files"), m_splitChaptersCheck);
    addFormRow(chaptersLayout, tr("Ask for sections before download"), m_downloadSectionsCheck);
    addFormRow(chaptersLayout, tr("FFmpeg cut encoder:"), m_ffmpegCutEncoderCombo);
    addFormRow(chaptersLayout, tr("Custom cut encoder args:"), m_ffmpegCutCustomArgsInput);
    contentLayout->addWidget(chaptersGroup);

    QGroupBox *filenameGroup = new QGroupBox(tr("Display & Filenames"), scrollWidget);
    filenameGroup->setToolTip(tr("Control command preview wrapping and filename compatibility for downloaded files."));
    QFormLayout *filenameLayout = new QFormLayout(filenameGroup);
    addFormRow(filenameLayout, tr("Single-line command preview"), m_singleLineCommandPreviewCheck);
    addFormRow(filenameLayout, tr("Restrict filenames"), m_restrictFilenamesCheck);
    addFormRow(filenameLayout, tr("Prefix playlist indices"), m_prefixPlaylistIndicesCheck);
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
        m_configManager->set(QStringLiteral("DownloadOptions"), QStringLiteral("prefix_playlist_indices"), c);
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

    ProcessUtils::FoundBinary aria2Binary = ProcessUtils::findBinary(QStringLiteral("aria2c"), m_configManager);
    bool aria2Available = (aria2Binary.source != QStringLiteral("Not Found") && aria2Binary.source != QStringLiteral("Invalid Custom") && !aria2Binary.path.isEmpty());
    
    bool useAria2c = m_configManager->get(QStringLiteral("Metadata"), QStringLiteral("use_aria2c"), false).toBool();
    if (!aria2Available) {
        m_externalDownloaderCombo->setVisible(false);
        m_externalDownloaderCombo->setCurrentIndex(0); // yt-dlp
        if (useAria2c) {
            m_configManager->set(QStringLiteral("Metadata"), QStringLiteral("use_aria2c"), false); // Auto-correct stale config
        }
    } else {
        m_externalDownloaderCombo->setVisible(true);
        m_externalDownloaderCombo->setCurrentIndex(useAria2c ? 1 : 0);
    }
    if (QFormLayout* form = qobject_cast<QFormLayout*>(m_externalDownloaderCombo->parentWidget()->layout())) {
        if (QWidget* label = form->labelForField(m_externalDownloaderCombo)) {
            label->setVisible(aria2Available);
        }
    }
    m_sponsorBlockCheck->setChecked(m_configManager->get(QStringLiteral("General"), QStringLiteral("sponsorblock"), false).toBool());
    m_embedChaptersCheck->setChecked(m_configManager->get(QStringLiteral("Metadata"), QStringLiteral("embed_chapters"), true).toBool());
    m_splitChaptersCheck->setChecked(m_configManager->get(QStringLiteral("DownloadOptions"), QStringLiteral("split_chapters"), false).toBool());
    m_downloadSectionsCheck->setChecked(m_configManager->get(QStringLiteral("DownloadOptions"), QStringLiteral("download_sections_enabled"), false).toBool());
    const QString cutEncoder = m_configManager->get(QStringLiteral("DownloadOptions"), QStringLiteral("ffmpeg_cut_encoder"), QStringLiteral("cpu")).toString();
    int cutEncoderIndex = m_ffmpegCutEncoderCombo->findData(cutEncoder);
    if (cutEncoderIndex < 0) cutEncoderIndex = 0;
    m_ffmpegCutEncoderCombo->setCurrentIndex(cutEncoderIndex);
    m_ffmpegCutCustomArgsInput->setText(m_configManager->get(QStringLiteral("DownloadOptions"), QStringLiteral("ffmpeg_cut_custom_args"), QStringLiteral("")).toString());
    m_ffmpegCutCustomArgsInput->setEnabled(m_ffmpegCutEncoderCombo->currentData().toString() == QStringLiteral("custom"));
    m_autoClearCompletedCheck->setChecked(m_configManager->get(QStringLiteral("DownloadOptions"), QStringLiteral("auto_clear_completed"), false).toBool());
    m_autoPasteModeCombo->setCurrentIndex(m_configManager->get(QStringLiteral("General"), QStringLiteral("auto_paste_mode"), 0).toInt());
    m_singleLineCommandPreviewCheck->setChecked(m_configManager->get(QStringLiteral("General"), QStringLiteral("single_line_preview"), false).toBool());
    m_restrictFilenamesCheck->setChecked(m_configManager->get(QStringLiteral("General"), QStringLiteral("restrict_filenames"), false).toBool());
    m_prefixPlaylistIndicesCheck->setChecked(m_configManager->get(QStringLiteral("DownloadOptions"), QStringLiteral("prefix_playlist_indices"), false).toBool());

    m_geoProxyInput->setText(m_configManager->get(QStringLiteral("DownloadOptions"), QStringLiteral("geo_verification_proxy"), QStringLiteral("")).toString());
}

void DownloadOptionsPage::onExternalDownloaderChanged(int index) {
    bool useAria2c = (m_externalDownloaderCombo->itemData(index).toString() == QStringLiteral("aria2c"));
    m_configManager->set(QStringLiteral("Metadata"), QStringLiteral("use_aria2c"), useAria2c);
}
void DownloadOptionsPage::onSponsorBlockToggled(bool c) { m_configManager->set(QStringLiteral("General"), QStringLiteral("sponsorblock"), c); }
void DownloadOptionsPage::onEmbedChaptersToggled(bool c) { m_configManager->set(QStringLiteral("Metadata"), QStringLiteral("embed_chapters"), c); }
void DownloadOptionsPage::onSplitChaptersToggled(bool c) { m_configManager->set(QStringLiteral("DownloadOptions"), QStringLiteral("split_chapters"), c); }
void DownloadOptionsPage::onDownloadSectionsToggled(bool c) { m_configManager->set(QStringLiteral("DownloadOptions"), QStringLiteral("download_sections_enabled"), c); }
void DownloadOptionsPage::onFfmpegCutEncoderChanged(int index) {
    const QString encoder = m_ffmpegCutEncoderCombo->itemData(index).toString();
    m_configManager->set(QStringLiteral("DownloadOptions"), QStringLiteral("ffmpeg_cut_encoder"), encoder);
}
void DownloadOptionsPage::onFfmpegCutCustomArgsChanged() { m_configManager->set(QStringLiteral("DownloadOptions"), QStringLiteral("ffmpeg_cut_custom_args"), m_ffmpegCutCustomArgsInput->text().trimmed()); }
void DownloadOptionsPage::onAutoPasteModeChanged(int index) { m_configManager->set(QStringLiteral("General"), QStringLiteral("auto_paste_mode"), index); }
void DownloadOptionsPage::onSingleLineCommandPreviewToggled(bool c) { m_configManager->set(QStringLiteral("General"), QStringLiteral("single_line_preview"), c); }
void DownloadOptionsPage::onRestrictFilenamesToggled(bool c) { m_configManager->set(QStringLiteral("General"), QStringLiteral("restrict_filenames"), c); }
void DownloadOptionsPage::onAutoClearCompletedToggled(bool c) { m_configManager->set(QStringLiteral("DownloadOptions"), QStringLiteral("auto_clear_completed"), c); }
void DownloadOptionsPage::onGeoProxyChanged() { m_configManager->set(QStringLiteral("DownloadOptions"), QStringLiteral("geo_verification_proxy"), m_geoProxyInput->text()); }

void DownloadOptionsPage::populateFfmpegCutEncoderCombo(const QStringList &visibleEncoderIds) {
    // Always read the intended target from config so async repopulation doesn't lose it
    const QString savedEncoder = m_configManager->get(QStringLiteral("DownloadOptions"), QStringLiteral("ffmpeg_cut_encoder"), QStringLiteral("cpu")).toString();
    QSignalBlocker blocker(m_ffmpegCutEncoderCombo);
    m_ffmpegCutEncoderCombo->clear();

    for (const CutEncoderOption &option : kCutEncoderOptions) {
        const QString id = QString::fromLatin1(option.id);
        const bool alwaysVisible = (id == QStringLiteral("cpu") || id == QStringLiteral("custom"));
        if (alwaysVisible || visibleEncoderIds.contains(id) || savedEncoder == id) {
            m_ffmpegCutEncoderCombo->addItem(tr(option.label), id);
        }
    }

    int index = m_ffmpegCutEncoderCombo->findData(savedEncoder);
    if (index < 0) index = m_ffmpegCutEncoderCombo->findData(QStringLiteral("cpu"));

    m_ffmpegCutEncoderCombo->setCurrentIndex(index < 0 ? 0 : index);
    if (m_ffmpegCutCustomArgsInput) {
        m_ffmpegCutCustomArgsInput->setEnabled(m_ffmpegCutEncoderCombo->currentData().toString() == QStringLiteral("custom"));
    }
}

void DownloadOptionsPage::startHardwareEncoderProbe() {
    m_ffmpegEncoderProbeOutput.clear();
    m_gpuProbeOutput.clear();
    m_ffmpegEncoderProbeFinished = false;
    m_gpuProbeFinished = false;

    const QString ffmpegPath = ProcessUtils::findBinary(QStringLiteral("ffmpeg"), m_configManager).path;
    if (ffmpegPath.isEmpty()) {
        m_ffmpegEncoderProbeFinished = true;
    } else {
        m_ffmpegEncoderProbe->start(ffmpegPath, {QStringLiteral("-hide_banner"), QStringLiteral("-encoders")});
    }

#if defined(Q_OS_WIN)
    m_gpuProbe->start(QStringLiteral("powershell"), {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"), QStringLiteral("-Command"), QStringLiteral("Get-CimInstance Win32_VideoController | Select-Object -ExpandProperty Name")});
#elif defined(Q_OS_MACOS)
    m_gpuProbe->start(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("system_profiler SPDisplaysDataType 2>/dev/null | grep 'Chipset Model'")});
#else
    m_gpuProbe->start(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("lspci 2>/dev/null | grep -Ei 'vga|3d|display'")});
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

    if (ffmpegHasEncoder(QStringLiteral("nvenc_h264")) && gpuOutput.contains(QStringLiteral("nvidia"))) {
        visibleEncoders << QStringLiteral("nvenc_h264");
    }
    if (ffmpegHasEncoder(QStringLiteral("qsv_h264")) && gpuOutput.contains(QStringLiteral("intel"))) {
        visibleEncoders << QStringLiteral("qsv_h264");
    }
    if (ffmpegHasEncoder(QStringLiteral("amf_h264")) && (gpuOutput.contains(QStringLiteral("amd")) || gpuOutput.contains(QStringLiteral("radeon")) || gpuOutput.contains(QStringLiteral("advanced micro devices")))) {
        visibleEncoders << QStringLiteral("amf_h264");
    }
#if defined(Q_OS_MACOS)
    if (ffmpegHasEncoder(QStringLiteral("videotoolbox_h264"))) {
        visibleEncoders << QStringLiteral("videotoolbox_h264");
    }
#endif

    populateFfmpegCutEncoderCombo(visibleEncoders);
}

void DownloadOptionsPage::handleConfigSettingChanged(const QString &section, const QString &key, const QVariant &value) {
    if (section == QStringLiteral("General")) {
        if (key == QStringLiteral("sponsorblock")) { QSignalBlocker b(m_sponsorBlockCheck); m_sponsorBlockCheck->setChecked(value.toBool()); }
        else if (key == QStringLiteral("auto_paste_mode")) { QSignalBlocker b(m_autoPasteModeCombo); m_autoPasteModeCombo->setCurrentIndex(value.toInt()); }
        else if (key == QStringLiteral("single_line_preview")) { QSignalBlocker b(m_singleLineCommandPreviewCheck); m_singleLineCommandPreviewCheck->setChecked(value.toBool()); }
        else if (key == QStringLiteral("restrict_filenames")) { QSignalBlocker b(m_restrictFilenamesCheck); m_restrictFilenamesCheck->setChecked(value.toBool()); }
    } else if (section == QStringLiteral("Metadata")) {
        if (key == QStringLiteral("use_aria2c")) {
            bool useAria2c = value.toBool();
            QSignalBlocker b(m_externalDownloaderCombo);
            m_externalDownloaderCombo->setCurrentIndex(useAria2c ? 1 : 0);
        }
        else if (key == QStringLiteral("embed_chapters")) { QSignalBlocker b(m_embedChaptersCheck); m_embedChaptersCheck->setChecked(value.toBool()); }
    } else if (section == QStringLiteral("DownloadOptions")) {
        if (key == QStringLiteral("split_chapters")) { QSignalBlocker b(m_splitChaptersCheck); m_splitChaptersCheck->setChecked(value.toBool()); }
        else if (key == QStringLiteral("auto_clear_completed")) { QSignalBlocker b(m_autoClearCompletedCheck); m_autoClearCompletedCheck->setChecked(value.toBool()); }
        else if (key == QStringLiteral("download_sections_enabled")) { QSignalBlocker b(m_downloadSectionsCheck); m_downloadSectionsCheck->setChecked(value.toBool()); }
        else if (key == QStringLiteral("ffmpeg_cut_encoder")) {
            const QString encoder = value.toString();
            int index = m_ffmpegCutEncoderCombo->findData(encoder);
            if (index < 0) index = 0;
            QSignalBlocker b(m_ffmpegCutEncoderCombo);
            m_ffmpegCutEncoderCombo->setCurrentIndex(index);
            m_ffmpegCutCustomArgsInput->setEnabled(m_ffmpegCutEncoderCombo->currentData().toString() == QStringLiteral("custom"));
        }
        else if (key == QStringLiteral("ffmpeg_cut_custom_args")) { QSignalBlocker b(m_ffmpegCutCustomArgsInput); m_ffmpegCutCustomArgsInput->setText(value.toString()); }
        else if (key == QStringLiteral("geo_verification_proxy")) { QSignalBlocker b(m_geoProxyInput); m_geoProxyInput->setText(value.toString()); }
        else if (key == QStringLiteral("prefix_playlist_indices")) {
            QSignalBlocker b(m_prefixPlaylistIndicesCheck);
            m_prefixPlaylistIndicesCheck->setChecked(value.toBool());
        }
    }
}
