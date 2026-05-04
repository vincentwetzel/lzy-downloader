#include "TestEndToEnd.h"
#include "core/ProcessUtils.h" // Added for ProcessUtils::terminateProcessTree and findBinary
#include <QSignalSpy>
#include <QFile>
#include <QDebug>
#include <QEventLoop>
#include <QProcess>
#include <QTcpSocket> // Added for server readiness check
#include <QUuid>
#include <QTimer> // Include QTimer
#include <QNetworkAccessManager> // Added for robust server readiness check
#include <QNetworkRequest>       // Added for robust server readiness check
#include <QNetworkReply>         // Added for robust server readiness check
#include <QThread> // Added for QThread::sleep

// Helper to setup ConfigManager for test (implementation)
void TestEndToEnd::setupTestConfig(ConfigManager *configManager, const QString &tempDir, const QString &ytDlpBinaryPath) {
    configManager->set("Paths", "completed_downloads_directory", tempDir);
    configManager->set("Paths", "temporary_downloads_directory", tempDir);
    configManager->set("Binaries", "yt_dlp_path", ytDlpBinaryPath); // Use provided path
    configManager->set("Binaries", "aria2c_path", "aria2c"); // Assuming aria2c is in PATH
    configManager->set("Binaries", "ffmpeg_path", "ffmpeg"); // Assuming ffmpeg is in PATH
    // Ensure that direct downloads are enabled, and aria2c is NOT used to simplify the test initially
    configManager->set("DownloadOptions", "external_downloader_enabled", false);
    configManager->set("DownloadOptions", "external_downloader", "yt-dlp");
    
    // Explicitly clear audio/video specific templates to force fallback to generic output_template
    configManager->set("General", "output_template_audio", "");
    configManager->set("General", "output_template_video", "");
    // Set the generic output template (relative to temp dir)
    configManager->set("General", "output_template", "%(id)s.%(ext)s");
    configManager->set("Metadata", "embed_metadata", false); // Disable metadata embedding to avoid ffmpeg processing the dummy file
}

void TestEndToEnd::init() {
    BaseTest::init();
    
    // 1. Create dummy video file
    const QString videoFilePath = getTempDir() + "/test_video.webm";
    QFile videoFile(videoFilePath);
    if (videoFile.open(QIODevice::WriteOnly)) {
        videoFile.write("dummy video content", 19); // Small content for a small file size
        videoFile.close();
    } else {
        QFAIL(qPrintable(QString("Failed to create dummy video file: %1").arg(videoFile.errorString())));
    }

    // 2. Start Python HTTP server
    const QString sourcePythonScriptPath = QCoreApplication::applicationDirPath() + "/../../../src/tests/test_server.py";
    if (sourcePythonScriptPath.isEmpty()) {
        QFAIL("test_server.py not found. Ensure it's in the correct test data path.");
    }
    const QString destPythonScriptPath = getTempDir() + "/test_server.py";
    if (!QFile::copy(sourcePythonScriptPath, destPythonScriptPath)) {
        QFAIL(qPrintable(QString("Failed to copy test_server.py to temporary directory: %1").arg(destPythonScriptPath)));
    }


    m_httpServerProcess = new QProcess(this);
    m_httpServerProcess->setProgram("python.exe"); // Use python.exe explicitly
    m_httpServerProcess->setArguments(QStringList() << destPythonScriptPath); // Run the copied script
    m_httpServerProcess->setWorkingDirectory(getTempDir()); // Server serves from temp dir

    QEventLoop loop;
    
    QObject::connect(m_httpServerProcess, &QProcess::readyReadStandardOutput, this, [&]() {
        QString output = m_httpServerProcess->readAllStandardOutput();
        qDebug() << "HTTP Server STDOUT:" << output.trimmed();
    });
    QObject::connect(m_httpServerProcess, &QProcess::readyReadStandardError, this, [&]() {
        QString output = m_httpServerProcess->readAllStandardError();
        qWarning() << "HTTP Server STDERR:" << output.trimmed(); // Print stderr
    });

    QObject::connect(m_httpServerProcess, &QProcess::errorOccurred, this, [&](QProcess::ProcessError error){
        QFAIL(qPrintable(QString("HTTP Server process error: %1, %2").arg(error).arg(m_httpServerProcess->errorString())));
    });
    
    m_httpServerProcess->start();
    QVERIFY(m_httpServerProcess->waitForStarted(5000)); // Wait for process to start
    
    // Robust server readiness check: try to connect with a QTcpSocket
    QTcpSocket socket;
    qint64 startTime = QDateTime::currentMSecsSinceEpoch();
    while (socket.state() != QAbstractSocket::ConnectedState && QDateTime::currentMSecsSinceEpoch() - startTime < 10000) {
        socket.connectToHost("localhost", 8000);
        if (socket.waitForConnected(200)) {
            break;
        }
    }
    
    if (socket.state() != QAbstractSocket::ConnectedState) {
        QFAIL("Timed out waiting for local HTTP server to become responsive.");
    }
    socket.disconnectFromHost();

    m_serverProcessId = m_httpServerProcess->processId(); // Store PID for cleanup
    qDebug() << "Started test HTTP server with PID:" << m_serverProcessId;

    QThread::sleep(1); // Add a small delay after server is confirmed responsive
}

