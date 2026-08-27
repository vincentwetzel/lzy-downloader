#pragma once

#include <QObject>
#include <QVariantMap>
#include <QStringList>
#include <QProcess>
#include <QElapsedTimer>

class ConfigManager;
class QTimer;

class YtDlpWorker : public QObject {
    Q_OBJECT

public:
    explicit YtDlpWorker(const QString &id, const QStringList &args, ConfigManager *configManager, QObject *parent = nullptr);
    [[nodiscard]] QString getId() const { return m_id; }
    void start();
    void killProcess();
    void finishGracefully();

signals:
    void progressUpdated(const QString &id, const QVariantMap &progressData);
    void finished(const QString &id, bool success, const QString &message, const QString &finalFilename, const QString &originalDownloadedFilename, const QVariantMap &videoMetadata);
    void outputReceived(const QString &id, const QString &output);
    void ytDlpErrorDetected(const QString &id, const QString &errorType, const QString &userMessage, const QString &rawError);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void readInfoJsonWithRetry(); // New slot for retry mechanism
    void pollTransferProgress();

protected: // Changed from private for testing
    void parseProcessBuffer(QByteArray &buffer, const QByteArray &newData);
    void parseStandardOutput(const QByteArray &output);
    void parseStandardError(const QByteArray &output);
    void handleOutputLine(const QString &line);
    bool parseYtDlpProgressLine(const QString &line);
    bool parseAria2ProgressLine(const QString &line);
    double parseSizeStringToBytes(const QString &sizeString);
    QString formatBytes(double bytes);
    QString normalizeConsoleLine(const QString &line) const;
    void updateTransferTarget(const QString &path);
    bool isAuxiliaryTransferTarget(const QString &path) const;
    QString statusForCurrentTransfer() const;
    void emitStatusUpdate(const QString &status, int progress = -2);
    bool handleLifecycleStatusLine(const QString &line);
    void inferRequestedTransfersFromFormatList(const QString &formatList);
    bool handleAria2CommandLine(const QString &line);
    int inferPrimaryStreamIndexFromPath(const QString &path) const;
    int inferPrimaryStreamIndexFromTotalBytes(double totalBytes) const;
    bool requestedAudioExtraction() const;
    bool isBrowserCookieFailureLine(const QString &line) const;
    bool hasBrowserCookieFailureDiagnostic() const;
    /** Removes browser cookie flags and their values from the pending yt-dlp arguments. */
    void removeBrowserCookieArguments();
    /** Returns whether one output line proves that the media transfer is unusable. */
    bool isIncompleteMediaDiagnosticLine(const QString &line) const;
    /** Checks retained worker diagnostics before allowing a non-zero exit to finalize. */
    bool hasFatalDownloadDiagnostic() const;
    /** Checks whether retained diagnostics specifically identify incomplete media. */
    bool hasIncompleteMediaDiagnostic() const;
    bool retryWithoutBrowserCookiesIfCookieExtractionFailed();
    /**
     * Retries a recoverable aria2-backed transfer once through yt-dlp's native downloader.
     *
     * This is limited to documented transient aria2 exit codes and the narrow case where
     * aria2 reports success but yt-dlp cannot find the required media .part output.
     */
    bool retryWithoutAria2cIfTransientFailure(const QString &diagnostic);
    /** Removes only metadata sidecars that can collide with yt-dlp's atomic JSON write on retry. */
    void cleanupMetadataSidecarsForRetry();
    QString inferPrimaryStreamStatusFromPath(const QString &path) const;
    QString inferPrimaryStreamStatusFromMetadata(int index) const;
    void updateInferredTransferStage(double percentage, double downloadedBytes, double totalBytes);
    double inferPrimaryStreamSizeBytes(const QVariantMap &requestMap) const;
    double inferPrimaryStreamSizeFromMetadata(const QString &formatId) const;
    void applyOverallPrimaryProgress(QVariantMap &progressData, double percentage, double downloadedBytes, double totalBytes);

    QString m_id;
    QStringList m_args;
    ConfigManager *m_configManager;
    QProcess *m_process;
    QString m_finalFilename;
    QString m_originalDownloadedFilename;
    QString m_videoTitle; // Added to store the video title
    QString m_thumbnailPath;
    bool m_finishEmitted;
    QByteArray m_outputBuffer;
    QByteArray m_errorBuffer; // New member for stderr buffering
    QStringList m_allOutputLines; // Stores all output lines for post-processing

    QString m_infoJsonPath; // Path to info.json file that needs to be read (Corrected from m_pendingInfoJsonPath)
    int m_infoJsonRetryCount;      // Retry counter for reading info.json
    QVariantMap m_fullMetadata;    // Stores the full metadata parsed from info.json
    bool m_errorEmitted;           // Tracks if a specific error has been emitted
    QStringList m_errorLines;      // Stores ERROR: lines from stderr
    bool m_promptDelayActive{false};
    bool m_retriedWithoutBrowserCookies = false;
    bool m_retriedWithoutAria2c = false;
    QString m_recoveryDiagnostic;
    QStringList m_requestedTransferStatuses;
    QStringList m_requestedTransferFormatIds;
    QList<double> m_requestedTransferSizes;
    QString m_currentTransferTarget;
    QString m_currentTransferStatus;
    bool m_currentTransferIsAuxiliary = false;
    int m_inferredTransferIndex = -1;
    double m_lastPrimaryProgress = -1.0;
    double m_lastPrimaryTotalBytes = 0.0;
    QTimer *m_progressPollTimer = nullptr;
    qint64 m_lastPolledTransferBytes = -1;
    double m_lastPolledProgress = -1.0;
    QElapsedTimer m_fileProgressClock;

    static constexpr int RECOVERY_RETRY_DELAY_MS = 1000;
};
