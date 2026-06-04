#include "TestEndToEnd.h"
#include "core/ProcessUtils.h" // Added for ProcessUtils::terminateProcessTree and findBinary
#include <QSignalSpy>
#include <QFile>
#include <QDebug>
#include <QEventLoop>
#include <QProcess>
#include <QTcpSocket> // Added for server readiness check
#include <QUuid>
#include <QTimer>
#include <QtTest/QTest> // Added for QTest::qWait

namespace {
    constexpr quint16 TEST_SERVER_PORT = 8000;
    const QString TEST_DUMMY_FILE = QStringLiteral("test_video.webm");
    const QString TEST_SERVER_SCRIPT = QStringLiteral("test_server.py");
}

// Helper to setup ConfigManager for test (implementation)
void TestEndToEnd::setupTestConfig(ConfigManager *configManager, const QString &tempDir, const QString &ytDlpBinaryPath) {
    configManager->set(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory"), tempDir);
    // temporary_downloads_directory is automatically derived from completed_downloads_directory by ConfigManager
    configManager->set(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_path"), ytDlpBinaryPath); // Use provided path
    configManager->set(QStringLiteral("Binaries"), QStringLiteral("aria2c_path"), QStringLiteral("aria2c")); // Assuming aria2c is in PATH
    configManager->set(QStringLiteral("Binaries"), QStringLiteral("ffmpeg_path"), QStringLiteral("ffmpeg")); // Assuming ffmpeg is in PATH
    // Ensure aria2c is NOT used to simplify the test initially
    configManager->set(QStringLiteral("Metadata"), QStringLiteral("use_aria2c"), false);
    
    // Explicitly clear audio/video specific templates to force fallback to generic output_template
    configManager->set(QStringLiteral("General"), QStringLiteral("output_template_audio"), QStringLiteral(""));
    configManager->set(QStringLiteral("General"), QStringLiteral("output_template_video"), QStringLiteral(""));
    // Set the generic output template (relative to temp dir)
    configManager->set(QStringLiteral("General"), QStringLiteral("output_template"), QStringLiteral("%(id)s.%(ext)s"));
    configManager->set(QStringLiteral("Metadata"), QStringLiteral("embed_metadata"), false); // Disable metadata embedding to avoid ffmpeg processing the dummy file
}

void TestEndToEnd::init() {
    BaseTest::init();
    
    // 1. Create dummy video file
    const QString videoFilePath = QDir(getTempDir()).filePath(TEST_DUMMY_FILE);
    QFile videoFile(videoFilePath);
    if (videoFile.open(QIODevice::WriteOnly)) {
        constexpr char dummyContent[] = "dummy video content";
        videoFile.write(dummyContent, sizeof(dummyContent) - 1); // Small content for a small file size
        videoFile.close();
    } else {
        QFAIL(qPrintable(QStringLiteral("Failed to create dummy video file: %1").arg(videoFile.errorString())));
    }

    // 2. Start Python HTTP server
    QString sourcePythonScriptPath;
    const QStringList searchPaths = {
        QStringLiteral("../../src/tests/%1").arg(TEST_SERVER_SCRIPT), // MSBuild (build/Release/)
        QStringLiteral("../src/tests/%1").arg(TEST_SERVER_SCRIPT)     // Ninja/Makefiles (build/)
    };
    for (const QString &relPath : searchPaths) {
        QString absPath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(relPath);
        if (QFile::exists(absPath)) {
            sourcePythonScriptPath = absPath;
            break;
        }
    }
    if (sourcePythonScriptPath.isEmpty()) {
        QFAIL(qPrintable(QStringLiteral("%1 not found. Ensure it's in the correct test data path.").arg(TEST_SERVER_SCRIPT)));
    }
    const QString destPythonScriptPath = QDir(getTempDir()).filePath(TEST_SERVER_SCRIPT);
    if (!QFile::copy(sourcePythonScriptPath, destPythonScriptPath)) {
        QFAIL(qPrintable(QStringLiteral("Failed to copy %1 to temporary directory: %2").arg(TEST_SERVER_SCRIPT, destPythonScriptPath)));
    }


    m_httpServerProcess = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    m_httpServerProcess->setProcessEnvironment(env);
#ifdef Q_OS_WIN
    m_httpServerProcess->setProgram(QStringLiteral("python.exe"));
#else
    m_httpServerProcess->setProgram(QStringLiteral("python3")); // Fallback for Unix-like systems
#endif
    m_httpServerProcess->setArguments(QStringList() << destPythonScriptPath << QString::number(TEST_SERVER_PORT)); // Run the copied script
    m_httpServerProcess->setWorkingDirectory(getTempDir()); // Server serves from temp dir
    
    QObject::connect(m_httpServerProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        while (m_httpServerProcess->canReadLine()) {
            QString line = QString::fromUtf8(m_httpServerProcess->readLine()).trimmed();
            if (!line.isEmpty()) {
                qDebug() << "HTTP Server STDOUT:" << line;
            }
        }
    });
    QObject::connect(m_httpServerProcess, &QProcess::readyReadStandardError, this, [this]() {
        while (m_httpServerProcess->canReadLine()) {
            QString line = QString::fromUtf8(m_httpServerProcess->readLine()).trimmed();
            if (!line.isEmpty()) {
                qWarning() << "HTTP Server STDERR:" << line;
            }
        }
    });

    QObject::connect(m_httpServerProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error){
        QFAIL(qPrintable(QStringLiteral("HTTP Server process error: %1, %2").arg(error).arg(m_httpServerProcess->errorString())));
    });
    
    m_httpServerProcess->start();
    QVERIFY(m_httpServerProcess->waitForStarted(5000)); // Wait for process to start
    
    // Robust server readiness check: try to connect with a QTcpSocket
    QTcpSocket socket;
    qint64 startTime = QDateTime::currentMSecsSinceEpoch();
    while (socket.state() != QAbstractSocket::ConnectedState && QDateTime::currentMSecsSinceEpoch() - startTime < 10000) {
        socket.connectToHost(QStringLiteral("127.0.0.1"), TEST_SERVER_PORT); // 127.0.0.1 prevents ambiguous IPv6 loops
        if (socket.waitForConnected(200)) {
            break;
        }
        socket.abort(); // Reset socket state before retrying
    }
    
    if (socket.state() != QAbstractSocket::ConnectedState) {
        QFAIL("Timed out waiting for local HTTP server to become responsive.");
    }
    socket.disconnectFromHost();

    m_serverProcessId = m_httpServerProcess->processId(); // Store PID for cleanup
    qDebug() << "Started test HTTP server with PID:" << m_serverProcessId;

    QTest::qWait(1000); // Process events while waiting for the server to settle
}

