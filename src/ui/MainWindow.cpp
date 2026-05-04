#include "core/version.h"
#include "MainWindow.h"
#include "StartTab.h"
#include "ActiveDownloadsTab.h"
#include "MainWindowUiBuilder.h" // Include the new UI builder
#include "AdvancedSettingsTab.h"
#include "SortingTab.h"

// Include full definitions for forward-declared classes
#include "core/ConfigManager.h"
#include "core/ArchiveManager.h"
#include "core/DownloadManager.h"
#include "core/AppUpdater.h"
#include "core/UrlValidator.h"
#include "core/UpdateStatus.h" // Include UpdateStatus enum
#include "core/StartupWorker.h"
#include "core/LocalApiServer.h"
#include "utils/ExtractorJsonParser.h"
#include "core/download_pipeline/YtDlpDownloadInfoExtractor.h"
#include "core/ProcessUtils.h"
#include "ui/RuntimeSelectionDialog.h"
#include "ui/FormatSelectionDialog.h"
#include "ui/DownloadSectionsDialog.h"
#include "ui/MissingBinariesDialog.h"
#include "ui/advanced_settings/BinariesPage.h"
#include "ToggleSwitch.h"
#include "utils/BinaryFinder.h"
#include "SupportedSitesDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QStatusBar>
#include <QLabel>
#include <QDebug>
#include <QMessageBox>
#include <QTimer>
#include <QFile>
#include <QCoreApplication>
#include <QIcon>
#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QDesktopServices>
#include <QRegularExpression>
#include <QPixmap>
#include <QImageReader>
#include <QFileDialog>
#include <QEvent>
#include <QStyleHints>
#include <QComboBox>
#include <QCheckBox>
#include <QSizePolicy>
#include <QPushButton>
#include <QStandardPaths>
#include <QDateTime>

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonDocument>

#ifdef Q_OS_WIN
#include <windows.h>
#include <cstdio>

static bool s_consoleAllocatedByUs = false;

static void ApplyConsoleState(bool show) {
    HWND consoleWindow = GetConsoleWindow();
    if (show) {
        if (consoleWindow == NULL) {
            if (AllocConsole()) {
                s_consoleAllocatedByUs = true;
                FILE* dummy;
                freopen_s(&dummy, "CONOUT$", "w", stdout);
                freopen_s(&dummy, "CONOUT$", "w", stderr);
                freopen_s(&dummy, "CONIN$", "r", stdin);
            }
        } else {
            ShowWindow(consoleWindow, SW_SHOW);
        }
    } else {
        if (consoleWindow != NULL && s_consoleAllocatedByUs) {
            ShowWindow(consoleWindow, SW_HIDE);
        }
    }
}
#endif

const QStringList REPO_URLS = {
    "https://api.github.com/repos/vincentwetzel/lzy-downloader",
    "https://api.github.com/repos/vincentwetzel/MediaDownloader"
};
const QString GITHUB_PROJECT_URL = "https://github.com/vincentwetzel/lzy-downloader";
const QString DEVELOPER_DISCORD_URL_PART1 = "https://discord.gg/";
const QString DEVELOPER_DISCORD_URL_PART2 = "NfWaqK";
const QString DEVELOPER_DISCORD_URL_PART3 = "gYRG";

namespace {
bool isHttpUrlArgument(const QString &arg)
{
    return !arg.startsWith("--") && (arg.startsWith("http://") || arg.startsWith("https://"));
}

QString directCliUrl()
{
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (isHttpUrlArgument(args[i])) {
            return args[i];
        }
    }
    return {};
}

bool hasNonInteractiveLaunchArgument()
{
    const QStringList args = QCoreApplication::arguments();
    return args.contains("--headless") || args.contains("--server") || !directCliUrl().isEmpty();
}

bool hasServerLaunchArgument()
{
    const QStringList args = QCoreApplication::arguments();
    return args.contains("--headless") || args.contains("--server");
}

bool isNonInteractiveRequest(const QVariantMap &options)
{
    return options.value("non_interactive", false).toBool();
}

void applyNonInteractiveDownloadDefaults(QVariantMap &options)
{
    options["non_interactive"] = true;
    options["override_archive"] = true;
    options["playlist_logic"] = "Download All (no prompt)";
    options["runtime_format_selected"] = true;
    options["download_sections_set"] = true;
}
}