void TestEndToEnd::cleanup() {
    if (m_httpServerProcess && m_httpServerProcess->state() == QProcess::Running) {
        m_httpServerProcess->terminate();
        m_httpServerProcess->waitForFinished(3000); // Wait for termination
        if (m_httpServerProcess->state() == QProcess::Running) {
            m_httpServerProcess->kill(); // Force kill if still running
            m_httpServerProcess->waitForFinished(1000);
        }
        qDebug() << "Terminated test HTTP server process with PID:" << m_serverProcessId;
    } else if (m_serverProcessId != 0) {
        // Fallback for detached process if any part of the test failed to properly manage m_httpServerProcess
#ifdef Q_OS_WIN
        if (QProcess::execute("taskkill /F /PID " + QString::number(m_serverProcessId)) != 0) {
             qWarning() << "Failed to terminate test HTTP server fallback process with PID:" << m_serverProcessId;
        }
#else
        // For Unix-like systems, use 'kill' command
        if (QProcess::execute("kill -9 " + QString::number(m_serverProcessId)) != 0) {
             qWarning() << "Failed to terminate test HTTP server fallback process with PID:" << m_serverProcessId;
        }
#endif
        qDebug() << "Terminated test HTTP server fallback process with PID:" << m_serverProcessId;
    }
    m_serverProcessId = 0; // Reset PID
    BaseTest::cleanup();
}

void TestEndToEnd::testSingleVideoDownload() {
    const QString testUrl = "http://localhost:8000/test_video.webm"; // Local HTTP server URL
    
    // 1. Setup ConfigManager
    ConfigManager *configManager = getConfigManager();
    // Get actual yt-dlp binary path
    const QString ytDlpPath = ProcessUtils::findBinary("yt-dlp", configManager).path;
    if (ytDlpPath.isEmpty()) {
        QFAIL("yt-dlp binary not found in system PATH. Cannot run end-to-end test.");
    }
    setupTestConfig(configManager, getTempDir(), ytDlpPath);
    configManager->save(); // Ensure settings are saved to disk

    // 2. Instantiate DownloadManager and its dependencies
    DownloadManager downloadManager(configManager, this);

    // 3. Monitor download completion and id
    QSignalSpy downloadFinishedSpy(&downloadManager, &DownloadManager::downloadFinished);
    QVERIFY(downloadFinishedSpy.isValid());
    
    QSignalSpy downloadAddedSpy(&downloadManager, &DownloadManager::downloadAddedToQueue);
    QVERIFY(downloadAddedSpy.isValid());

    // 4. Enqueue download
    QVariantMap downloadOptionsMap;
    downloadOptionsMap["type"] = "video";
    downloadOptionsMap["format"] = "bestvideo[ext=webm]+bestaudio[ext=webm]/best[ext=webm]"; // Force webm for consistent filename
    
    downloadManager.enqueueDownload(testUrl, downloadOptionsMap);

    // 5. Wait for download to be added to the queue and get the id
    QVERIFY(downloadAddedSpy.wait(5000));
    QList<QVariant> addedArguments = downloadAddedSpy.takeFirst();
    QString downloadId = addedArguments.at(0).toMap().value("id").toString();
    const QString expectedFilePath = QDir(getTempDir()).filePath("test_video.webm");

    // 6. Wait for download to finish
    QVERIFY(downloadFinishedSpy.wait(60000)); // Wait up to 60 seconds for the download to complete

    // Assert that the downloadFinished signal was emitted exactly once
    QCOMPARE(downloadFinishedSpy.count(), 1);

    // Check the result of the download
    QList<QVariant> arguments = downloadFinishedSpy.takeFirst();
    QVERIFY(arguments.at(1).toBool()); // Second argument is success status (true for success)

    // 7. Verify file existence
    QVERIFY2(QFile::exists(expectedFilePath), qPrintable(QString("Expected file not found: %1").arg(expectedFilePath)));

    // Optional: basic file size check (might vary slightly)
    QFileInfo fileInfo(expectedFilePath);
    // The dummy file is 19 bytes, but yt-dlp might add metadata so check for slightly more.
    QVERIFY(fileInfo.size() > 10); 

    // 8. Cleanup (BaseTest takes care of the temporary directory)
}

QTEST_MAIN(TestEndToEnd)
