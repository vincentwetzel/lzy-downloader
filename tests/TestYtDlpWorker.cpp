#include "BaseTest.h"
#include "core/YtDlpWorker.h"
#include "core/ConfigManager.h"
#include "core/DiagnosticTail.h"

#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QSignalSpy>

// Define a testable YtDlpWorker that allows direct access to protected parsing methods
class TestableYtDlpWorker : public YtDlpWorker {
    Q_OBJECT
public:
    explicit TestableYtDlpWorker(const QString &id, const QStringList &args, ConfigManager *configManager, QObject *parent = nullptr)
        : YtDlpWorker(id, args, configManager, parent) {}

    // Expose protected methods for testing
    void callParseStandardOutput(const QByteArray &output) { parseStandardOutput(output); }
    void callParseStandardError(const QByteArray &output) { parseStandardError(output); }
    void callHandleOutputLine(const QString &line) { handleOutputLine(line); }
    bool callRetryWithoutAria2c(const QString &diagnostic) { return retryWithoutAria2cIfTransientFailure(diagnostic); }
    double callInferPrimaryStreamSizeFromMetadata(const QString &formatId) const { return inferPrimaryStreamSizeFromMetadata(formatId); }
    QVariantMap callApplyOverallPrimaryProgress(double percentage, double downloadedBytes, double totalBytes) {
        QVariantMap progressData;
        applyOverallPrimaryProgress(progressData, percentage, downloadedBytes, totalBytes);
        return progressData;
    }
    void setPrimaryTransferState(const QList<double> &sizes, int index) {
        m_requestedTransferSizes = sizes;
        m_inferredTransferIndex = index;
        m_currentTransferIsAuxiliary = false;
    }
    void setFullMetadata(const QVariantMap &metadata) { m_fullMetadata = metadata; }
    bool callIsIncompleteMediaDiagnosticLine(const QString &line) const { return isIncompleteMediaDiagnosticLine(line); }
    bool callHasFatalDownloadDiagnostic() const { return hasFatalDownloadDiagnostic(); }
    bool callHasIncompleteMediaDiagnostic() const { return hasIncompleteMediaDiagnostic(); }
    QStringList arguments() const { return m_args; }

    // Expose internal state for assertions
    bool errorEmitted() const { return m_errorEmitted; }
    void resetErrorEmitted() { m_errorEmitted = false; }
};

class TestYtDlpWorker : public BaseTest {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testYtDlpProgressParsing();
    void testYtDlpErrorParsing();
    void testAria2ProgressParsing();
    void testAria2AdvancedProgressParsing();
    void testAudioExtractionUsesAudioTransferStatusForCombinedSource();
    void testTransientAria2FailureFallsBackToNativeDownloader();
    void testMissingAria2OutputFallsBackToNativeDownloader();
    void testAria2RecoveryRejectsUnrelatedFailuresAndRetriesOnce();
    void testMetadataFormatSizeFallback();
    void testOverallProgressPayloadForDiscord();
    void testYtDlpProgressStalledParsing();
    void testLifecycleStatusParsing();
    void testLivestreamWaitParsing();
    void testIncompleteMediaDiagnosticClassification();
    void testDiskSpaceFailureClassification();
    void testDiagnosticTailRetainsBoundedNewestLines();
};

void TestYtDlpWorker::init() {
    // Called before each test function
    BaseTest::init();
}

void TestYtDlpWorker::cleanup() {
    // Called after each test function
    BaseTest::cleanup();
}