static void sendDiscordWebhookUpdate(const QString& url, const QString& downloadType, 
                                     const QString& status, double progress, 
                                     const QString& speed, const QString& eta, QObject* parent = nullptr) 
{
    static QNetworkAccessManager* networkManager = new QNetworkAccessManager(parent);

    QJsonObject json;
    json["url"] = url;
    json["download_type"] = downloadType;
    json["status"] = status;
    json["progress"] = progress;
    json["speed"] = speed;
    json["eta"] = eta;

    QJsonDocument doc(json);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);

    QNetworkRequest request(QUrl("http://127.0.0.1:8766/webhook"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = networkManager->post(request, payload);
    QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

MainWindow::MainWindow(ExtractorJsonParser *extractorJsonParser, QWidget *parent)
    : QMainWindow(parent), m_configManager(nullptr), m_archiveManager(nullptr), m_downloadManager(nullptr),
      m_appUpdater(nullptr), m_urlValidator(nullptr), m_startupWorker(nullptr), m_startupThread(nullptr),
      m_extractorJsonParser(extractorJsonParser), m_runtimeExtractor(nullptr), m_uiBuilder(nullptr),
      m_localApiServer(nullptr),
      m_clipboard(nullptr), m_startTab(nullptr), m_activeDownloadsTab(nullptr),
      m_advancedSettingsTab(nullptr), m_trayIcon(nullptr), m_trayMenu(nullptr),
      m_silentUpdateCheck(false), m_nonInteractiveLaunch(hasNonInteractiveLaunchArgument()), m_lastAutoPasteTimestamp(0)
{
    // Intercept window creation BEFORE it can be shown by main.cpp
    if (QCoreApplication::arguments().contains("--headless") || QCoreApplication::arguments().contains("--server")) {
        setAttribute(Qt::WA_DontShowOnScreen, true);
    }

    // Initialize core components
    m_configManager = new ConfigManager("settings.ini", this);
    qInfo() << "Using settings file at:" << QDir(m_configManager->getConfigDir()).filePath("settings.ini");

    // Apply CLI overrides and ensure exit_after resets to false on normal launches
    if (QCoreApplication::arguments().contains("--exit-after")) {
        m_configManager->set("General", "exit_after", true);
    } else {
        m_configManager->set("General", "exit_after", false);
    }
    m_configManager->save();

    m_archiveManager = new ArchiveManager(m_configManager, this);
    m_downloadManager = new DownloadManager(m_configManager, this);
    m_appUpdater = new AppUpdater(REPO_URLS, QString(APP_VERSION_STRING), this);
    m_urlValidator = new UrlValidator(m_configManager, this);
    m_clipboard = QApplication::clipboard(); // Initialize QClipboard

    // --- Dynamic Binary Discovery ---
    QMap<QString, QString> foundBinaries = BinaryFinder::findAllBinaries();
    for (auto it = foundBinaries.constBegin(); it != foundBinaries.constEnd(); ++it) {
        QString configKey = it.key() + "_path";
        QString currentPath = m_configManager->get("Binaries", configKey).toString();
        // If current path is empty or invalid, update it
        if (currentPath.isEmpty() || !QFile::exists(currentPath)) {
            if (!it.value().isEmpty()) {
                m_configManager->set("Binaries", configKey, it.value());
            }
        }
    }
    m_configManager->save();

    // Create worker and thread but do not parent the worker to MainWindow
    m_startupWorker = new StartupWorker(m_configManager, m_extractorJsonParser, nullptr);
    m_startupThread = new QThread(this);

    m_runtimeExtractor = new YtDlpDownloadInfoExtractor(this);
    connect(m_runtimeExtractor, &YtDlpDownloadInfoExtractor::extractionSuccess, this,
            [this](const QString &, const QString &, const QList<DownloadTarget> &, const QString &, const QMap<QString, QString> &, const QVariantMap &metadata) {
                onRuntimeInfoReady(metadata);
            });
    connect(m_runtimeExtractor, &YtDlpDownloadInfoExtractor::extractionFailed, this, &MainWindow::onRuntimeInfoError);

    // Apply theme before UI setup
    m_uiBuilder = new MainWindowUiBuilder(m_configManager, this); // Initialize UI builder
    applyTheme(m_configManager->get("General", "theme", "System").toString());

    // Local API Server setup
    m_localApiServer = new LocalApiServer(m_configManager, this);
    connect(m_localApiServer, &LocalApiServer::enqueueRequested, this, &MainWindow::onLocalApiEnqueueRequested);
    const bool serverMode = hasServerLaunchArgument();
    if (serverMode || m_configManager->get("General", "enable_local_api", false).toBool()) {
        qInfo() << "[LocalApi] Attempting to start Local API Server on startup..."
                << "serverMode:" << serverMode;
        m_localApiServer->start();
    }
    connect(m_configManager, &ConfigManager::settingChanged, this, [this, serverMode](const QString &section, const QString &key, const QVariant &value) {
        if (section == "General" && key == "enable_local_api") {
            if (value.toBool()) {
                qInfo() << "[LocalApi] Local API Server enabled by user setting. Starting server...";
                m_localApiServer->start();
            } else if (serverMode) {
                qInfo() << "[LocalApi] Ignoring disabled GUI API preference because server mode explicitly requested the API.";
            } else {
                qInfo() << "[LocalApi] Local API Server disabled by user setting. Stopping server...";
                m_localApiServer->stop();
            }
        }
    });

#ifdef Q_OS_WIN
    bool isDebug = false;
#ifdef QT_DEBUG
    isDebug = true;
#elif !defined(NDEBUG)
    isDebug = true;
#endif
    bool showConsole = m_configManager->get("General", "show_debug_console", isDebug).toBool();
    ApplyConsoleState(showConsole);
    connect(m_configManager, &ConfigManager::settingChanged, this, [](const QString &section, const QString &key, const QVariant &value) {
        if (section == "General" && key == "show_debug_console") {
            ApplyConsoleState(value.toBool());
        }
    });
#endif

    // Dynamically reschedule queue if the user increases Max Concurrent
    connect(m_configManager, &ConfigManager::settingChanged, this, [this](const QString &section, const QString &key, const QVariant &/*value*/) {
        if (section == "General" && key == "max_threads") {
            if (m_downloadManager) {
                QMetaObject::invokeMethod(m_downloadManager, "startNextDownload", Qt::QueuedConnection);
            }
        }
    });

    setupUI();
    setupTrayIcon();

    connect(m_appUpdater, &AppUpdater::updateAvailable, this,
            [this](const QString &latestVersion, const QString &releaseNotes, const QUrl &downloadUrl) {
                if (m_nonInteractiveLaunch) {
                    qInfo() << "Skipping update prompt during non-interactive launch. Available version:" << latestVersion;
                    m_silentUpdateCheck = false;
                    return;
                }

                m_silentUpdateCheck = false;

                QMessageBox msgBox(this);
                msgBox.setIcon(QMessageBox::Information);
                msgBox.setWindowTitle("Update Available");
                msgBox.setText(QString("LzyDownloader %1 is available. You are currently running %2.")
                                   .arg(latestVersion, QString(APP_VERSION_STRING)));

                QString informativeText = "Would you like to download and install the update now?";
                if (!releaseNotes.trimmed().isEmpty()) {
                    QString trimmedNotes = releaseNotes.trimmed();
                    if (trimmedNotes.size() > 1200) {
                        trimmedNotes = trimmedNotes.left(1200).trimmed() + "\n\n[Release notes truncated]";
                    }
                    informativeText += "\n\nRelease notes:\n" + trimmedNotes;
                }
                msgBox.setInformativeText(informativeText);

                QPushButton *updateNowButton = msgBox.addButton("Update Now", QMessageBox::AcceptRole);
                QPushButton *viewReleaseButton = msgBox.addButton("View Release", QMessageBox::ActionRole);
                msgBox.addButton(QMessageBox::Cancel);
                msgBox.exec();

                if (msgBox.clickedButton() == updateNowButton) {
                    statusBar()->showMessage("Downloading update...");
                    m_appUpdater->downloadAndInstall(downloadUrl);
                } else if (msgBox.clickedButton() == viewReleaseButton) {
                    QDesktopServices::openUrl(QUrl(GITHUB_PROJECT_URL + "/releases/latest"));
                }
            });

    connect(m_appUpdater, &AppUpdater::noUpdateAvailable, this, [this]() {
        m_silentUpdateCheck = false;
        qInfo() << "No app update available. Current version:" << APP_VERSION_STRING;
    });

    connect(m_appUpdater, &AppUpdater::updateCheckFailed, this, [this](const QString &error) {
        const bool wasSilent = m_silentUpdateCheck;
        m_silentUpdateCheck = false;
        qWarning() << "App update check failed:" << error;
        if (!wasSilent) {
            QMessageBox::warning(this, "Update Check Failed", error);
        }
    });

    connect(m_appUpdater, &AppUpdater::downloadProgress, this, [this](qint64 bytesReceived, qint64 bytesTotal) {
        if (bytesTotal > 0) {
            const double percent = (static_cast<double>(bytesReceived) / static_cast<double>(bytesTotal)) * 100.0;
            statusBar()->showMessage(QString("Downloading update... %1%").arg(percent, 0, 'f', 1));
        } else {
            statusBar()->showMessage("Downloading update...");
        }
    });

    connect(m_appUpdater, &AppUpdater::downloadFinished, this, [this]() {
        statusBar()->showMessage("Update downloaded. Launching installer...", 5000);
    });

    // Defer the setup dialogs until after the main window is shown
    QTimer::singleShot(0, this, [this]() {
        bool isHeadless = QCoreApplication::arguments().contains("--headless") || QCoreApplication::arguments().contains("--server");
        bool isNonInteractive = m_nonInteractiveLaunch;

        if (isHeadless) {
            if (m_trayIcon) {
                m_trayIcon->showMessage("LzyDownloader", "Running in headless server mode.", QSystemTrayIcon::Information, 3000);
            }
        }

        // Ensure completed downloads directory is set
        QString completedDownloadsDir = m_configManager->get("Paths", "completed_downloads_directory").toString();
        if (completedDownloadsDir.isEmpty()) {
            if (isNonInteractive) {
                QString baseDownloadDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
                if (baseDownloadDir.isEmpty()) {
                    baseDownloadDir = QDir::homePath();
                }
                completedDownloadsDir = QDir(baseDownloadDir).filePath("LzyDownloader");
                if (m_configManager->set("Paths", "completed_downloads_directory", completedDownloadsDir)) {
                    m_configManager->save();
                }
                qInfo() << "Set default completed downloads directory for non-interactive launch:" << completedDownloadsDir;
            } else {
            QMessageBox::information(this, "Setup Required",
                                     "Please select a directory for completed downloads. This will also set up a temporary downloads directory.");
            QString selectedDir = QFileDialog::getExistingDirectory(this, "Select Completed Downloads Directory",
                                                                    QDir::homePath(), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
            if (!selectedDir.isEmpty()) {
                if (m_configManager->set("Paths", "completed_downloads_directory", selectedDir)) {
                    m_configManager->save();
                }
                completedDownloadsDir = selectedDir;
                QMessageBox::information(this, "Directory Set",
                                         QString("Completed downloads directory set to:\n%1\n\nTemporary downloads directory set to:\n%2")
                                             .arg(completedDownloadsDir)
                                             .arg(QDir(completedDownloadsDir).filePath("temp_downloads")));
            } else {
                QMessageBox::warning(this, "Directory Not Set",
                                     "No completed downloads directory was selected. Please set it in Advanced Settings to enable downloads.");
            }
            }
        }

        // Ensure temporary downloads directory is set if completed is available but temp is not
        QString temporaryDownloadsDir = m_configManager->get("Paths", "temporary_downloads_directory").toString();
        if (!completedDownloadsDir.isEmpty() && temporaryDownloadsDir.isEmpty()) {
            QString defaultTempDir = QDir(completedDownloadsDir).filePath("temp_downloads");
            if (m_configManager->set("Paths", "temporary_downloads_directory", defaultTempDir)) {
                m_configManager->save();
                qInfo() << "Automatically set missing temporary_downloads_directory to" << defaultTempDir;
            }
        }

        m_silentUpdateCheck = true;
        QTimer::singleShot(0, this, [this]() {
            if (m_appUpdater) {
                m_appUpdater->checkForUpdates();
            }
        });
    });

    // Connect signals
    connect(m_downloadManager, &DownloadManager::downloadAddedToQueue,
            m_activeDownloadsTab, &ActiveDownloadsTab::addDownloadItem);
    connect(m_downloadManager, &DownloadManager::downloadProgress,
            m_activeDownloadsTab, &ActiveDownloadsTab::updateDownloadProgress);
    connect(m_downloadManager, &DownloadManager::downloadFinished,
            m_activeDownloadsTab, &ActiveDownloadsTab::onDownloadFinished);
    connect(m_downloadManager, &DownloadManager::downloadCancelled,
            m_activeDownloadsTab, &ActiveDownloadsTab::onDownloadCancelled);
    connect(m_downloadManager, &DownloadManager::downloadPaused,
            m_activeDownloadsTab, &ActiveDownloadsTab::onDownloadPaused);
    connect(m_downloadManager, &DownloadManager::downloadResumed,
            m_activeDownloadsTab, &ActiveDownloadsTab::onDownloadResumed);
    connect(m_downloadManager, &DownloadManager::downloadFinalPathReady,
            m_activeDownloadsTab, &ActiveDownloadsTab::onDownloadFinalPathReady);
    connect(m_downloadManager, &DownloadManager::downloadRemovedFromQueue,
            m_activeDownloadsTab, &ActiveDownloadsTab::removeDownloadItem);
    connect(m_downloadManager, &DownloadManager::playlistExpansionStarted,
            m_activeDownloadsTab, &ActiveDownloadsTab::addExpandingPlaylist);
    connect(m_downloadManager, &DownloadManager::playlistExpansionFinished,
            m_activeDownloadsTab, &ActiveDownloadsTab::removeExpandingPlaylist); // Corrected: removeExpandingPlaylist
    connect(m_downloadManager, &DownloadManager::queueFinished, this, &MainWindow::onQueueFinished);
    connect(m_downloadManager, &DownloadManager::totalSpeedUpdated, this, &MainWindow::updateTotalSpeed);
    connect(m_downloadManager, &DownloadManager::videoQualityWarning, this, &MainWindow::onVideoQualityWarning);
    connect(m_downloadManager, &DownloadManager::downloadStatsUpdated, this, &MainWindow::onDownloadStatsUpdated);

    connect(m_downloadManager, &DownloadManager::downloadAddedToQueue, m_localApiServer, &LocalApiServer::onDownloadAdded);
    connect(m_downloadManager, &DownloadManager::downloadProgress, m_localApiServer, &LocalApiServer::onDownloadProgress);
    connect(m_downloadManager, &DownloadManager::downloadFinished, m_localApiServer, &LocalApiServer::onDownloadFinished);
    connect(m_downloadManager, &DownloadManager::downloadRemovedFromQueue, m_localApiServer, &LocalApiServer::onDownloadRemoved);

    // Hook up the Discord webhook to the existing job update pipeline
    connect(m_downloadManager, &DownloadManager::downloadProgress, this, [](const QString &id, const QVariantMap &data) {
        Q_UNUSED(id);
        // Fallbacks are provided if 'url' or 'download_type' are omitted from progress payload
        QString url = data.value("url").toString();
        QString type = data.value("download_type", "video").toString(); 
        QString status = data.value("status").toString();
        double progress = data.value("progress").toDouble();
        QString speed = data.value("speed").toString();
        QString eta = data.value("eta").toString();

        sendDiscordWebhookUpdate(url, type, status, progress, speed, eta);
    });

    connect(m_downloadManager, &DownloadManager::downloadFinished, this, [](const QString &id) {
        Q_UNUSED(id);
        sendDiscordWebhookUpdate("", "video", "Completed", 100.0, "", "");
    });

    connect(m_downloadManager, &DownloadManager::downloadCancelled, this, [](const QString &id) {
        Q_UNUSED(id);
        sendDiscordWebhookUpdate("", "video", "Cancelled", 0.0, "", "");
    });

    // Connect duplicate detection signal to StartTab
    connect(m_downloadManager, &DownloadManager::duplicateDownloadDetected, m_startTab, &StartTab::onDuplicateDownloadDetected);

    // Connect yt-dlp error popup signal
    connect(m_downloadManager, &DownloadManager::ytDlpErrorPopupRequested, this, &MainWindow::onYtDlpErrorPopup);

    // Connect download sections signal
    connect(m_downloadManager, &DownloadManager::downloadSectionsRequested, this, &MainWindow::onDownloadSectionsRequested);
    connect(m_downloadManager, &DownloadManager::playlistActionRequested, this,
            [this](const QString &url, int itemCount, const QVariantMap &options, const QList<QVariantMap> &expandedItems) {
                if (isNonInteractiveRequest(options)) {
                    qInfo() << "Non-interactive playlist request detected; queueing all playlist items for" << url << "count:" << itemCount;
                    m_downloadManager->processPlaylistSelection(url, "Download All", options, expandedItems);
                    m_uiBuilder->tabWidget()->setCurrentWidget(m_activeDownloadsTab);
                    return;
                }

                QMessageBox msgBox(this);
                msgBox.setIcon(QMessageBox::Question);
                msgBox.setWindowTitle("Playlist Detected");
                msgBox.setText(QString("This URL contains a playlist with %1 item(s).").arg(itemCount));
                msgBox.setInformativeText("Do you want to queue every item or just the first one?");

                QPushButton *downloadAllButton = msgBox.addButton("Download All", QMessageBox::AcceptRole);
                QPushButton *downloadSingleButton = msgBox.addButton("Download Single Item", QMessageBox::ActionRole);
                QPushButton *cancelButton = msgBox.addButton(QMessageBox::Cancel);

                msgBox.exec();

                QString action;
                if (msgBox.clickedButton() == downloadAllButton) {
                    action = "Download All";
                } else if (msgBox.clickedButton() == downloadSingleButton) {
                    action = "Download Single Item";
                } else if (msgBox.clickedButton() == cancelButton) {
                    action = "Cancel";
                }

                m_downloadManager->processPlaylistSelection(url, action, options, expandedItems);
                m_uiBuilder->tabWidget()->setCurrentWidget(m_activeDownloadsTab);
            });

    // Handle requests for runtime format selection
    connect(m_downloadManager, &DownloadManager::formatSelectionRequested, this,
        [this](const QString &url, const QVariantMap &options, const QVariantMap &infoDict) {
            if (isNonInteractiveRequest(options)) {
                QVariantMap newOptions = options;
                newOptions["runtime_format_selected"] = true;
                qInfo() << "Skipping runtime format dialog for non-interactive request:" << url;
                m_downloadManager->enqueueDownload(url, newOptions);
                return;
            }

            FormatSelectionDialog dialog(infoDict, options, this);
            if (dialog.exec() == QDialog::Accepted) {
                QStringList selectedFormats = dialog.getSelectedFormatIds();
                if (!selectedFormats.isEmpty()) {
                    // The dialog allows selecting multiple formats, and the user expects
                    // each to be enqueued as a separate download.
                    for (const QString &formatId : selectedFormats) {
                        QVariantMap newOptions = options;
                        newOptions["runtime_format_selected"] = true;
                        newOptions["format"] = formatId;
                        m_downloadManager->enqueueDownload(url, newOptions);
                    }
                    m_uiBuilder->tabWidget()->setCurrentWidget(m_activeDownloadsTab);
                }
            }
        });

    connect(m_activeDownloadsTab, &ActiveDownloadsTab::cancelDownloadRequested,
            m_downloadManager, &DownloadManager::cancelDownload);
    connect(m_activeDownloadsTab, &ActiveDownloadsTab::retryDownloadRequested,
            m_downloadManager, &DownloadManager::retryDownload);
    connect(m_activeDownloadsTab, &ActiveDownloadsTab::resumeDownloadRequested,
            m_downloadManager, &DownloadManager::resumeDownload);
    connect(m_activeDownloadsTab, &ActiveDownloadsTab::pauseDownloadRequested,
            m_downloadManager, &DownloadManager::pauseDownload);
    connect(m_activeDownloadsTab, &ActiveDownloadsTab::unpauseDownloadRequested,
            m_downloadManager, &DownloadManager::unpauseDownload);
    connect(m_activeDownloadsTab, &ActiveDownloadsTab::moveDownloadUpRequested,
            m_downloadManager, &DownloadManager::moveDownloadUp);
    connect(m_activeDownloadsTab, &ActiveDownloadsTab::moveDownloadDownRequested,
            m_downloadManager, &DownloadManager::moveDownloadDown);
    // FIXME: Re-enable this connection once the corresponding signal and slot are declared in their headers.
    // connect(m_activeDownloadsTab, &ActiveDownloadsTab::clearInactiveRequested,
    //         m_downloadManager, &DownloadManager::clearInactiveDownloads);
    // FIXME: Re-enable this connection once the corresponding signal and slot are declared in their headers.
    // See comments in ActiveDownloadsTab.cpp and DownloadManager.cpp for details.
    // connect(m_activeDownloadsTab, &ActiveDownloadsTab::itemCleared,
    //         m_downloadManager, &DownloadManager::onItemCleared);

    connect(m_urlValidator, &UrlValidator::validationFinished, this, &MainWindow::onValidationFinished);

    // Setup startup worker threading
    m_startupWorker->moveToThread(m_startupThread);
    connect(m_startupThread, &QThread::started, m_startupWorker, &StartupWorker::start);
    connect(m_startupWorker, &StartupWorker::finished, m_startupThread, &QThread::quit);
    // DO NOT connect deleteLater here. It will be handled manually in the destructor.
    connect(m_startupWorker, &StartupWorker::binariesChecked, this, [this](const QStringList &missingBinaries){
        if (!missingBinaries.isEmpty() && !m_nonInteractiveLaunch) {
            showMissingBinariesDialog(missingBinaries);
        }
    });
    connect(m_startupWorker, &StartupWorker::ytDlpVersionFetched, this, &MainWindow::setYtDlpVersion);
    connect(m_startupWorker, &StartupWorker::galleryDlVersionFetched, m_advancedSettingsTab, &AdvancedSettingsTab::setGalleryDlVersion);

    // Connect clipboard signal
    connect(m_clipboard, &QClipboard::changed, this, &MainWindow::onClipboardChanged);

    startStartupChecks();

    // Process direct CLI URL downloads (e.g., for Discord bots)
    QString cliUrl = directCliUrl();
    if (!cliUrl.isEmpty()) {
        QTimer::singleShot(500, this, [this, cliUrl]() {
            QVariantMap options;
            if (QCoreApplication::arguments().contains("--audio")) {
                options["type"] = "audio";
            } else {
                options["type"] = "video"; // Default to video
            }
            applyNonInteractiveDownloadDefaults(options);
            onDownloadRequested(cliUrl, options);
        });
    }
}

MainWindow::~MainWindow() {
    if (m_startupWorker) {
        QMetaObject::invokeMethod(m_startupWorker, "deleteLater", Qt::QueuedConnection);
    }
    if (m_startupThread && m_startupThread->isRunning()) {
        m_startupThread->quit();
        m_startupThread->wait();
    }
}

void MainWindow::setupUI() {
    setWindowTitle("LzyDownloader v" + QString(APP_VERSION_STRING));

    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    m_startTab = new StartTab(m_configManager, m_extractorJsonParser, this);
    m_activeDownloadsTab = new ActiveDownloadsTab(m_configManager, this);
    m_advancedSettingsTab = new AdvancedSettingsTab(m_configManager, this);
    SortingTab *sortingTab = new SortingTab(m_configManager, this);

    m_uiBuilder->build(this, mainLayout, m_startTab, m_activeDownloadsTab, m_advancedSettingsTab, sortingTab);

    // Connect signals from the builder's widgets
    connect(m_uiBuilder->exitAfterSwitch(), &ToggleSwitch::toggled, this, [this](bool checked){
        m_configManager->set("General", "exit_after", checked);
        m_configManager->save();
    });

    connect(m_startTab, &StartTab::downloadRequested, this, &MainWindow::onDownloadRequested);
    connect(m_startTab, &StartTab::navigateToExternalBinaries, this, [this]() {
        m_uiBuilder->tabWidget()->setCurrentWidget(m_advancedSettingsTab);
        m_advancedSettingsTab->navigateToCategory("External Binaries");
    });
    connect(m_startTab, &StartTab::missingBinariesDetected, this, [this](const QStringList &missingBinaries) {
        showMissingBinariesDialog(missingBinaries);
    });
    connect(m_advancedSettingsTab, &AdvancedSettingsTab::themeChanged, this, &MainWindow::applyTheme);

    connect(m_uiBuilder->tabWidget(), &QTabWidget::currentChanged, this, [this](int index) {
        if (m_uiBuilder->tabWidget()->widget(index) == m_startTab) {
            m_startTab->updateDynamicUI();
        }
    });
}

void MainWindow::setupTrayIcon() {
    m_trayIcon = new QSystemTrayIcon(QIcon(":/app-icon"), this);
    m_trayIcon->setToolTip("LzyDownloader");

    m_trayMenu = new QMenu(this);
    QAction *showAction = m_trayMenu->addAction("Show");
    connect(showAction, &QAction::triggered, this, &QWidget::showNormal);
    QAction *quitAction = m_trayMenu->addAction("Quit");
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayIcon->show();

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayIconActivated);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (!m_configManager) {
        event->accept();
        return;
    }

    int activeCount = 0;
    int queuedCount = 0;

    QString activeText = m_uiBuilder->activeDownloadsLabel()->text();
    if (activeText.startsWith("Active: ")) {
        activeCount = activeText.mid(8).toInt();
    }

    QString queuedText = m_uiBuilder->queuedDownloadsLabel()->text();
    if (queuedText.startsWith("Queued: ")) {
        queuedCount = queuedText.mid(8).toInt();
    }

    if (activeCount > 0 || queuedCount > 0) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::warning(this, "Downloads in Progress",
                                     "There are downloads currently running or queued.\n\nAre you sure you want to exit?\nYour downloads will be safely saved and will resume the next time you start the application.",
                                     QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::No) {
            event->ignore();
            return;
        }
    }

    qInfo() << "Main window close requested; shutting down active background tasks before exit.";
    if (m_downloadManager) {
        m_downloadManager->shutdown();
    }

    // Catch any remaining detached UI-spawned processes (e.g., UrlValidator, Extractors)
    const QList<QProcess*> remainingProcesses = findChildren<QProcess*>();
    for (QProcess *process : remainingProcesses) {
        if (process && process->state() == QProcess::Running) {
            process->disconnect(); // Prevent re-entrant read operations on the dying process buffer
            ProcessUtils::terminateProcessTree(process);
        }
    }

    if (m_trayIcon && m_trayIcon->isVisible()) {
        m_trayIcon->hide();
    }
    event->accept();
    QApplication::quit();
}

bool MainWindow::event(QEvent *event)
{
    bool handled = QMainWindow::event(event);
    if (!m_configManager) {
        return handled;
    }

    int autoPasteMode = m_configManager->get("General", "auto_paste_mode", 0).toInt();

    if (event->type() == QEvent::WindowActivate || event->type() == QEvent::Enter) {
        if (autoPasteMode == 1) { // Auto-paste on app focus
            handleClipboardAutoPaste(false);
        } else if (autoPasteMode == 3) { // Auto-paste on app focus & enqueue
            handleClipboardAutoPaste(true);
        }
    }
    return handled;
}

void MainWindow::onClipboardChanged()
{
    if (!m_configManager) {
        return;
    }

    int autoPasteMode = m_configManager->get("General", "auto_paste_mode", 0).toInt();

    if (autoPasteMode == 2) { // Auto-paste on new URL in clipboard
        handleClipboardAutoPaste(false);
    } else if (autoPasteMode == 4) { // Auto-paste on new URL & enqueue
        handleClipboardAutoPaste(true);
    }
}

void MainWindow::handleClipboardAutoPaste(bool forceEnqueue)
{
    if (!m_startTab || !m_uiBuilder || !m_uiBuilder->tabWidget() || !m_startTab->isEnabled()) {
        return;
    }

    // Enforce a brief cooldown period (500ms) to debounce rapid clipboard signals from the OS,
    // rather than a 5-second lock that prevents users from quickly copying multiple URLs.
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastAutoPasteTimestamp < 500) {
        return;
    }

    // Get the URL from clipboard to check before pasting
    const QString clipboardText = m_clipboard->text().trimmed();
    if (clipboardText.isEmpty()) {
        return;
    }

    // Check if this is the same URL we just auto-pasted
    if (clipboardText == m_lastAutoPastedUrl) {
        return;
    }

    // Try to auto-paste (this validates and sets the URL in the input)
    if (m_startTab->tryAutoPasteFromClipboard()) {
        // Update tracking
        m_lastAutoPastedUrl = clipboardText;
        m_lastAutoPasteTimestamp = QDateTime::currentMSecsSinceEpoch();

        // Switch to Start tab if not already there
        if (m_uiBuilder->tabWidget()->currentWidget() != m_startTab) {
            m_uiBuilder->tabWidget()->setCurrentWidget(m_startTab);
        }
        m_startTab->focusUrlInput();

        // Only auto-enqueue if forceEnqueue is true AND this is a new URL
        if (forceEnqueue) {
            m_startTab->onDownloadButtonClicked(); // Trigger download
        }
    }
}

