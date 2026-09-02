#include "TestConfigManager.h"
#include <QDir>
#include <QSignalSpy>
#include <QSettings>
#include <QThread>
#include <atomic>

void TestConfigManager::init() {
    BaseTest::init();
    // Use the constructor for testing that uses in-memory settings
    m_configManager = new ConfigManager(QStringLiteral(":memory:"), true, this);
}

void TestConfigManager::cleanup() {
    if (m_configManager) {
        m_configManager->deleteLater();
        m_configManager = nullptr;
    }
    BaseTest::cleanup();
}

void TestConfigManager::testDefaultValues() {
    // Check some known defaults from ConfigManager::initializeDefaultSettings()
    QCOMPARE(m_configManager->get(QStringLiteral("General"), QStringLiteral("theme")).toString(), QStringLiteral("System"));
    QCOMPARE(m_configManager->get(QStringLiteral("General"), QStringLiteral("max_threads")).toString(), QStringLiteral("4"));
    QCOMPARE(m_configManager->get(QStringLiteral("General"), QStringLiteral("sponsorblock")).toBool(), false);
    QCOMPARE(m_configManager->get(QStringLiteral("Metadata"), QStringLiteral("embed_thumbnail")).toBool(), true);
}

void TestConfigManager::testInvalidPlaylistLogicFallsBackToAsk() {
    const QString settingsPath = QDir(getTempDir()).filePath(QStringLiteral("invalid_playlist_logic.ini"));
    {
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("General/playlist_logic"), QStringLiteral("legacy-value"));
        settings.sync();
    }

    ConfigManager manager(settingsPath, true, nullptr);
    QCOMPARE(manager.get(QStringLiteral("General"), QStringLiteral("playlist_logic")).toString(), QStringLiteral("Ask"));
}

void TestConfigManager::testSetAndGet() {
    // Set a value and get it back
    QSignalSpy spy(m_configManager, &ConfigManager::settingChanged);
    
    QVERIFY(m_configManager->set(QStringLiteral("General"), QStringLiteral("theme"), QStringLiteral("Dark")));
    QCOMPARE(m_configManager->get(QStringLiteral("General"), QStringLiteral("theme")).toString(), QStringLiteral("Dark"));
    
    QCOMPARE(spy.count(), 1);
    QList<QVariant> arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toString(), QStringLiteral("General"));
    QCOMPARE(arguments.at(1).toString(), QStringLiteral("theme"));
    QCOMPARE(arguments.at(2).toString(), QStringLiteral("Dark"));
}

void TestConfigManager::testLegacyCleanup() {
    // Create a physical file to test the actual cleanup since QSettings with :memory: 
    // might not persist keys the same way for cleanup cycles.
    QString testIniPath = QDir(getTempDir()).filePath(QStringLiteral("legacy_test.ini"));
    
    // First, seed it with some legacy data using standard QSettings
    {
        QSettings settings(testIniPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("General/theme"), QStringLiteral("Light")); // valid
        settings.setValue(QStringLiteral("Obsolete/old_key"), QStringLiteral("junk")); // invalid
        settings.setValue(QStringLiteral("UI/window_width"), 800); // valid dynamic group
        settings.sync();
    }
    
    // Now load it via ConfigManager which should clean up legacy keys
    ConfigManager manager(testIniPath, true, nullptr);
    
    // Valid key should remain
    QCOMPARE(manager.get(QStringLiteral("General"), QStringLiteral("theme")).toString(), QStringLiteral("Light"));
    
    // Dynamic group key should remain
    QCOMPARE(manager.get(QStringLiteral("UI"), QStringLiteral("window_width")).toInt(), 800);
    
    // Legacy key should be removed. We check using get with a fallback to see if it's gone
    QCOMPARE(manager.get(QStringLiteral("Obsolete"), QStringLiteral("old_key"), QStringLiteral("fallback")).toString(), QStringLiteral("fallback"));
}

void TestConfigManager::testResetToDefaults() {
    m_configManager->set(QStringLiteral("General"), QStringLiteral("theme"), QStringLiteral("Dark"));
    m_configManager->set(QStringLiteral("General"), QStringLiteral("max_threads"), QStringLiteral("8"));
    m_configManager->set(QStringLiteral("UI"), QStringLiteral("window_width"), 800);
    
    QSignalSpy spy(m_configManager, &ConfigManager::settingsReset);
    
    m_configManager->resetToDefaults();
    
    QCOMPARE(spy.count(), 1);
    
    // Theme is a preserved key, should remain "Dark"
    QCOMPARE(m_configManager->get(QStringLiteral("General"), QStringLiteral("theme")).toString(), QStringLiteral("Dark"));
    
    // UI is a preserved group, should remain
    QCOMPARE(m_configManager->get(QStringLiteral("UI"), QStringLiteral("window_width")).toInt(), 800);
    
    // Max threads should be back to 4
    QCOMPARE(m_configManager->get(QStringLiteral("General"), QStringLiteral("max_threads")).toString(), QStringLiteral("4"));
}

void TestConfigManager::testConcurrentReadsAndWrites()
{
    std::atomic_bool allReadsValid{true};
    QList<QThread *> threads;
    for (int threadIndex = 0; threadIndex < 4; ++threadIndex) {
        QThread *thread = QThread::create([this, threadIndex, &allReadsValid]() {
            for (int iteration = 0; iteration < 250; ++iteration) {
                m_configManager->set(QStringLiteral("Concurrent"), QStringLiteral("value"), threadIndex * 1000 + iteration);
                if (!m_configManager->get(QStringLiteral("Concurrent"), QStringLiteral("value")).isValid()) {
                    allReadsValid.store(false);
                }
            }
        });
        threads.append(thread);
        thread->start();
    }

    for (QThread *thread : threads) {
        QVERIFY(thread->wait(10000));
        delete thread;
    }

    QVERIFY(allReadsValid.load());
    QVERIFY(m_configManager->get(QStringLiteral("Concurrent"), QStringLiteral("value")).isValid());
}

QTEST_GUILESS_MAIN(TestConfigManager)
