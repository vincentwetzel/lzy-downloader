#include "core/version.h"
#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "MainWindowUiBuilder.h"
#include "CoordinatorWindowActivation.h"
#include "DownloadHistoryTab.h"

#include "core/AppUpdater.h"
#include "core/ConfigManager.h"
#include "core/DownloadManager.h"
#include "core/LocalApiServer.h"
#include "core/StartupWorker.h"
#include "core/UrlValidator.h"
#include "core/download_pipeline/YtDlpDownloadInfoExtractor.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QShowEvent>
#include <QThread>
#include <QTimer>

MainWindow::MainWindow(ExtractorJsonParser *extractorJsonParser, QWidget *parent)
    : QMainWindow(parent), m_configManager(nullptr), m_downloadManager(nullptr),
      m_appUpdater(nullptr), m_urlValidator(nullptr), m_startupWorker(nullptr), m_startupThread(nullptr),
      m_extractorJsonParser(extractorJsonParser), m_runtimeExtractor(nullptr), m_uiBuilder(nullptr),
      m_localApiServer(nullptr),
      m_clipboard(nullptr), m_startTab(nullptr), m_activeDownloadsTab(nullptr),
      m_advancedSettingsTab(nullptr), m_sortingTab(nullptr),
      m_trayIcon(nullptr), m_trayMenu(nullptr),
      m_silentUpdateCheck(false), m_appUpdateCheckPending(false), m_appUpdatePromptActive(false),
      m_appUpdateInstalling(false),
      m_startupChecksFinished(false), m_startupSetupPresented(false),
      m_nonInteractiveLaunch(MainWindowHelpers::hasNonInteractiveLaunchArgument()),
      m_lastAutoPasteTimestamp(0), m_deferredClipboardAutoPaste(false),
      m_deferredClipboardForceEnqueue(false), m_skipInitialFocusAutoEnqueue(true)
{
    // A server-mode owner is hidden because main.cpp never calls show() for
    // it. Avoid WA_DontShowOnScreen there so a later show-ui command can map
    // this same window normally. Direct URL launches still pass through
    // main.cpp's show() call and need the attribute to remain non-interactive.
    if (m_nonInteractiveLaunch && !MainWindowHelpers::hasServerLaunchArgument()) {
        setAttribute(Qt::WA_DontShowOnScreen, true);
    }

    // Initialize core components
    m_configManager = new ConfigManager(QStringLiteral("settings.ini"), this);
    qInfo() << "Using settings file at:" << QDir(m_configManager->getConfigDir()).filePath(QStringLiteral("settings.ini"));

    // Apply CLI overrides and ensure exit_after resets to false on normal launches
    if (QCoreApplication::arguments().contains(QStringLiteral("--exit-after"))) {
        m_configManager->set(QStringLiteral("General"), QStringLiteral("exit_after"), true);
    } else {
        m_configManager->set(QStringLiteral("General"), QStringLiteral("exit_after"), false);
    }
    m_configManager->save();
    qInfo() << "[MainWindow] Launch lifecycle:"
            << "pid=" << QCoreApplication::applicationPid()
            << "non_interactive=" << m_nonInteractiveLaunch
            << "server=" << QCoreApplication::arguments().contains(QStringLiteral("--server"))
            << "exit_after=" << m_configManager->get(QStringLiteral("General"), QStringLiteral("exit_after"), false).toBool();

    m_downloadManager = new DownloadManager(m_configManager, this);
    const QStringList repoUrls = {
        QStringLiteral("https://api.github.com/repos/vincentwetzel/lzy-downloader"),
        QStringLiteral("https://api.github.com/repos/vincentwetzel/MediaDownloader")
    };
    m_appUpdater = new AppUpdater(repoUrls, QStringLiteral(APP_VERSION_STRING), this);
    m_urlValidator = new UrlValidator(m_configManager, this);
    m_clipboard = QApplication::clipboard(); // Initialize QClipboard

    const QStringList trackedBinaries = {QStringLiteral("yt-dlp"), QStringLiteral("ffmpeg"),
                                         QStringLiteral("ffprobe"), QStringLiteral("gallery-dl"),
                                         QStringLiteral("aria2c"), QStringLiteral("deno")};
    bool hadSavedBinaryConfiguration = false;
    for (const QString &binary : trackedBinaries) {
        if (!m_configManager->get(QStringLiteral("Binaries"), binary + QStringLiteral("_path")).toString().isEmpty()) {
            hadSavedBinaryConfiguration = true;
            break;
        }
    }
    if (hadSavedBinaryConfiguration && !m_configManager->get(QStringLiteral("Binaries"), QStringLiteral("setup_completed"), false).toBool()) {
        m_configManager->set(QStringLiteral("Binaries"), QStringLiteral("setup_completed"), true);
    }

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
    applyTheme(m_configManager->get(QStringLiteral("General"), QStringLiteral("theme"), QStringLiteral("System")).toString());

    setupLocalApiServer();
    setupWindowsDebugConsole();
    setupUI();
    setupTrayIcon();
    connectAppUpdaterSignals();
    scheduleInitialSetup();
    connectDownloadManagerSignals();
    connectStartupWorkerSignals();
    if (m_nonInteractiveLaunch) {
        startStartupChecks();
    }
    queueDirectCliDownload();

    // Build secondary tabs after show() returns so they are ready by the time
    // the user first opens them, without putting their construction on the
    // click path.
    if (!m_nonInteractiveLaunch) {
        preloadSecondaryTabs();
    }
}

MainWindow::~MainWindow() {
    if (m_startupThread && m_startupThread->isRunning()) {
        m_startupThread->quit();
        m_startupThread->wait();
    } else if (!m_startupChecksStarted && m_startupWorker) {
        // A very early window close can cancel the delayed GUI startup check
        // before its thread has ever started.
        delete m_startupWorker;
        m_startupWorker = nullptr;
    }
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (!m_nonInteractiveLaunch && !m_startupChecksScheduled) {
        m_startupChecksScheduled = true;
        // Start binary discovery after the native window has been shown and
        // the event loop has had time to paint the real UI.
        QTimer::singleShot(0, this, [this]() {
            QTimer::singleShot(350, this, &MainWindow::startStartupChecks);
        });
    }
}

void MainWindow::activateCoordinatorUi()
{
    if (!m_nonInteractiveLaunch) {
        CoordinatorWindowActivation::activate(*this);
        return;
    }

    // Keep the existing queue and workers; this process has simply gained an
    // interactive surface after starting as an automation coordinator.
    m_nonInteractiveLaunch = false;
    preloadSecondaryTabs();
    if (auto *historyTab = findChild<DownloadHistoryTab *>(QStringLiteral("downloadHistoryTab"))) {
        historyTab->setRenderingEnabled(true);
    }
    CoordinatorWindowActivation::activate(*this);
    qInfo() << "[MainWindow] Coordinator UI show requested."
            << "exit_after=" << m_configManager->get(QStringLiteral("General"), QStringLiteral("exit_after"), false).toBool()
            << "visible=" << isVisible() << "active=" << isActiveWindow();
}

void MainWindow::ensureCoordinatorApi()
{
    if (m_localApiServer) {
        m_localApiServer->start();
    }
}

void MainWindow::enqueueCoordinatorDownload(const QString &url, const QString &type)
{
    QVariantMap options;
    options.insert(QStringLiteral("type"), type);
    MainWindowHelpers::applyNonInteractiveDownloadDefaults(options);
    onDownloadRequested(url, options);
}
