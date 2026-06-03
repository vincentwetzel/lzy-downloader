#include "StartTabUiBuilder.h"
#include "core/ConfigManager.h"
#include "ToggleSwitch.h"
#include <QLabel>
#include <QMessageBox>
#include <QDesktopServices>
#include <QDir>
#include <QFormLayout>
#include <QClipboard>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleFactory> // For QStyleHints
#include <QPushButton>
#include <QTextEdit>
#include <QHBoxLayout>
#include <QVBoxLayout>

StartTabUiBuilder::StartTabUiBuilder(ConfigManager *configManager, QObject *parent)
    : QObject(parent), m_configManager(configManager),
      m_urlInput(nullptr), m_downloadButton(nullptr), m_downloadTypeCombo(nullptr),
      m_playlistLogicCombo(nullptr), m_maxConcurrentCombo(nullptr), m_rateLimitCombo(nullptr),
      m_overrideDuplicateCheck(nullptr), m_commandPreview(nullptr), m_openDownloadsFolderButton(nullptr)
{
}

void StartTabUiBuilder::build(QWidget *parentWidget, QVBoxLayout *mainLayout)
{
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    QHBoxLayout *topLayout = new QHBoxLayout();

    QLabel *urlLabel = new QLabel(tr("Video/Playlist URL(s):"), parentWidget);
    urlLabel->setToolTip(tr("Enter the URLs of the videos or playlists you want to download."));
    topLayout->addWidget(urlLabel);

    topLayout->addStretch();

    QPushButton *supportedSitesBtn = new QPushButton(tr("Supported Sites"), parentWidget);
    supportedSitesBtn->setObjectName(QStringLiteral("supportedSitesBtn"));
    supportedSitesBtn->setToolTip(tr("View a searchable list of all supported websites and their capabilities."));
    topLayout->addWidget(supportedSitesBtn);

    QPushButton *openTempFolderButton = new QPushButton(tr("Open Temporary Folder"), parentWidget);
    openTempFolderButton->setToolTip(tr("Click here to open the folder where active downloads are temporarily stored."));
    connect(openTempFolderButton, &QPushButton::clicked, this, [this, parentWidget]() {
        QString tempDir = m_configManager->get(QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory")).toString();
        if (tempDir.isEmpty() || !QDir(tempDir).exists()) {
            QMessageBox::warning(parentWidget, tr("Folder Not Found"),
                                 tr("The temporary downloads directory is not set or does not exist.\n"
                                 "Please configure it in the Advanced Settings tab."));
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(tempDir));
    });
    topLayout->addWidget(openTempFolderButton);

    m_openDownloadsFolderButton = new QPushButton(tr("Open Downloads Folder"), parentWidget);
    m_openDownloadsFolderButton->setToolTip(tr("Click here to open the folder where all your finished downloads are saved."));
    topLayout->addWidget(m_openDownloadsFolderButton);
    mainLayout->addLayout(topLayout);

    QLabel *cookieWarningLabel = new QLabel(tr("⚠️ Warning: No browser selected for cookies. Video/Audio downloads may fail. We strongly recommend you use firefox cookies."), parentWidget);
    cookieWarningLabel->setObjectName(QStringLiteral("cookieWarningLabel"));
    cookieWarningLabel->setStyleSheet(QStringLiteral("color: #E6A23C; font-weight: bold;"));
    cookieWarningLabel->setWordWrap(true);
    mainLayout->addWidget(cookieWarningLabel);

    QHBoxLayout *inputSectionLayout = new QHBoxLayout();

    m_urlInput = new QTextEdit(parentWidget);
    m_urlInput->setPlaceholderText(tr("Paste one or more media URLs (one per line)..."));
    m_urlInput->setToolTip(tr("Paste the web address (URL) of the video or audio you want to download here. You can paste multiple links, just put each one on a new line."));
    m_urlInput->setMinimumHeight(100);
    applyUrlInputStyleSheet(m_urlInput);
    inputSectionLayout->addWidget(m_urlInput, 70);

    QVBoxLayout *actionColumnLayout = new QVBoxLayout();
    actionColumnLayout->setSpacing(10);

    actionColumnLayout->addStretch();

    m_downloadButton = new QPushButton(tr("Download Video"), parentWidget);
    m_downloadButton->setMinimumHeight(100);
    actionColumnLayout->addWidget(m_downloadButton);

    actionColumnLayout->addStretch();

    QLabel *downloadTypeLabel = new QLabel(tr("Download Type:"), parentWidget);
    downloadTypeLabel->setToolTip(tr("Select the type of download."));
    actionColumnLayout->addWidget(downloadTypeLabel);

    m_downloadTypeCombo = new QComboBox(parentWidget);
    m_downloadTypeCombo->addItem(tr("Video"), QStringLiteral("video"));
    m_downloadTypeCombo->addItem(tr("Audio Only"), QStringLiteral("audio"));
    m_downloadTypeCombo->addItem(tr("Gallery"), QStringLiteral("gallery"));
    m_downloadTypeCombo->addItem(tr("View Video/Audio Formats"), QStringLiteral("formats"));
    m_downloadTypeCombo->setItemData(3, tr("Uses yt-dlp to list available video and audio formats. Not supported for galleries."), Qt::ToolTipRole);
    m_downloadTypeCombo->setToolTip(tr("Select the type of download."));
    actionColumnLayout->addWidget(m_downloadTypeCombo);


    actionColumnLayout->addSpacing(20);

    inputSectionLayout->addLayout(actionColumnLayout, 30);
    mainLayout->addLayout(inputSectionLayout);

    QHBoxLayout *settingsLayout = new QHBoxLayout();

    QLabel *playlistLabel = new QLabel(tr("Playlist Logic:"), parentWidget);
    settingsLayout->addWidget(playlistLabel);
    m_playlistLogicCombo = new QComboBox(parentWidget);
    m_playlistLogicCombo->addItem(tr("Ask"), QStringLiteral("Ask"));
    m_playlistLogicCombo->addItem(tr("Download All (no prompt)"), QStringLiteral("Download All (no prompt)"));
    m_playlistLogicCombo->addItem(tr("Download Single (ignore playlist)"), QStringLiteral("Download Single (ignore playlist)"));
    m_playlistLogicCombo->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    m_playlistLogicCombo->setMinimumContentsLength(10);
    m_playlistLogicCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    settingsLayout->addWidget(m_playlistLogicCombo);

    QLabel *concurrentLabel = new QLabel(tr("Max Concurrent:"), parentWidget);
    settingsLayout->addWidget(concurrentLabel);
    m_maxConcurrentCombo = new QComboBox(parentWidget);
    m_maxConcurrentCombo->addItem(QStringLiteral("1"), QStringLiteral("1"));
    m_maxConcurrentCombo->addItem(QStringLiteral("2"), QStringLiteral("2"));
    m_maxConcurrentCombo->addItem(QStringLiteral("3"), QStringLiteral("3"));
    m_maxConcurrentCombo->addItem(QStringLiteral("4"), QStringLiteral("4"));
    m_maxConcurrentCombo->addItem(QStringLiteral("5"), QStringLiteral("5"));
    m_maxConcurrentCombo->addItem(QStringLiteral("6"), QStringLiteral("6"));
    m_maxConcurrentCombo->addItem(QStringLiteral("7"), QStringLiteral("7"));
    m_maxConcurrentCombo->addItem(QStringLiteral("8"), QStringLiteral("8"));
    m_maxConcurrentCombo->addItem(tr("1 (short sleep)"), QStringLiteral("1 (short sleep)"));
    m_maxConcurrentCombo->addItem(tr("1 (long sleep)"), QStringLiteral("1 (long sleep)"));
    m_maxConcurrentCombo->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    settingsLayout->addWidget(m_maxConcurrentCombo);

    QLabel *rateLabel = new QLabel(tr("Rate Limit:"), parentWidget);
    settingsLayout->addWidget(rateLabel);
    m_rateLimitCombo = new QComboBox(parentWidget);
    m_rateLimitCombo->addItem(tr("Unlimited"), QStringLiteral("Unlimited"));
    m_rateLimitCombo->addItem(tr("500 KB/s"), QStringLiteral("500 KB/s"));
    m_rateLimitCombo->addItem(tr("1 MB/s"), QStringLiteral("1 MB/s"));
    m_rateLimitCombo->addItem(tr("2 MB/s"), QStringLiteral("2 MB/s"));
    m_rateLimitCombo->addItem(tr("5 MB/s"), QStringLiteral("5 MB/s"));
    m_rateLimitCombo->addItem(tr("10 MB/s"), QStringLiteral("10 MB/s"));
    m_rateLimitCombo->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    settingsLayout->addWidget(m_rateLimitCombo);

    QLabel *overrideLabel = new QLabel(tr("Override Archive:"), parentWidget);
    settingsLayout->addWidget(overrideLabel);
    m_overrideDuplicateCheck = new ToggleSwitch(parentWidget);
    settingsLayout->addWidget(m_overrideDuplicateCheck);

    mainLayout->addLayout(settingsLayout);

    QLabel *previewLabel = new QLabel(tr("Command Preview:"), parentWidget);
    mainLayout->addWidget(previewLabel);

    m_commandPreview = new QTextEdit(parentWidget);
    m_commandPreview->setReadOnly(true);
    m_commandPreview->setMaximumHeight(80);
    mainLayout->addWidget(m_commandPreview);
}

void StartTabUiBuilder::applyUrlInputStyleSheet(QTextEdit *urlInput)
{
    // Implement style sheet logic here, or leave empty if handled by global themes
}