void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) {
        if (isVisible()) {
            hide();
        } else {
            showNormal();
            activateWindow();
        }
    }
}

void MainWindow::onLocalApiEnqueueRequested(const QString &url, const QString &type) {
    // The slot signature in MainWindow.h must be updated to:
    // void onLocalApiEnqueueRequested(const QString &url, const QString &type);
    // Default to video download as requested. The settings will be handled automatically
    // by the pipeline exactly as if the user clicked "Download" on the Start tab.
    QVariantMap options;
    options["type"] = type.isEmpty() ? "video" : type;
    applyNonInteractiveDownloadDefaults(options);

    // Route it through the standard validation and queuing pipeline
    onDownloadRequested(url, options);
}

void MainWindow::onDownloadRequested(const QString &url, const QVariantMap &options) {
    const bool nonInteractive = isNonInteractiveRequest(options);

    if (!m_pendingUrl.isEmpty()) {
        if (nonInteractive) {
            qWarning() << "Ignoring non-interactive download request while another metadata request is pending:" << url;
        } else {
            QMessageBox::warning(this, "Please Wait", "Currently fetching info for another download.");
        }
        return;
    }

    QString type = options.value("type").toString();
    QStringList missingBinaries;

    if (type == "gallery") {
        QStringList required = {"gallery-dl", "ffmpeg", "ffprobe"};
        for (const QString &bin : required) {
            QString source = ProcessUtils::findBinary(bin, m_configManager).source;
            if (source == "Not Found" || source == "Invalid Custom") {
                missingBinaries << bin;
            }
        }
    } else if (type == "view_formats") {
        QString source = ProcessUtils::findBinary("yt-dlp", m_configManager).source;
        if (source == "Not Found" || source == "Invalid Custom") {
            missingBinaries << "yt-dlp";
        }
    } else {
        QStringList required = {"yt-dlp", "ffmpeg", "ffprobe", "deno"};
        for (const QString &bin : required) {
            QString source = ProcessUtils::findBinary(bin, m_configManager).source;
            if (source == "Not Found" || source == "Invalid Custom") {
                missingBinaries << bin;
            }
        }

        // Auto-revert to yt-dlp native downloader if aria2c is enabled but missing.
        // This prevents yt-dlp from instantly crashing and escapes the hidden-combobox UI trap.
        bool useAria2c = m_configManager->get("Metadata", "use_aria2c", false).toBool();
        if (useAria2c) {
            QString ariaSource = ProcessUtils::findBinary("aria2c", m_configManager).source;
            if (ariaSource == "Not Found" || ariaSource == "Invalid Custom") {
                qWarning() << "aria2c is enabled but missing. Auto-reverting to yt-dlp native downloader.";
                m_configManager->set("Metadata", "use_aria2c", false);
                m_configManager->save();
            }
        }
    }

    if (!missingBinaries.isEmpty()) {
        if (nonInteractive) {
            qWarning() << "Cannot queue non-interactive download because required binaries are missing:"
                       << missingBinaries.join(", ") << "URL:" << url;
            return;
        }

        if (!showMissingBinariesDialog(missingBinaries)) {
            return;
        }

        missingBinaries.clear();
        const QStringList requiredAfterSetup = type == "gallery"
            ? QStringList{"gallery-dl", "ffmpeg", "ffprobe"}
            : type == "view_formats"
                ? QStringList{"yt-dlp"}
                : QStringList{"yt-dlp", "ffmpeg", "ffprobe", "deno"};
        for (const QString &bin : requiredAfterSetup) {
            QString source = ProcessUtils::resolveBinary(bin, m_configManager).source;
            if (source == "Not Found" || source == "Invalid Custom") {
                missingBinaries << bin;
            }
        }
        if (!missingBinaries.isEmpty()) {
            return;
        }
    }

    QVariantMap mutableOptions = options;
    if (nonInteractive) {
        applyNonInteractiveDownloadDefaults(mutableOptions);
    }

    bool overrideArchive = mutableOptions.value("override_archive", m_configManager->get("General", "override_archive", false)).toBool();

    if (!overrideArchive && m_archiveManager && m_archiveManager->isInArchive(url)) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "Duplicate Download",
                                      QString("The following URL is already in your download history:\n%1\n\nDo you want to download it again?").arg(url),
                                      QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::No) {
            return;
        }
        mutableOptions["override_archive"] = true;
    }

    // Handle runtime subtitle selection. (Video and Audio runtime formats are handled internally by DownloadManager).
    bool runtimeSubs = m_configManager->get("Subtitles", "languages", "en").toString().split(',').contains("runtime");

    if (runtimeSubs && !nonInteractive) {
        m_pendingUrl = url;
        m_pendingOptions = mutableOptions;
        statusBar()->showMessage("Fetching media info for runtime selection...");
        QString ytDlpPath = ProcessUtils::findBinary("yt-dlp", m_configManager).path;
        QStringList args;
        args << "--dump-json" << "--no-playlist" << url;
        m_runtimeExtractor->extract(ytDlpPath, args);
        return;
    }

    static QRegularExpression fastTrackRe(R"(^(https?://)?(www\.)?(youtube\.com|youtu\.be|music\.youtube\.com|tiktok\.com|instagram\.com|twitter\.com|x\.com)/)");
    if (fastTrackRe.match(url).hasMatch()) {
        m_downloadManager->enqueueDownload(url, mutableOptions); // Corrected: m_downloadManager
        m_uiBuilder->tabWidget()->setCurrentWidget(m_activeDownloadsTab);
        return;
    }

    m_pendingUrl = url;
    m_pendingOptions = mutableOptions;

    m_urlValidator->validate(url);
}

