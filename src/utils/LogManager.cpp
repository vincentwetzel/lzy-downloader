#include "LogManager.h"
#include <QDebug>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QStringConverter>
#include <iostream>
#include <QDir>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QMutex>
#include <QStringList>

// Define a static file pointer for the log file
static QFile *logFile = nullptr;

// Mutex to prevent concurrent file I/O from multiple threads
static QMutex s_logMutex;

// Maximum number of log files to keep (oldest will be deleted)
static const int MAX_LOG_FILES = 5;

static bool isHeadlessMode()
{
    const QStringList args = QCoreApplication::arguments();
    return args.contains("--headless") || args.contains("--server");
}

// Custom message handler
void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    QMutexLocker locker(&s_logMutex);

    QString formattedMsg;
    QTextStream stream(&formattedMsg);
    stream.setEncoding(QStringConverter::Utf8);

    stream << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz") << " ";

    switch (type) {
    case QtDebugMsg:
        stream << "Debug: ";
        break;
    case QtInfoMsg:
        stream << "Info: ";
        break;
    case QtWarningMsg:
        stream << "Warning: ";
        break;
    case QtCriticalMsg:
        stream << "Critical: ";
        break;
    case QtFatalMsg:
        stream << "Fatal: ";
        break;
    }

    stream << msg;
    // The context information is often too verbose for a standard log file, so it's omitted.
    stream << "\n";

    // Write to stderr for console view (useful during development)
    QTextStream errStream(stderr);
    errStream.setEncoding(QStringConverter::Utf8);
    errStream << formattedMsg;
    errStream.flush();

    // Write to the log file if it's open
    if (logFile) {
        QTextStream logStream(logFile);
        logStream.setEncoding(QStringConverter::Utf8);
        logStream << formattedMsg;
        logStream.flush(); // Ensure the message is written immediately
    }

    if (type == QtFatalMsg) {
        abort();
    }
}

/**
 * Cleans up old log files, keeping only the MAX_LOG_FILES most recent.
 *
 * Log files are named: LzyDownloader_YYYY-MM-dd_HH-mm-ss.log
 * When the number of log files exceeds MAX_LOG_FILES, the oldest files are deleted.
 */
static void cleanupOldLogs(const QString &logDir) {
    QDir dir(logDir);
    if (!dir.exists()) {
        return;
    }

    // Find all log files matching the pattern, sort by name descending (newest first)
    QStringList logFiles = dir.entryList(QStringList() << "LzyDownloader*.log", QDir::Files, QDir::Name | QDir::Reversed);
    
    // We want to keep exactly MAX_LOG_FILES - 1 files before creating the new one
    const int maxKeep = MAX_LOG_FILES > 0 ? MAX_LOG_FILES - 1 : 0;

    // Delete oldest files if we exceed the limit
    if (logFiles.size() > maxKeep) {
        for (int i = maxKeep; i < logFiles.size(); ++i) {
            QString oldLog = dir.filePath(logFiles[i]);
            if (QFile::exists(oldLog)) {
                QFile::remove(oldLog);
                qDebug() << "Removed old log file:" << logFiles[i];
            }
        }
    }

    // Also cleanup legacy size-rotated logs if they exist
    QStringList legacyLogs = dir.entryList(QStringList() << "LzyDownloader.log.*", QDir::Files);
    for (const QString& legacyLog : legacyLogs) {
        QString path = dir.filePath(legacyLog);
        if (QFile::exists(path)) {
            QFile::remove(path);
            qDebug() << "Removed legacy log file:" << legacyLog;
        }
    }
}

void LogManager::installHandler() {
    // Use the same location family as settings.ini/state files (AppData on Windows).
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (logDir.isEmpty()) {
        // Fallback to application directory if AppConfigLocation is unavailable
        logDir = QCoreApplication::applicationDirPath();
    }
    if (isHeadlessMode()) {
        logDir = QDir(logDir).filePath("Server");
    }
    QDir().mkpath(logDir);

    // Clean up old log files first
    cleanupOldLogs(logDir);

    // Create a new log file with timestamp in the filename
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    const QString logPath = QDir(logDir).filePath("LzyDownloader_" + timestamp + ".log");

    // Open the new log file (write mode, no append - fresh log for each run)
    logFile = new QFile(logPath);
    if (!logFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
        std::cerr << "Failed to open log file: " << logPath.toStdString() << std::endl;
        delete logFile;
        logFile = nullptr;
        // We don't return here, so console logging will still work
    }

    qInstallMessageHandler(customMessageHandler);

    // Print the log file path on startup
    qDebug() << "Log file created:" << logPath;
}