void TestYtDlpWorker::testYtDlpProgressParsing() {
    ConfigManager *config = getConfigManager();
    QStringList args; // Args are not critical for parsing tests
    TestableYtDlpWorker worker(QStringLiteral("testId1"), args, config, nullptr);

    QSignalSpy progressSpy(&worker, &YtDlpWorker::progressUpdated);

    // Simulate yt-dlp progress line (0.5% of 10MiB at 2MiB/s ETA 00:05)
    QString progressLine = QStringLiteral("[download]   0.5% of   10.00MiB at  2.00MiB/s ETA 00:05");
    worker.callHandleOutputLine(progressLine);

    // Assert progress signal
    QCOMPARE(progressSpy.count(), 1);
    QVariantList arguments = progressSpy.takeFirst();
    QCOMPARE(arguments.at(0).toString(), QStringLiteral("testId1"));
    QVariantMap progressData = arguments.at(1).toMap();

    QCOMPARE(progressData[QStringLiteral("progress")].toInt(), 1); // 0.5% rounded up
    QCOMPARE(progressData[QStringLiteral("status")].toString(), QStringLiteral("Downloading...")); // Default status
    QCOMPARE(progressData[QStringLiteral("speed")].toString(), QStringLiteral("2.00 MiB/s"));
    QCOMPARE(progressData[QStringLiteral("eta")].toString(), QStringLiteral("00:05"));
}

void TestYtDlpWorker::testYtDlpErrorParsing() {
    ConfigManager *config = getConfigManager();
    QStringList args;
    TestableYtDlpWorker worker(QStringLiteral("testId2"), args, config, nullptr);

    QSignalSpy errorSpy(&worker, &YtDlpWorker::ytDlpErrorDetected);

    // Simulate a private video error
    QString errorLine = QStringLiteral("ERROR: This video is private");
    worker.callHandleOutputLine(errorLine);

    QCOMPARE(errorSpy.count(), 1);
    QVariantList errorArgs = errorSpy.takeFirst();
    QCOMPARE(errorArgs.at(0).toString(), QStringLiteral("testId2"));
    QCOMPARE(errorArgs.at(1).toString(), QStringLiteral("private"));
    QVERIFY(errorArgs.at(2).toString().contains(QStringLiteral("private")));
    QCOMPARE(errorArgs.at(3).toString(), errorLine);

    // Simulate an unavailable video error
    errorLine = QStringLiteral("ERROR: This video is unavailable");
    worker.resetErrorEmitted(); // Reset internal flag as if it's a new error
    worker.callHandleOutputLine(errorLine);
    QCOMPARE(errorSpy.count(), 1); // Should emit again after reset
    QCOMPARE(errorSpy.takeFirst().at(1).toString(), QStringLiteral("unavailable"));

    // Simulate a scheduled livestream error
    errorLine = QStringLiteral("ERROR: Premieres in 2 hours");
    worker.resetErrorEmitted(); // Reset internal flag as if it's a new error
    worker.callHandleOutputLine(errorLine);
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(errorSpy.takeFirst().at(1).toString(), QStringLiteral("scheduled_livestream"));

    // Ordinary title text must not open the scheduled-livestream dialog.
    worker.resetErrorEmitted();
    worker.callHandleOutputLine(QStringLiteral("[youtube] Title: Starting in the morning"));
    QVERIFY(errorSpy.isEmpty());

    // Ordinary metadata text containing "locked" must not trigger the old
    // browser-cookie retry false positive.
    TestableYtDlpWorker cookieWorker(QStringLiteral("cookieFalsePositive"),
                                     {QStringLiteral("--cookies-from-browser"), QStringLiteral("firefox")},
                                     config, nullptr);
    QSignalSpy cookieProgressSpy(&cookieWorker, &YtDlpWorker::progressUpdated);
    cookieWorker.callHandleOutputLine(QStringLiteral("Follow Crooked on Instagram - locked in a heated race"));
    for (const QList<QVariant> &arguments : cookieProgressSpy) {
        QVERIFY(!arguments.at(1).toMap().value(QStringLiteral("status")).toString().contains(QStringLiteral("Browser cookies")));
    }
}