void MainWindow::onRuntimeInfoReady(const QVariantMap &info) {
    statusBar()->clearMessage();
    bool runtimeSubs = m_configManager->get("Subtitles", "languages", "en").toString().split(',').contains("runtime");

    // We only use RuntimeSelectionDialog for subtitles now. Video/Audio formats use FormatSelectionDialog.
    RuntimeSelectionDialog dialog(info, false, false, runtimeSubs, this);
    if (dialog.exec() == QDialog::Accepted) {
        QVariantMap opts = m_pendingOptions;
        if (runtimeSubs) {
            QStringList subs = dialog.getSelectedSubtitles();
            if (!subs.isEmpty()) opts["runtime_subtitles"] = subs.join(',');
        }
        m_downloadManager->enqueueDownload(m_pendingUrl, opts);
        m_uiBuilder->tabWidget()->setCurrentWidget(m_activeDownloadsTab);
    }
    m_pendingUrl.clear();
    m_pendingOptions.clear();
}

void MainWindow::onRuntimeInfoError(const QString &error) {
    statusBar()->clearMessage();
    if (isNonInteractiveRequest(m_pendingOptions)) {
        qWarning() << "Runtime metadata extraction failed for non-interactive request:" << error;
    } else {
        QMessageBox::warning(this, "Extraction Error", "Failed to fetch media info for runtime selection:\n" + error);
    }
    m_pendingUrl.clear();
    m_pendingOptions.clear();
}

