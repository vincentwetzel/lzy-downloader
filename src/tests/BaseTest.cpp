#include "BaseTest.h"
#include "ConfigManager.h"
#include "ArchiveManager.h"

#include <QDebug>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDateTime>
#include <QtTest/QTest>

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
        QString settingsFilePath = getTempDir() + "/test_settings.ini";
        m_configManager = QSharedPointer<ConfigManager>(new ConfigManager(settingsFilePath, true, this));
        qDebug() << "ConfigManager created for testing with path:" << settingsFilePath;
    }
    return m_configManager.data();
}

ArchiveManager* BaseTest::getArchiveManager() {
    if (m_archiveManager.isNull()) {
        QString dbFilePath = getTempDir() + "/test_archive.db"; // Use a file path to demonstrate
        m_archiveManager = QSharedPointer<ArchiveManager>(new ArchiveManager(getConfigManager(), dbFilePath, true, this));
        qDebug() << "ArchiveManager created for testing with path:" << dbFilePath;
    }
    return m_archiveManager.data();
}
