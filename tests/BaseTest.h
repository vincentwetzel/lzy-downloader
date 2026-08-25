#pragma once

#include <QObject>
#include <QDir>
#include <QSharedPointer>
#include <QTemporaryDir> // Include QTemporaryDir here

class ConfigManager;
class ArchiveManager;

class BaseTest : public QObject {
    Q_OBJECT

public:
    explicit BaseTest(QObject *parent = nullptr);
    ~BaseTest() override;

protected:
    // Test case initialization (runs before each test function)
    void init();
    // Test case cleanup (runs after each test function)
    void cleanup();

    QString getTempDir() const;
    ConfigManager* getConfigManager();
    ArchiveManager* getArchiveManager();

private slots:
    // QTest discovers lifecycle hooks through the meta-object system.
    void initTestCase();
    void cleanupTestCase();

private:
    QTemporaryDir m_temporaryDir; // Direct member variable
    QSharedPointer<ConfigManager> m_configManager;
    QSharedPointer<ArchiveManager> m_archiveManager;
};