void MainWindow::onDownloadSectionsRequested(const QString &url, const QVariantMap &options, const QVariantMap &infoJson)
{
    if (isNonInteractiveRequest(options)) {
        QVariantMap newOptions = options;
        newOptions["download_sections_set"] = true;
        qInfo() << "Skipping download sections dialog for non-interactive request:" << url;
        m_downloadManager->enqueueDownload(url, newOptions);
        m_uiBuilder->tabWidget()->setCurrentWidget(m_activeDownloadsTab);
        return;
    }

    DownloadSectionsDialog dialog(infoJson, this);
    if (dialog.exec() == QDialog::Accepted) {
        QString sections = dialog.getSectionsString();
        QString sectionLabel = dialog.getFilenameLabel();
        QVariantMap newOptions = options;
        newOptions["download_sections_set"] = true; // Mark as done to prevent looping
        if (!sections.isEmpty()) {
            newOptions["download_sections"] = sections;
        }
        if (!sectionLabel.isEmpty()) {
            newOptions["download_sections_label"] = sectionLabel;
        }
        // Re-enqueue with the new options
        m_downloadManager->enqueueDownload(url, newOptions);
        m_uiBuilder->tabWidget()->setCurrentWidget(m_activeDownloadsTab);
    } else {
        // User cancelled, do nothing. The download was never enqueued.
        qInfo() << "Download sections selection cancelled by user for" << url;
    }
}

