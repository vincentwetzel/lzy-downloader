#ifndef BASETEST_H
#define BASETEST_H

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

    // Setup that runs once before all tests in the test class
    void initTestCase();
    // Cleanup that runs once after all tests in the test class
    void cleanupTestCase();

    QString getTempDir() const;
    ConfigManager* getConfigManager();
    ArchiveManager* getArchiveManager();

private:
    QTemporaryDir m_temporaryDir; // Direct member variable
    QSharedPointer<ConfigManager> m_configManager;
    QSharedPointer<ArchiveManager> m_archiveManager;
};

#endif // BASETEST_H