void TestYtDlpWorker::testMetadataFormatSizeFallback() {
    ConfigManager *config = getConfigManager();
    TestableYtDlpWorker worker(QStringLiteral("metadataSizeFallback"), {}, config, nullptr);
    worker.setFullMetadata({
        {QStringLiteral("formats"), QVariantList{
            QVariantMap{{QStringLiteral("format_id"), QStringLiteral("299")},
                        {QStringLiteral("filesize"), 6546721601.0}},
            QVariantMap{{QStringLiteral("format_id"), QStringLiteral("140")},
                        {QStringLiteral("filesize"), 258401410.0}}
        }}
    });

    QCOMPARE(worker.callInferPrimaryStreamSizeFromMetadata(QStringLiteral("299")), 6546721601.0);
    QCOMPARE(worker.callInferPrimaryStreamSizeFromMetadata(QStringLiteral("140")), 258401410.0);
    QCOMPARE(worker.callInferPrimaryStreamSizeFromMetadata(QStringLiteral("missing")), 0.0);
}

void TestYtDlpWorker::testAria2ProgressParsing() {
    ConfigManager *config = getConfigManager();
    QStringList args;
    TestableYtDlpWorker worker(QStringLiteral("testId3"), args, config, nullptr);

    QSignalSpy progressSpy(&worker, &YtDlpWorker::progressUpdated);

    // Simulate aria2c progress line
    QString progressLine = QStringLiteral("[#123456 1.2MiB/5.0MiB(24%) DL:800KiB/s ETA:00:04]");
    worker.callHandleOutputLine(progressLine);

    QCOMPARE(progressSpy.count(), 1);
    QVariantList arguments = progressSpy.takeFirst();
    QCOMPARE(arguments.at(0).toString(), QStringLiteral("testId3"));
    QVariantMap progressData = arguments.at(1).toMap();

    QCOMPARE(progressData[QStringLiteral("progress")].toInt(), 24);
    QCOMPARE(progressData[QStringLiteral("downloaded_size")].toString(), QStringLiteral("1.20 MiB"));
    QCOMPARE(progressData[QStringLiteral("total_size")].toString(), QStringLiteral("5.00 MiB"));
    QCOMPARE(progressData[QStringLiteral("speed")].toString(), QStringLiteral("800.0 KiB/s"));
    QCOMPARE(progressData[QStringLiteral("eta")].toString(), QStringLiteral("00:04"));
}

void TestYtDlpWorker::testLifecycleStatusParsing() {
    ConfigManager *config = getConfigManager();
    QStringList args;
    TestableYtDlpWorker worker(QStringLiteral("testId4"), args, config, nullptr);

    QSignalSpy progressSpy(&worker, &YtDlpWorker::progressUpdated);

    // Simulate merger line
    worker.callHandleOutputLine(QStringLiteral("[Merger] Merging video and audio files"));
    QCOMPARE(progressSpy.count(), 1);
    QCOMPARE(progressSpy.takeFirst().at(1).toMap()[QStringLiteral("status")].toString(), QStringLiteral("Merging segments with ffmpeg..."));

    // Simulate extract audio line
    worker.callHandleOutputLine(QStringLiteral("[ExtractAudio] Post-processing for audio extraction"));
    QCOMPARE(progressSpy.count(), 1);
    QCOMPARE(progressSpy.takeFirst().at(1).toMap()[QStringLiteral("status")].toString(), QStringLiteral("Extracting audio..."));
}

void TestYtDlpWorker::testAria2AdvancedProgressParsing() {
    ConfigManager *config = getConfigManager();
    TestableYtDlpWorker worker(QStringLiteral("testId5"), {}, config, nullptr);
    QSignalSpy progressSpy(&worker, &YtDlpWorker::progressUpdated);

    // Test aria2 progress with higher percentage and different speed/ETA
    worker.callHandleOutputLine(QStringLiteral("[#987654 4.8MiB/5.0MiB(96%) DL:2.5MiB/s ETA:00:01]"));
    QCOMPARE(progressSpy.count(), 1);
    QVariantMap progressData = progressSpy.takeFirst().at(1).toMap();
    QCOMPARE(progressData[QStringLiteral("progress")].toInt(), 96);
    QCOMPARE(progressData[QStringLiteral("speed")].toString(), QStringLiteral("2.50 MiB/s"));
}