void MainWindow::onValidationFinished(bool isValid, const QString &error) {
    if (isValid) {
        m_downloadManager->enqueueDownload(m_pendingUrl, m_pendingOptions); // Corrected: m_downloadManager
        m_uiBuilder->tabWidget()->setCurrentWidget(m_activeDownloadsTab);
    } else {
        if (isNonInteractiveRequest(m_pendingOptions)) {
            qWarning() << "Non-interactive URL validation failed for" << m_pendingUrl << ":" << error;
        } else {
            QMessageBox::warning(this, "Invalid URL", "The URL could not be validated:\n" + error);
        }
    }
    m_pendingUrl.clear();
    m_pendingOptions.clear();
}

void MainWindow::onQueueFinished() {
    if (!m_configManager) {
        return;
    }

    // Notify the user that the queue has finished
    if (m_trayIcon && m_trayIcon->isVisible()) {
        m_trayIcon->showMessage("Downloads Complete", "All queued media downloads have finished.",
                                QSystemTrayIcon::Information, 3000);
    }

    bool exitAfter = m_configManager->get("General", "exit_after", false).toBool();
    if (exitAfter) {
        qInfo() << "Queue finished and 'exit after' is enabled. Waiting 2 seconds before quitting to allow for final file cleanup.";
        QTimer::singleShot(2000, this, [this]() {
            if (!m_configManager || !m_uiBuilder) {
                return;
            }

            const bool exitStillEnabled = m_configManager->get("General", "exit_after", false).toBool();
            if (!exitStillEnabled) {
                qInfo() << "Exit-after timer cancelled because the setting was turned off before shutdown.";
                return;
            }

            int activeCount = 0;
            int queuedCount = 0;

            const QString activeText = m_uiBuilder->activeDownloadsLabel()->text();
            if (activeText.startsWith("Active: ")) {
                activeCount = activeText.mid(8).toInt();
            }

            const QString queuedText = m_uiBuilder->queuedDownloadsLabel()->text();
            if (queuedText.startsWith("Queued: ")) {
                queuedCount = queuedText.mid(8).toInt();
            }

            if (activeCount == 0 && queuedCount == 0) {
                QCoreApplication::quit();
                return;
            }

            qInfo() << "Exit-after timer cancelled because downloads resumed before shutdown."
                    << "Active:" << activeCount << "Queued:" << queuedCount;
        });
    }
}

