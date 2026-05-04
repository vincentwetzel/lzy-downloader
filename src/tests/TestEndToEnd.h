#ifndef TESTENDTOEND_H
#define TESTENDTOEND_H

#include <QtTest/QtTest>
#include "BaseTest.h"
#include "core/DownloadManager.h"
#include "core/ConfigManager.h"
#include "core/DownloadQueueManager.h"

class TestEndToEnd : public BaseTest {
    Q_OBJECT

private slots:
    void testSingleVideoDownload();

protected:
    void init();
    void cleanup();

private:
    // Helper to setup ConfigManager for test
    void setupTestConfig(ConfigManager *configManager, const QString &tempDir, const QString &ytDlpBinaryPath);

    qint64 m_serverProcessId = 0; // Stores the PID of the Python HTTP server
    QProcess *m_httpServerProcess = nullptr; // Manages the HTTP server process
};

#endif // TESTENDTOEND_H