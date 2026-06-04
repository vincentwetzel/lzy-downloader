#include "BaseTest.h"
#include "core/YtDlpWorker.h"
#include "core/ConfigManager.h"

#include <QtTest/QtTest>
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
    void testYtDlpProgressStalledParsing();
    void testLifecycleStatusParsing();
    void testLivestreamWaitParsing();
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

// Generates the main() function for the test executable
QTEST_MAIN(TestYtDlpWorker)

// Includes the moc file generated by Qt
#include "TestYtDlpWorker.moc"