void MainWindow::startStartupChecks() {
    m_startupThread->start();
}

bool MainWindow::showMissingBinariesDialog(const QStringList &missingBinaries)
{
    if (missingBinaries.isEmpty()) {
        return true;
    }

    BinariesPage *binariesPage = m_advancedSettingsTab
        ? m_advancedSettingsTab->findChild<BinariesPage*>()
        : nullptr;

    MissingBinariesDialog dialog(missingBinaries, m_configManager, binariesPage, this);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    const bool resolved = dialog.allBinariesResolved();

    if (resolved) {
        ProcessUtils::clearCache();
        if (m_startTab) {
            m_startTab->updateDynamicUI();
        }
        return true;
    }

    if (accepted) {
        qWarning() << "Missing binary setup dialog accepted before all binaries resolved:"
                   << missingBinaries.join(", ");
    }
    return false;
}

void MainWindow::onVideoQualityWarning(const QString &url, const QString &message) {
    if (m_nonInteractiveLaunch) {
        qWarning() << "Low quality video warning for non-interactive launch:" << url << message;
        return;
    }

    QMessageBox::warning(this, "Low Quality Video",
                         QString("The following video was downloaded at a low quality:\n%1\n\n%2").arg(url, message));
}

void MainWindow::applyTheme(const QString &themeName) {
    qApp->setStyle(QStyleFactory::create("Fusion"));

    bool useDarkTheme = false;
    if (themeName == "Dark") {
        useDarkTheme = true;
    } else if (themeName == "System") {
        const auto colorScheme = qApp->styleHints()->colorScheme();
        useDarkTheme = (colorScheme == Qt::ColorScheme::Dark);
    }

    if (useDarkTheme) {
        QPalette darkPalette;
        darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
        darkPalette.setColor(QPalette::WindowText, Qt::white);
        darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
        darkPalette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
        darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
        darkPalette.setColor(QPalette::ToolTipText, Qt::white);
        darkPalette.setColor(QPalette::Text, Qt::white);
        darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
        darkPalette.setColor(QPalette::ButtonText, Qt::white);
        darkPalette.setColor(QPalette::BrightText, Qt::red);
        darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
        darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        darkPalette.setColor(QPalette::HighlightedText, Qt::black);
        darkPalette.setColor(QPalette::Mid, QColor(40, 40, 40));
        qApp->setPalette(darkPalette);
    } else { // Light theme
        QPalette lightPalette(QColor(240, 240, 240));
        lightPalette.setColor(QPalette::WindowText, Qt::black);
        lightPalette.setColor(QPalette::Base, Qt::white);
        lightPalette.setColor(QPalette::AlternateBase, QColor(246, 246, 246));
        lightPalette.setColor(QPalette::ToolTipBase, Qt::white);
        lightPalette.setColor(QPalette::ToolTipText, Qt::black);
        lightPalette.setColor(QPalette::Text, Qt::black);
        lightPalette.setColor(QPalette::ButtonText, Qt::black);
        lightPalette.setColor(QPalette::BrightText, Qt::red);
        lightPalette.setColor(QPalette::Link, QColor(42, 130, 218));
        lightPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        lightPalette.setColor(QPalette::HighlightedText, Qt::white);
        qApp->setPalette(lightPalette);
    }
}