void TestEndToEnd::cleanup() {
    if (m_httpServerProcess) {
        if (m_httpServerProcess->state() == QProcess::Running) {
            ProcessUtils::terminateProcessTree(m_httpServerProcess);
            m_httpServerProcess->waitForFinished(3000); // Ensure process has released file handles
            qDebug() << "Terminated test HTTP server process with PID:" << m_serverProcessId;
        }
        m_httpServerProcess->deleteLater();
        m_httpServerProcess = nullptr;
    }
    m_serverProcessId = 0; // Reset PID
    BaseTest::cleanup();
}

void TestEndToEnd::testSingleVideoDownload() {
    const QString testUrl = QStringLiteral("http://localhost:%1/%2").arg(TEST_SERVER_PORT).arg(TEST_DUMMY_FILE); // Local HTTP server URL
    
    // 1. Setup ConfigManager
    ConfigManager *configManager = getConfigManager();
    // Get actual yt-dlp binary path
    const QString ytDlpPath = ProcessUtils::findBinary(QStringLiteral("yt-dlp"), configManager).path;
    if (ytDlpPath.isEmpty()) {
        QFAIL("yt-dlp binary not found in system PATH. Cannot run end-to-end test.");
    }
    setupTestConfig(configManager, getTempDir(), ytDlpPath);
    configManager->save(); // Ensure settings are saved to disk

    // 2. Instantiate DownloadManager and its dependencies
    DownloadManager downloadManager(configManager, nullptr);

    // 3. Monitor download completion and id
    QSignalSpy downloadFinishedSpy(&downloadManager, &DownloadManager::downloadFinished);
    QVERIFY(downloadFinishedSpy.isValid());
    
    QSignalSpy downloadAddedSpy(&downloadManager, &DownloadManager::downloadAddedToQueue);
    QVERIFY(downloadAddedSpy.isValid());

    // 4. Enqueue download
    QVariantMap downloadOptionsMap;
    downloadOptionsMap[QStringLiteral("type")] = QStringLiteral("video");
    downloadOptionsMap[QStringLiteral("format")] = QStringLiteral("bestvideo[ext=webm]+bestaudio[ext=webm]/best[ext=webm]"); // Force webm for consistent filename
    
    downloadManager.enqueueDownload(testUrl, downloadOptionsMap);

    // 5. Wait for download to be added to the queue and get the id
    if (downloadAddedSpy.isEmpty()) {
        QVERIFY(downloadAddedSpy.wait(5000));
    }
    QList<QVariant> addedArguments = downloadAddedSpy.takeFirst();
    QString downloadId = addedArguments.at(0).toMap().value(QStringLiteral("id")).toString();
    QVERIFY(!downloadId.isEmpty());
    const QString expectedFilePath = QDir(getTempDir()).filePath(TEST_DUMMY_FILE);

    // 6. Wait for download to finish
    if (downloadFinishedSpy.isEmpty()) {
        QVERIFY(downloadFinishedSpy.wait(60000)); // Wait up to 60 seconds for the download to complete
    }

    // Assert that the downloadFinished signal was emitted exactly once
    QCOMPARE(downloadFinishedSpy.count(), 1);

    // Check the result of the download
    QList<QVariant> arguments = downloadFinishedSpy.takeFirst();
    QVERIFY(arguments.at(1).toBool()); // Second argument is success status (true for success)

    // 7. Verify file existence
    QVERIFY2(QFile::exists(expectedFilePath), qPrintable(QStringLiteral("Expected file not found: %1").arg(expectedFilePath)));

    // Optional: basic file size check (might vary slightly)
    QFileInfo fileInfo(expectedFilePath);
    // The dummy file is 19 bytes, but yt-dlp might add metadata so check for slightly more.
    QVERIFY(fileInfo.size() > 10); 

    // 8. Cleanup (BaseTest takes care of the temporary directory)
}

QTEST_MAIN(TestEndToEnd)