void TestYtDlpWorker::testAudioExtractionUsesAudioTransferStatusForCombinedSource() {
    ConfigManager *config = getConfigManager();
    TestableYtDlpWorker worker(QStringLiteral("audioCombinedSource"),
                               {QStringLiteral("-x")}, config, nullptr);
    QSignalSpy progressSpy(&worker, &YtDlpWorker::progressUpdated);

    worker.callHandleOutputLine(QStringLiteral(
        "[debug] aria2c command line: aria2c --out \"audio.f18.mp4\" "
        "\"https://media.example.test/?itag=18&mime=video%2Fmp4\""));
    worker.callHandleOutputLine(QStringLiteral("[#123456 1.0MiB/2.0MiB(50%) DL:1.0MiB/s ETA:00:01]"));

    QVERIFY(!progressSpy.isEmpty());
    const QVariantMap progressData = progressSpy.last().at(1).toMap();
    QCOMPARE(progressData.value(QStringLiteral("status")).toString(),
             QStringLiteral("Downloading audio stream..."));
}

void TestYtDlpWorker::testTransientAria2FailureFallsBackToNativeDownloader() {
    ConfigManager *config = getConfigManager();
    const QString tempRoot = QDir(getTempDir()).filePath(QStringLiteral("temp_downloads"));
    const QString id = QStringLiteral("44444444-4444-4444-8444-444444444444");
    const QString uuidDirectory = QDir(tempRoot).filePath(id);
    QVERIFY(QDir().mkpath(uuidDirectory));

    QFile staleInfo(QDir(uuidDirectory).filePath(QStringLiteral("video.info.json")));
    QVERIFY(staleInfo.open(QIODevice::WriteOnly));
    staleInfo.write("{}");
    staleInfo.close();

    config->set(QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory"), tempRoot);
    TestableYtDlpWorker worker(id,
                               {QStringLiteral("--external-downloader"), QStringLiteral("aria2c"),
                                QStringLiteral("--external-downloader-args"), QStringLiteral("aria2c:--max-tries=6")},
                               config, nullptr);
    QSignalSpy progressSpy(&worker, &YtDlpWorker::progressUpdated);

    QVERIFY(worker.callRetryWithoutAria2c(QStringLiteral("ERROR: aria2c exited with code 29")));
    QVERIFY(!worker.arguments().contains(QStringLiteral("--external-downloader")));
    QVERIFY(!worker.arguments().contains(QStringLiteral("--external-downloader-args")));
    QVERIFY(!QFile::exists(QDir(uuidDirectory).filePath(QStringLiteral("video.info.json"))));
    QVERIFY(!progressSpy.isEmpty());
    QVERIFY(progressSpy.last().at(1).toMap().value(QStringLiteral("status")).toString().contains(QStringLiteral("native downloader")));
}

void TestYtDlpWorker::testMissingAria2OutputFallsBackToNativeDownloader() {
    const QString id = QStringLiteral("55555555-5555-4555-8555-555555555555");
    ConfigManager *config = getConfigManager();
    TestableYtDlpWorker worker(id,
                               {QStringLiteral("--external-downloader"), QStringLiteral("aria2c"),
                                QStringLiteral("--external-downloader-args"), QStringLiteral("aria2c:--max-tries=6")},
                               config, nullptr);
    QSignalSpy progressSpy(&worker, &YtDlpWorker::progressUpdated);

    const QString diagnostic = QStringLiteral(
        "ERROR: Unable to download video: [WinError 3] The system cannot find the path specified: "
        "'C:/temp/video.f299.mp4.part'\n"
        "FileNotFoundError: [WinError 3] The system cannot find the path specified: "
        "'C:/temp/video.f299.mp4.part'");
    QVERIFY(worker.callRetryWithoutAria2c(diagnostic));
    QVERIFY(!worker.arguments().contains(QStringLiteral("--external-downloader")));
    QVERIFY(!worker.arguments().contains(QStringLiteral("--external-downloader-args")));
    QVERIFY(!progressSpy.isEmpty());
    QVERIFY(progressSpy.last().at(1).toMap().value(QStringLiteral("status")).toString().contains(QStringLiteral("expected media output")));
}

void TestYtDlpWorker::testAria2RecoveryRejectsUnrelatedFailuresAndRetriesOnce() {
    ConfigManager *config = getConfigManager();
    TestableYtDlpWorker worker(QStringLiteral("aria2RecoveryBoundaries"),
                               {QStringLiteral("--external-downloader"), QStringLiteral("aria2c"),
                                QStringLiteral("--external-downloader-args"), QStringLiteral("aria2c:--max-tries=6")},
                               config, nullptr);

    QVERIFY(!worker.callRetryWithoutAria2c(QStringLiteral("ERROR: aria2c exited with code 1")));
    QVERIFY(!worker.callRetryWithoutAria2c(QStringLiteral("ERROR: network connection failed")));
    QVERIFY(worker.callRetryWithoutAria2c(QStringLiteral("ERROR: aria2c exited with code 6")));
    QVERIFY(!worker.arguments().contains(QStringLiteral("--external-downloader")));
    QVERIFY(!worker.callRetryWithoutAria2c(QStringLiteral("ERROR: aria2c exited with code 29")));
}

void TestYtDlpWorker::testDiagnosticTailRetainsBoundedNewestLines() {
    DiagnosticTail tail(3, 100);
    tail.append(QStringLiteral("first"));
    tail.append(QStringLiteral("second"));
    tail.append(QStringLiteral("third"));
    tail.append(QStringLiteral("fourth"));

    QCOMPARE(tail.lines(), QStringList({QStringLiteral("second"), QStringLiteral("third"), QStringLiteral("fourth")}));

    DiagnosticTail characterCappedTail(10, 10);
    characterCappedTail.append(QStringLiteral("12345"));
    characterCappedTail.append(QStringLiteral("67890"));
    characterCappedTail.append(QStringLiteral("newest"));
    QCOMPARE(characterCappedTail.lines(), QStringList({QStringLiteral("newest")}));

    DiagnosticTail textTail(3, 100);
    textTail.appendText(QStringLiteral("one\ntwo\nthree\nfour"));
    QCOMPARE(textTail.lines(), QStringList({QStringLiteral("two"), QStringLiteral("three"), QStringLiteral("four")}));
}

void TestYtDlpWorker::testOverallProgressPayloadForDiscord() {
    TestableYtDlpWorker worker(QStringLiteral("overallProgress"), {}, getConfigManager(), nullptr);
    worker.setPrimaryTransferState({800.0, 200.0}, 0);

    QVariantMap progressData = worker.callApplyOverallPrimaryProgress(50.0, 400.0, 800.0);
    QCOMPARE(progressData.value(QStringLiteral("overall_progress")).toDouble(), 40.0);

    worker.setPrimaryTransferState({800.0, 200.0}, 1);
    progressData = worker.callApplyOverallPrimaryProgress(50.0, 100.0, 200.0);
    QCOMPARE(progressData.value(QStringLiteral("overall_progress")).toDouble(), 90.0);
}

void TestYtDlpWorker::testYtDlpProgressStalledParsing() {
    ConfigManager *config = getConfigManager();
    TestableYtDlpWorker worker(QStringLiteral("testId6"), {}, config, nullptr);
    QSignalSpy progressSpy(&worker, &YtDlpWorker::progressUpdated);

    // Simulate yt-dlp stalled/unknown progress
    worker.callHandleOutputLine(QStringLiteral("[download] Destination: video.mp4"));
    worker.callHandleOutputLine(QStringLiteral("[download]   0.0% of unknown size at  0.00MiB/s ETA --:--"));
    QCOMPARE(progressSpy.count(), 2);
    QVariantMap progressData = progressSpy.last().at(1).toMap();
    QCOMPARE(progressData[QStringLiteral("progress")].toInt(), 0);
    QCOMPARE(progressData[QStringLiteral("status")].toString(), QStringLiteral("Downloading video stream..."));
}

void TestYtDlpWorker::testLivestreamWaitParsing() {
    ConfigManager *config = getConfigManager();
    TestableYtDlpWorker worker(QStringLiteral("testLiveId"), {}, config, nullptr);
    QSignalSpy progressSpy(&worker, &YtDlpWorker::progressUpdated);

    // Test generic wait for video
    worker.callHandleOutputLine(QStringLiteral("[wait] Waiting for 01:00:00 - Press Ctrl+C to try now"));
    QCOMPARE(progressSpy.count(), 1);
    QVariantMap progressData = progressSpy.takeFirst().at(1).toMap();
    QCOMPARE(progressData[QStringLiteral("progress")].toInt(), -1);
    QCOMPARE(progressData[QStringLiteral("status")].toString(), QStringLiteral("Waiting for livestream to start..."));

    // Test precise wait remaining time countdown
    worker.callHandleOutputLine(QStringLiteral("[wait] Remaining time until next attempt: 00:04:45"));
    QCOMPARE(progressSpy.count(), 1);
    progressData = progressSpy.takeFirst().at(1).toMap();
    QCOMPARE(progressData[QStringLiteral("progress")].toInt(), -1);
    QCOMPARE(progressData[QStringLiteral("status")].toString(), QStringLiteral("Next check in 00:04:45"));

    // Test generic wait prefix
    worker.callHandleOutputLine(QStringLiteral("[wait] Some other wait reason"));
    QCOMPARE(progressSpy.count(), 1);
    progressData = progressSpy.takeFirst().at(1).toMap();
    QCOMPARE(progressData[QStringLiteral("progress")].toInt(), -1);
    QCOMPARE(progressData[QStringLiteral("status")].toString(), QStringLiteral("Waiting for livestream to start..."));
}

void TestYtDlpWorker::testIncompleteMediaDiagnosticClassification() {
    ConfigManager *config = getConfigManager();
    TestableYtDlpWorker worker(QStringLiteral("incompleteMedia"), {}, config, nullptr);
    QSignalSpy errorSpy(&worker, &YtDlpWorker::ytDlpErrorDetected);

    worker.callHandleOutputLine(QStringLiteral("ERROR: Did not get any data blocks"));
    worker.callHandleOutputLine(QStringLiteral("[download] fragment not found; Skipping fragment 826 ..."));
    worker.callHandleOutputLine(QStringLiteral("ERROR: Error opening input files: Invalid data found when processing input"));

    QVERIFY(worker.callIsIncompleteMediaDiagnosticLine(QStringLiteral("ERROR: Did not get any data blocks")));
    QVERIFY(worker.callIsIncompleteMediaDiagnosticLine(QStringLiteral("[download] fragment not found; Skipping fragment 826 ...")));
    QVERIFY(worker.callHasFatalDownloadDiagnostic());
    QVERIFY(worker.callHasIncompleteMediaDiagnostic());
    QCOMPARE(errorSpy.count(), 1);
    const QVariantList errorArgs = errorSpy.first();
    QCOMPARE(errorArgs.at(1).toString(), QStringLiteral("incomplete_media"));
    QVERIFY(errorArgs.at(2).toString().contains(QStringLiteral("incomplete"), Qt::CaseInsensitive));
}

void TestYtDlpWorker::testDiskSpaceFailureClassification() {
    ConfigManager *config = getConfigManager();
    TestableYtDlpWorker worker(QStringLiteral("diskSpaceFailure"), {}, config, nullptr);

    worker.callHandleOutputLine(QStringLiteral("WARNING: unable to embed using mutagen; [Errno 28] No space left on device"));
    worker.callHandleOutputLine(QStringLiteral("[out#0/mp4] Task finished with error code: -28 (No space left on device)"));

    QVERIFY(worker.callHasFatalDownloadDiagnostic());
}

// Generates the main() function for the test executable
QTEST_MAIN(TestYtDlpWorker)

// Includes the moc file generated by Qt
#include "TestYtDlpWorker.moc"