void MainWindow::updateTotalSpeed(double speed) {
    double speedMb = speed / (1024.0 * 1024.0); // Corrected: m_uiBuilder->speedLabel()
    m_uiBuilder->speedLabel()->setText(QString("Current Speed: %1 MB/s").arg(speedMb, 0, 'f', 2));
}

void MainWindow::onDownloadStatsUpdated(int queued, int active, int completed, int errors) {
    m_uiBuilder->queuedDownloadsLabel()->setText(QString("Queued: %1").arg(queued));
    m_uiBuilder->activeDownloadsLabel()->setText(QString("Active: %1").arg(active));
    m_uiBuilder->completedDownloadsLabel()->setText(QString("Completed: %1").arg(completed));
    m_uiBuilder->errorDownloadsLabel()->setText(QString("Errors: %1").arg(errors));
}

void MainWindow::onYtDlpErrorPopup(const QString &id, const QString &errorType, const QString &userMessage, const QString &rawError, const QVariantMap &itemData) {
    Q_UNUSED(id);

    QString url = itemData.value("url").toString();
    QVariantMap requestOptions = itemData.value("options").toMap();
    const bool nonInteractive = isNonInteractiveRequest(requestOptions);
    QString urlHtml = url.isEmpty() ? "" : QString("<br><br><a href=\"%1\">%1</a>").arg(url.toHtmlEscaped());
    QString richUserMessage = userMessage.toHtmlEscaped().replace("\n", "<br>");

    // Clean up the raw error string to make it more user-friendly
    QString cleanError = rawError;
    if (cleanError.startsWith("ERROR: ")) {
        cleanError = cleanError.mid(7); // Remove "ERROR: "
    }
    // Remove "[extractor] " prefix if present (e.g. "[youtube] ")
    cleanError.remove(QRegularExpression("^\\[[^\\]]+\\]\\s*"));

    if (errorType == "scheduled_livestream") {
        if (nonInteractive) {
            QVariantMap newItemData = itemData;
            QVariantMap options = requestOptions;
            options["wait_for_video"] = true;
            options["livestream_wait_min"] = m_configManager->get("Livestream", "wait_for_video_min", 60).toInt();
            options["livestream_wait_max"] = m_configManager->get("Livestream", "wait_for_video_max", 300).toInt();
            applyNonInteractiveDownloadDefaults(options);
            newItemData["options"] = options;
            qInfo() << "Automatically waiting for scheduled livestream in non-interactive request:" << url;
            m_downloadManager->restartDownloadWithOptions(newItemData);
            return;
        }

        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Scheduled Livestream");
        msgBox.setTextFormat(Qt::RichText);
        msgBox.setTextInteractionFlags(Qt::TextBrowserInteraction);
        msgBox.setText(richUserMessage + urlHtml);
        if (!cleanError.isEmpty()) {
            msgBox.setInformativeText(cleanError.toHtmlEscaped().replace("\n", "<br>"));
        }
        msgBox.setIcon(QMessageBox::Information);

        QPushButton *waitButton = msgBox.addButton("Wait and Download When Available", QMessageBox::AcceptRole);
        msgBox.addButton(QMessageBox::Cancel);

        msgBox.exec();

        if (msgBox.clickedButton() == waitButton) {
            QVariantMap newItemData = itemData;
            QVariantMap options = newItemData["options"].toMap();
            options["wait_for_video"] = true;

            // Dynamically adjust wait time based on how far away the stream is.
            int minWait, maxWait;
            if (cleanError.contains("days", Qt::CaseInsensitive) || cleanError.contains("hours", Qt::CaseInsensitive)) {
                minWait = 1800; // 30 minutes
                maxWait = 3600; // 60 minutes
            } else if (cleanError.contains("minutes", Qt::CaseInsensitive)) {
                minWait = 60; // 1 minute
                maxWait = 300; // 5 minutes
            } else {
                minWait = m_configManager->get("Livestream", "wait_for_video_min", 60).toInt();
                maxWait = m_configManager->get("Livestream", "wait_for_video_max", 300).toInt();
            }

            options["livestream_wait_min"] = minWait;
            options["livestream_wait_max"] = maxWait;
            newItemData["options"] = options;
            m_downloadManager->restartDownloadWithOptions(newItemData);
        }
    } else {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Download Error");
        msgBox.setTextFormat(Qt::RichText);
        msgBox.setTextInteractionFlags(Qt::TextBrowserInteraction);
        msgBox.setText(richUserMessage + urlHtml);
        if (!cleanError.isEmpty()) {
            msgBox.setInformativeText(cleanError.toHtmlEscaped().replace("\n", "<br>"));
        }
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.exec();
    }
}

void MainWindow::setYtDlpVersion(const QString &version) {
    // Deprecated: BinariesPage autonomously fetches its own versions now.
    // Retained here to satisfy MOC and the linker.
    Q_UNUSED(version);
}
