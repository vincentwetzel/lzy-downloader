#include "BaseTest.h"
#include "core/ConfigManager.h"
#include "core/ArchiveManager.h"

#include <QDebug>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDateTime>
#include <QtTest/QTest>

namespace {
    const QString TEST_SETTINGS_FILE = QStringLiteral("test_settings.ini");
    const QString TEST_ARCHIVE_FILE = QStringLiteral("test_archive.db");
}

BaseTest::BaseTest(QObject *parent)
    : QObject(parent), m_temporaryDir() { // Initialize QTemporaryDir directly
}

BaseTest::~BaseTest() {
    // QTemporaryDir destructor will handle cleanup automatically
}

void BaseTest::initTestCase() {
    qDebug() << "BaseTest::initTestCase() - Setting up unique temporary directory.";
    // m_temporaryDir is already constructed. Ensure it's valid.
    if (!m_temporaryDir.isValid()) {
        qCritical() << "Failed to create temporary directory!";
        QFAIL("Failed to create temporary directory for tests.");
    }
    qDebug() << "Temporary directory for tests:" << m_temporaryDir.path();
}

void BaseTest::cleanupTestCase() {
    qDebug() << "BaseTest::cleanupTestCase() - Cleaning up temporary directory.";
    // QTemporaryDir destructor will handle cleanup automatically when BaseTest is destroyed.
    // Clear other shared pointers explicitly.
    m_configManager.clear();
    m_archiveManager.clear();
}

void BaseTest::init() {
    // Re-initialize for each test function if needed.
}

void BaseTest::cleanup() {
    // Cleanup for each test function.
}

QString BaseTest::getTempDir() const {
    qDebug() << "BaseTest::getTempDir() called. m_temporaryDir.isValid():" << m_temporaryDir.isValid();
    if (!m_temporaryDir.isValid()) {
        qCritical() << "Attempted to access temporary directory before it was initialized or after cleanup.";
        return QString();
    }
    return m_temporaryDir.path();
}

ConfigManager* BaseTest::getConfigManager() {
    if (m_configManager.isNull()) {
        QString settingsFilePath = QDir(getTempDir()).filePath(TEST_SETTINGS_FILE);
        m_configManager = QSharedPointer<ConfigManager>::create(settingsFilePath, true, nullptr);
        qDebug() << "ConfigManager created for testing with path:" << settingsFilePath;
    }
    return m_configManager.data();
}

ArchiveManager* BaseTest::getArchiveManager() {
    if (m_archiveManager.isNull()) {
        QString dbFilePath = QDir(getTempDir()).filePath(TEST_ARCHIVE_FILE);
        m_archiveManager = QSharedPointer<ArchiveManager>::create(getConfigManager(), dbFilePath, true, nullptr);
        qDebug() << "ArchiveManager created for testing with path:" << dbFilePath;
    }
    return m_archiveManager.data();
}
