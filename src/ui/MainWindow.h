#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QLabel>
#include <QCloseEvent>
#include <QVariant>
#include <QStringList>
#include <QThread> // Include QThread
#include <QClipboard> // Include QClipboard

// Forward declarations
class QEvent;
class ConfigManager;
class ArchiveManager;
class DownloadManager;
class AppUpdater;
class UrlValidator;
class StartupWorker;
class ActiveDownloadsTab;
class AdvancedSettingsTab;
class StartTab;
class SortingTab;
class ExtractorJsonParser;
class YtDlpDownloadInfoExtractor;
class MainWindowUiBuilder;
class LocalApiServer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(ExtractorJsonParser *extractorJsonParser, QWidget *parent = nullptr);
    ~MainWindow();

    QString appVersion() const;

protected:
    void closeEvent(QCloseEvent *event) override;
    bool event(QEvent *event) override;

private slots:
    void onDownloadRequested(const QString &url, const QVariantMap &options);
    void onValidationFinished(bool isValid, const QString &error);
    void onQueueFinished();
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void onVideoQualityWarning(const QString &url, const QString &message);
    void applyTheme(const QString &themeName);
    void updateTotalSpeed(double speed);
    void onDownloadStatsUpdated(int queued, int active, int completed, int errors);
    void setYtDlpVersion(const QString &version);
    void onLocalApiEnqueueRequested(const QString &url, const QString &type, const QString &jobId);
    void onClipboardChanged(); // New slot for clipboard changes
    void onRuntimeInfoReady(const QVariantMap &info);
    void onRuntimeInfoError(const QString &error);
    void onDownloadSectionsRequested(const QString &url, const QVariantMap &options, const QVariantMap &infoJson);
    void onYtDlpErrorPopup(const QString &id, const QString &errorType, const QString &userMessage, const QString &rawError, const QVariantMap &itemData);

private:
    void setupUI();
    void setupTrayIcon();
    void checkBinaries();
    void startStartupChecks();
    void setupLocalApiServer();
    void setupWindowsDebugConsole();
    void connectAppUpdaterSignals();
    void scheduleInitialSetup();
    void connectDownloadManagerSignals();
    void connectDiscordWebhookSignals();
    void connectStartupWorkerSignals();
    void queueDirectCliDownload();
    void handleClipboardAutoPaste(bool forceEnqueue = false); // Modified to accept forceEnqueue
    bool showMissingBinariesDialog(const QStringList &missingBinaries);

    ConfigManager *m_configManager;
    ArchiveManager *m_archiveManager;
    DownloadManager *m_downloadManager;
    AppUpdater *m_appUpdater;
    UrlValidator *m_urlValidator;
    StartupWorker *m_startupWorker;
    QThread *m_startupThread; // New thread for the startup worker
    ExtractorJsonParser *m_extractorJsonParser;
    YtDlpDownloadInfoExtractor *m_runtimeExtractor;
    MainWindowUiBuilder *m_uiBuilder;
    LocalApiServer *m_localApiServer;
    QClipboard *m_clipboard; // New QClipboard member

    QTabWidget *m_tabWidget;
    ActiveDownloadsTab *m_activeDownloadsTab;
    AdvancedSettingsTab *m_advancedSettingsTab;
    StartTab *m_startTab;
    QLabel *m_speedLabel;
    QLabel *m_queuedDownloadsLabel;
    QLabel *m_activeDownloadsLabel;
    QLabel *m_completedDownloadsLabel;
    QLabel *m_errorDownloadsLabel;

    QSystemTrayIcon *m_trayIcon;
    QMenu *m_trayMenu;

    QString m_pendingUrl;
    QVariantMap m_pendingOptions;
    bool m_silentUpdateCheck;
    bool m_nonInteractiveLaunch;
    QString m_lastAutoPastedUrl; // Track last auto-pasted URL to prevent duplicates
    qint64 m_lastAutoPasteTimestamp; // Timestamp of last auto-paste to enforce cooldown
};

#endif // MAINWINDOW_H
