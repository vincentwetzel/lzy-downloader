#include "core/version.h"
#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "MainWindowUiBuilder.h"

#include "core/ConfigManager.h"
#include "core/ArchiveManager.h"
#include "core/DownloadManager.h"
#include "core/AppUpdater.h"
#include "core/UrlValidator.h"
#include "core/StartupWorker.h"
#include "core/download_pipeline/YtDlpDownloadInfoExtractor.h"
#include "utils/BinaryFinder.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QThread>

const QStringList REPO_URLS = {
    "https://api.github.com/repos/vincentwetzel/lzy-downloader",
    "https://api.github.com/repos/vincentwetzel/MediaDownloader"
};

MainWindow::MainWindow(ExtractorJsonParser *extractorJsonParser, QWidget *parent)
    : QMainWindow(parent), m_configManager(nullptr), m_archiveManager(nullptr), m_downloadManager(nullptr),
      m_appUpdater(nullptr), m_urlValidator(nullptr), m_startupWorker(nullptr), m_startupThread(nullptr),
      m_extractorJsonParser(extractorJsonParser), m_runtimeExtractor(nullptr), m_uiBuilder(nullptr),
      m_localApiServer(nullptr),
      m_clipboard(nullptr), m_startTab(nullptr), m_activeDownloadsTab(nullptr),
      m_advancedSettingsTab(nullptr), m_trayIcon(nullptr), m_trayMenu(nullptr),
      m_silentUpdateCheck(false), m_nonInteractiveLaunch(MainWindowHelpers::hasNonInteractiveLaunchArgument()), m_lastAutoPasteTimestamp(0)
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

    setupLocalApiServer();
    setupWindowsDebugConsole();

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
    connectAppUpdaterSignals();
    scheduleInitialSetup();
    connectDownloadManagerSignals();
    connectStartupWorkerSignals();
    startStartupChecks();
    queueDirectCliDownload();
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

