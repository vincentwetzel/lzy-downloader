#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "MainWindowUiBuilder.h"
#include "StartTab.h"
#include "ActiveDownloadsTab.h"
#include "AdvancedSettingsTab.h"
#include "DownloadHistoryTab.h"
#include "FormatSelectionDialog.h"
#include "ui/advanced_settings/BinariesPage.h"

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
#include <QFileInfo>
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
        if (consoleWindow == nullptr) {
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
        if (consoleWindow != nullptr && s_consoleAllocatedByUs) {
            ShowWindow(consoleWindow, SW_HIDE);
        }
    }
}
}
#endif

namespace {
constexpr const char* GITHUB_PROJECT_URL = "https://github.com/vincentwetzel/lzy-downloader";
}

void MainWindow::setupLocalApiServer()
{
    m_localApiServer = new LocalApiServer(m_configManager, this);
    connect(m_localApiServer, &LocalApiServer::enqueueRequested, this, &MainWindow::onLocalApiEnqueueRequested);
    connect(m_localApiServer, &LocalApiServer::enqueueWithCookieFileRequested,
            this, &MainWindow::onLocalApiEnqueueWithCookieFileRequested);
    connect(m_localApiServer, &LocalApiServer::cancelRequested, this, &MainWindow::onLocalApiCancelRequested);
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
                    m_appUpdateInstalling = true;
                    m_appUpdateCheckPending = false;
                    statusBar()->showMessage(tr("Downloading update..."));
                    m_appUpdater->downloadAndInstall(downloadUrl);
                } else if (msgBox.clickedButton() == viewReleaseButton) {
                    QDesktopServices::openUrl(QUrl(QStringLiteral("%1/releases/latest").arg(QLatin1String(GITHUB_PROJECT_URL))));
                    m_appUpdateCheckPending = false;
                    releaseDeferredStartupBinaryUpdates();
                    showStartupBinarySetupIfReady();
                } else {
                    m_appUpdateCheckPending = false;
                    releaseDeferredStartupBinaryUpdates();
                    showStartupBinarySetupIfReady();
                }
            });

    connect(m_appUpdater, &AppUpdater::noUpdateAvailable, this, [this]() {
        m_silentUpdateCheck = false;
        m_appUpdateCheckPending = false;
        releaseDeferredStartupBinaryUpdates();
        showStartupBinarySetupIfReady();
        qInfo() << "No app update available. Current version:" << APP_VERSION_STRING;
    });

    connect(m_appUpdater, &AppUpdater::updateCheckFailed, this, [this](const QString &error) {
        const bool wasSilent = m_silentUpdateCheck;
        m_silentUpdateCheck = false;
        m_appUpdateInstalling = false;
        m_appUpdateCheckPending = false;
        releaseDeferredStartupBinaryUpdates();
        showStartupBinarySetupIfReady();
        qWarning() << "App update check failed:" << error;
        if (!wasSilent && !m_nonInteractiveLaunch) {
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

    connect(m_appUpdater, &AppUpdater::installingUpdate, this, [this]() {
        // AppUpdater normally quits directly after launching the installer, which
        // bypasses closeEvent(). Explicitly perform the same synchronous cleanup
        // first so active yt-dlp/gallery-dl/FFmpeg processes do not keep the EXE
        // locked while NSIS replaces it. shutdown() also persists resumable queue
        // state before terminating the worker process trees.
        qInfo() << "Preparing application update: saving queue and stopping downloads.";
        if (m_downloadManager) {
            m_downloadManager->shutdown();
        }
    });
}

void MainWindow::scheduleInitialSetup()
{
    m_appUpdateCheckPending = !m_nonInteractiveLaunch;
    QTimer::singleShot(0, this, [this]() {
        const bool isNonInteractive = m_nonInteractiveLaunch;

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
                                             .arg(QDir(completedDownloadsDir).filePath(QStringLiteral("temp_downloads"))));
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
