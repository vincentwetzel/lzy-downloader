#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "MainWindowUiBuilder.h"
#include "StartTab.h"
#include "ActiveDownloadsTab.h"
#include "AdvancedSettingsTab.h"
#include "FormatSelectionDialog.h"

#include "core/version.h"
#include "core/PlaylistRangeDialog.h"
#include "core/AppUpdater.h"
#include "core/ConfigManager.h"
#include "core/DownloadManager.h"
#include "core/LocalApiServer.h"
#include "core/StartupWorker.h"
#include "core/UrlValidator.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <chrono>

#ifdef Q_OS_WIN
#include <windows.h>
#include <cstdio>

namespace {
bool s_consoleAllocatedByUs = false;

void applyConsoleState(bool show)
{
    HWND consoleWindow = GetConsoleWindow();
    if (show) {
        if (consoleWindow == NULL) {
            if (AllocConsole()) {
                s_consoleAllocatedByUs = true;
                QCoreApplication::instance()->setProperty("lzy_consoleAllocatedByUs", true);
                FILE *dummy;
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
}
#endif

namespace {
const QString GITHUB_PROJECT_URL = QStringLiteral("https://github.com/vincentwetzel/lzy-downloader");
}

void MainWindow::setupLocalApiServer()
{
    m_localApiServer = new LocalApiServer(m_configManager, this);
    connect(m_localApiServer, &LocalApiServer::enqueueRequested, this, &MainWindow::onLocalApiEnqueueRequested);
    const bool serverMode = MainWindowHelpers::hasServerLaunchArgument();
    if (serverMode || m_configManager->get(QStringLiteral("General"), QStringLiteral("enable_local_api"), false).toBool()) {
        qInfo() << "[LocalApi] Attempting to start Local API Server on startup..."
                << "serverMode:" << serverMode;
        m_localApiServer->start();
    }
    connect(m_configManager, &ConfigManager::settingChanged, this, [this, serverMode](const QString &section, const QString &key, const QVariant &value) {
        if (section == QStringLiteral("General") && key == QStringLiteral("enable_local_api")) {
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
}

void MainWindow::setupWindowsDebugConsole()
{
#ifdef Q_OS_WIN
    bool isDebug = false;
#ifdef QT_DEBUG
    isDebug = true;
#elif !defined(NDEBUG)
    isDebug = true;
#endif
    bool showConsole = m_configManager->get(QStringLiteral("General"), QStringLiteral("show_debug_console"), isDebug).toBool();
    applyConsoleState(showConsole);
    connect(m_configManager, &ConfigManager::settingChanged, this, [](const QString &section, const QString &key, const QVariant &value) {
        if (section == QStringLiteral("General") && key == QStringLiteral("show_debug_console")) {
            applyConsoleState(value.toBool());
        }
    });
#endif
}

void MainWindow::connectAppUpdaterSignals()
{
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
                msgBox.setWindowTitle(tr("Update Available"));
                msgBox.setText(tr("LzyDownloader %1 is available. You are currently running %2.")
                                   .arg(latestVersion, QStringLiteral(APP_VERSION_STRING)));

                QString informativeText = tr("Would you like to download and install the update now?");
                if (!releaseNotes.trimmed().isEmpty()) {
                    QString trimmedNotes = releaseNotes.trimmed();
                    if (trimmedNotes.size() > 1200) {
                        trimmedNotes = tr("%1\n\n[Release notes truncated]").arg(trimmedNotes.left(1200).trimmed());
                    }
                    informativeText = tr("%1\n\nRelease notes:\n%2").arg(informativeText, trimmedNotes);
                }
                msgBox.setInformativeText(informativeText);

                QPushButton *updateNowButton = msgBox.addButton(tr("Update Now"), QMessageBox::AcceptRole);
                QPushButton *viewReleaseButton = msgBox.addButton(tr("View Release"), QMessageBox::ActionRole);
                msgBox.addButton(QMessageBox::Cancel);
                msgBox.exec();

                if (msgBox.clickedButton() == updateNowButton) {
                    statusBar()->showMessage(tr("Downloading update..."));
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
            QMessageBox::warning(this, tr("Update Check Failed"), error);
        }
    });

    connect(m_appUpdater, &AppUpdater::downloadProgress, this, [this](qint64 bytesReceived, qint64 bytesTotal) {
        if (bytesTotal > 0) {
            const double percent = (static_cast<double>(bytesReceived) / static_cast<double>(bytesTotal)) * 100.0;
            statusBar()->showMessage(tr("Downloading update... %1%").arg(percent, 0, 'f', 1));
        } else {
            statusBar()->showMessage(tr("Downloading update..."));
        }
    });

    connect(m_appUpdater, &AppUpdater::downloadFinished, this, [this]() {
        statusBar()->showMessage(tr("Update downloaded. Launching installer..."), 5000);
    });
}

void MainWindow::scheduleInitialSetup()
{
    QTimer::singleShot(0, this, [this]() {
        bool isHeadless = QCoreApplication::arguments().contains("--headless") || QCoreApplication::arguments().contains("--server");
        bool isNonInteractive = m_nonInteractiveLaunch;

        if (isHeadless && m_trayIcon) {
            m_trayIcon->showMessage(QStringLiteral("LzyDownloader"), tr("Running in headless server mode."), QSystemTrayIcon::Information, 3000);
        }

        QString completedDownloadsDir = m_configManager->get(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory")).toString();
        if (completedDownloadsDir.isEmpty()) {
            if (isNonInteractive) {
                QString baseDownloadDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
                if (baseDownloadDir.isEmpty()) {
                    baseDownloadDir = QDir::homePath();
                }
                completedDownloadsDir = QDir(baseDownloadDir).filePath(QStringLiteral("LzyDownloader"));
                if (m_configManager->set(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory"), completedDownloadsDir)) {
                    m_configManager->save();
                }
                qInfo() << "Set default completed downloads directory for non-interactive launch:" << completedDownloadsDir;
            } else {
                QMessageBox::information(this, tr("Setup Required"),
                                         tr("Please select a directory for completed downloads. This will also set up a temporary downloads directory."));
                QString selectedDir = QFileDialog::getExistingDirectory(this, tr("Select Completed Downloads Directory"),
                                                                        QDir::homePath(), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
                if (!selectedDir.isEmpty()) {
                    if (m_configManager->set(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory"), selectedDir)) {
                        m_configManager->save();
                    }
                    completedDownloadsDir = selectedDir;
                    QMessageBox::information(this, tr("Directory Set"),
                                             tr("Completed downloads directory set to:\n%1\n\nTemporary downloads directory set to:\n%2")
                                                 .arg(completedDownloadsDir)
                                                 .arg(QDir(completedDownloadsDir).filePath("temp_downloads")));
                } else {
                    QMessageBox::warning(this, tr("Directory Not Set"),
                                         tr("No completed downloads directory was selected. Please set it in Advanced Settings to enable downloads."));
                }
            }
        }

        QString temporaryDownloadsDir = m_configManager->get(QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory")).toString();
        if (!completedDownloadsDir.isEmpty() && temporaryDownloadsDir.isEmpty()) {
            QString defaultTempDir = QDir(completedDownloadsDir).filePath(QStringLiteral("temp_downloads"));
            if (m_configManager->set(QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory"), defaultTempDir)) {
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
}

void MainWindow::connectDownloadManagerSignals()
{
    // CRITICAL: Explicitly flush state and tear down workers before quitting!
    // This compensates for QCoreApplication::quit() bypassing MainWindow::closeEvent
    // during automated headless shutdowns (like --server --exit-after).
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this]() {
        if (m_downloadManager) {
            qInfo() << "Executing headless shutdown/cleanup sequence before event loop terminates...";
            m_downloadManager->shutdown();
        }
    });

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
            m_activeDownloadsTab, &ActiveDownloadsTab::removeExpandingPlaylist);
    connect(m_downloadManager, &DownloadManager::queueFinished, this, &MainWindow::onQueueFinished);
    connect(m_downloadManager, &DownloadManager::totalSpeedUpdated, this, &MainWindow::updateTotalSpeed);
    connect(m_downloadManager, &DownloadManager::videoQualityWarning, this, &MainWindow::onVideoQualityWarning);
    connect(m_downloadManager, &DownloadManager::downloadStatsUpdated, this, &MainWindow::onDownloadStatsUpdated);

    connect(m_downloadManager, &DownloadManager::downloadAddedToQueue, m_localApiServer, &LocalApiServer::onDownloadAdded);
    connect(m_downloadManager, &DownloadManager::downloadProgress, m_localApiServer, &LocalApiServer::onDownloadProgress);
    connect(m_downloadManager, &DownloadManager::downloadFinished, m_localApiServer, &LocalApiServer::onDownloadFinished);
    connect(m_downloadManager, &DownloadManager::downloadRemovedFromQueue, m_localApiServer, &LocalApiServer::onDownloadRemoved);

    connectDiscordWebhookSignals();

    connect(m_downloadManager, &DownloadManager::duplicateDownloadDetected, m_startTab, &StartTab::onDuplicateDownloadDetected);
    connect(m_downloadManager, &DownloadManager::ytDlpErrorPopupRequested, this, &MainWindow::onYtDlpErrorPopup);
    connect(m_downloadManager, &DownloadManager::downloadSectionsRequested, this, &MainWindow::onDownloadSectionsRequested);

    connect(m_downloadManager, &DownloadManager::playlistActionRequested, this,
            [this](const QString &url, int itemCount, const QVariantMap &options, const QList<QVariantMap> &expandedItems) {
                if (MainWindowHelpers::isNonInteractiveRequest(options)) {
                    qInfo() << "Non-interactive playlist request detected; queueing all playlist items for" << url << "count:" << itemCount;
                    m_downloadManager->processPlaylistSelection(url, "Download All", options, expandedItems);
                    m_uiBuilder->tabWidget()->setCurrentWidget(m_activeDownloadsTab);
                    return;
                }

                QMessageBox msgBox(this);
                msgBox.setIcon(QMessageBox::Question);
                msgBox.setWindowTitle(tr("Playlist Detected"));
                msgBox.setText(tr("This URL contains a playlist with %1 item(s).").arg(itemCount));
                msgBox.setInformativeText(tr("Do you want to queue every item or just the first one?"));

                QPushButton *downloadAllButton = msgBox.addButton(tr("Download All"), QMessageBox::AcceptRole);
                QPushButton *downloadPartButton = msgBox.addButton(tr("Download Part..."), QMessageBox::ActionRole);
                QPushButton *downloadSingleButton = msgBox.addButton(tr("Download Single Item"), QMessageBox::ActionRole);
                QPushButton *cancelButton = msgBox.addButton(QMessageBox::Cancel);

                msgBox.exec();

                QString action;
                QList<QVariantMap> itemsToProcess = expandedItems;

                if (msgBox.clickedButton() == downloadAllButton) {
                    action = QStringLiteral("Download All");
                } else if (msgBox.clickedButton() == downloadPartButton) {
                    PlaylistRangeDialog rangeDialog(expandedItems, this);
                    if (rangeDialog.exec() == QDialog::Accepted) {
                        itemsToProcess = rangeDialog.getSelectedItems();
                        if (!itemsToProcess.isEmpty()) {
                            action = QStringLiteral("Download Part");
                        } else {
                            action = QStringLiteral("Cancel");
                        }
                    } else {
                        action = QStringLiteral("Cancel");
                    }
                } else if (msgBox.clickedButton() == downloadSingleButton) {
                    action = QStringLiteral("Download Single Item");
                } else if (msgBox.clickedButton() == cancelButton) {
                    action = QStringLiteral("Cancel");
                }

                m_downloadManager->processPlaylistSelection(url, action, options, itemsToProcess);
                m_uiBuilder->tabWidget()->setCurrentWidget(m_activeDownloadsTab);
            });

    connect(m_downloadManager, &DownloadManager::formatSelectionRequested, this,
            [this](const QString &url, const QVariantMap &options, const QVariantMap &infoDict) {
                if (MainWindowHelpers::isNonInteractiveRequest(options)) {
                    QVariantMap newOptions = options;
                    newOptions[QStringLiteral("runtime_format_selected")] = true;
                    qInfo() << "Skipping runtime format dialog for non-interactive request:" << url;
                    m_downloadManager->enqueueDownload(url, newOptions);
                    return;
                }

                FormatSelectionDialog dialog(infoDict, options, this);
                if (dialog.exec() == QDialog::Accepted) {
                    QStringList selectedFormats = dialog.getSelectedFormatIds();
                    if (!selectedFormats.isEmpty()) {
                        for (const QString &formatId : selectedFormats) {
                            QVariantMap newOptions = options;
                            newOptions[QStringLiteral("runtime_format_selected")] = true;
                            newOptions[QStringLiteral("format")] = formatId;
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
    connect(m_activeDownloadsTab, &ActiveDownloadsTab::finishDownloadRequested,
            m_downloadManager, &DownloadManager::finishDownload);
    connect(m_activeDownloadsTab, &ActiveDownloadsTab::itemCleared,
            m_downloadManager, &DownloadManager::onItemCleared);

    connect(m_urlValidator, &UrlValidator::validationFinished, this, &MainWindow::onValidationFinished);
}

void MainWindow::connectStartupWorkerSignals()
{
    m_startupWorker->moveToThread(m_startupThread);
    connect(m_startupThread, &QThread::started, m_startupWorker, &StartupWorker::start);
    connect(m_startupWorker, &StartupWorker::finished, m_startupThread, &QThread::quit);
    connect(m_startupWorker, &StartupWorker::binariesChecked, this, [this](const QStringList &missingBinaries) {
        if (!missingBinaries.isEmpty() && !m_nonInteractiveLaunch) {
            showMissingBinariesDialog(missingBinaries);
        }
    });
    connect(m_startupWorker, &StartupWorker::ytDlpVersionFetched, this, &MainWindow::setYtDlpVersion);
    connect(m_startupWorker, &StartupWorker::galleryDlVersionFetched, m_advancedSettingsTab, &AdvancedSettingsTab::setGalleryDlVersion);
    connect(m_clipboard, &QClipboard::changed, this, &MainWindow::onClipboardChanged);
}

void MainWindow::queueDirectCliDownload()
{
    QString cliUrl = MainWindowHelpers::directCliUrl();
    if (!cliUrl.isEmpty()) {
        QTimer::singleShot(std::chrono::milliseconds(500), this, [this, cliUrl]() {
            QVariantMap options;
            if (QCoreApplication::arguments().contains("--audio")) {
                options[QStringLiteral("type")] = QStringLiteral("audio");
            } else {
                options[QStringLiteral("type")] = QStringLiteral("video");
            }
            MainWindowHelpers::applyNonInteractiveDownloadDefaults(options);
            onDownloadRequested(cliUrl, options);
        });
    }
}
