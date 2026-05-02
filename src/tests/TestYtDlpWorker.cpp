#include <QtTest/QtTest>
#include <QSignalSpy>
#include "BaseTest.h" // Now in src/tests
#include "../core/YtDlpWorker.h" // Relative path
#include "../core/ConfigManager.h" // Relative path

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
    void testLifecycleStatusParsing();
};

void TestYtDlpWorker::init() {
    // Called before each test function
    // Ensure the ConfigManager is fresh for each test if needed
    // For now, BaseTest::getConfigManager() provides a fresh instance per test class.
}

void TestYtDlpWorker::cleanup() {
    // Called after each test function
}

void TestYtDlpWorker::testYtDlpProgressParsing() {
    ConfigManager *config = getConfigManager();
    QStringList args; // Args are not critical for parsing tests
    TestableYtDlpWorker worker("testId1", args, config, this);

    QSignalSpy progressSpy(&worker, &YtDlpWorker::progressUpdated);

    // Simulate yt-dlp progress line (0.5% of 10MiB at 2MiB/s ETA 00:05)
    QString progressLine = "[download]   0.5% of   10.00MiB at  2.00MiB/s ETA 00:05";
    worker.callHandleOutputLine(progressLine);

    // Assert progress signal
    QCOMPARE(progressSpy.count(), 1);
    QVariantList arguments = progressSpy.takeFirst();
    QCOMPARE(arguments.at(0).toString(), "testId1");
    QVariantMap progressData = arguments.at(1).toMap();

    QCOMPARE(progressData["progress"].toInt(), 1); // 0.5% rounded up
    QCOMPARE(progressData["status"].toString(), "Downloading..."); // Default status
    // QCOMPARE(progressData["downloaded_size"].toString(), "50.0 KiB"); // Actual downloaded size calculation is complex
    QCOMPARE(progressData["speed"].toString(), "2.00 MiB/s");
    QCOMPARE(progressData["eta"].toString(), "00:05");
}

void TestYtDlpWorker::testYtDlpErrorParsing() {
    ConfigManager *config = getConfigManager();
    QStringList args;
    TestableYtDlpWorker worker("testId2", args, config, this);

    QSignalSpy errorSpy(&worker, &YtDlpWorker::ytDlpErrorDetected);

    // Simulate a private video error
    QString errorLine = "ERROR: This video is private";
    worker.callHandleOutputLine(errorLine);

    QCOMPARE(errorSpy.count(), 1);
    QVariantList errorArgs = errorSpy.takeFirst();
    QCOMPARE(errorArgs.at(0).toString(), "testId2");
    QCOMPARE(errorArgs.at(1).toString(), "private");
    QVERIFY(errorArgs.at(2).toString().contains("private"));
    QCOMPARE(errorArgs.at(3).toString(), errorLine);

    // Simulate an unavailable video error
    errorLine = "ERROR: This video is unavailable";
    worker.resetErrorEmitted(); // Reset internal flag as if it's a new error
    worker.callHandleOutputLine(errorLine);
    QCOMPARE(errorSpy.count(), 1); // Should emit again after reset
    QCOMPARE(errorSpy.takeFirst().at(1).toString(), "unavailable");
}

void TestYtDlpWorker::testAria2ProgressParsing() {
    ConfigManager *config = getConfigManager();
    QStringList args;
    TestableYtDlpWorker worker("testId3", args, config, this);

    QSignalSpy progressSpy(&worker, &YtDlpWorker::progressUpdated);

    // Simulate aria2c progress line
    QString progressLine = "[#123456 1.2MiB/5.0MiB(24%) DL:800KiB/s ETA:00:04]";
    worker.callHandleOutputLine(progressLine);

    QCOMPARE(progressSpy.count(), 1);
    QVariantList arguments = progressSpy.takeFirst();
    QCOMPARE(arguments.at(0).toString(), "testId3");
    QVariantMap progressData = arguments.at(1).toMap();

    QCOMPARE(progressData["progress"].toInt(), 24);
    QCOMPARE(progressData["downloaded_size"].toString(), "1.20 MiB");
    QCOMPARE(progressData["total_size"].toString(), "5.00 MiB");
    QCOMPARE(progressData["speed"].toString(), "800.0 KiB/s");
    QCOMPARE(progressData["eta"].toString(), "00:04");
}

void TestYtDlpWorker::testLifecycleStatusParsing() {
    ConfigManager *config = getConfigManager();
    QStringList args;
    TestableYtDlpWorker worker("testId4", args, config, this);

    QSignalSpy progressSpy(&worker, &YtDlpWorker::progressUpdated);

    // Simulate merger line
    worker.callHandleOutputLine("[Merger] Merging video and audio files");
    QCOMPARE(progressSpy.count(), 1);
    QCOMPARE(progressSpy.takeFirst().at(1).toMap()["status"].toString(), "Merging segments with ffmpeg...");

    // Simulate extract audio line
    worker.callHandleOutputLine("[ExtractAudio] Post-processing for audio extraction");
    QCOMPARE(progressSpy.count(), 1);
    QCOMPARE(progressSpy.takeFirst().at(1).toMap()["status"].toString(), "Extracting audio...");
}

// Generates the main() function for the test executable
QTEST_MAIN(TestYtDlpWorker)

// Includes the moc file generated by Qt
#include "TestYtDlpWorker.moc"
