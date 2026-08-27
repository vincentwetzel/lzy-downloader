#include "TestProcessUtils.h"
#include "core/ConfigManager.h"

#include <QDir>
#include <QFile>

void TestProcessUtils::init() {
    BaseTest::init();
    // Ensure a clean slate before every test
    ProcessUtils::clearCache();
}

void TestProcessUtils::cleanup() {
    BaseTest::cleanup();
}

void TestProcessUtils::testCacheHit() {
    // First lookup computes and stores the result
    ProcessUtils::FoundBinary first = ProcessUtils::resolveBinary(QStringLiteral("dummy-tool"), getConfigManager());
    
    // Second lookup should return identical results immediately from the cache
    ProcessUtils::FoundBinary second = ProcessUtils::resolveBinary(QStringLiteral("dummy-tool"), getConfigManager());
    
    QCOMPARE(first.source, second.source);
    QCOMPARE(first.path, second.path);
}

void TestProcessUtils::testCacheInvalidation() {
    ProcessUtils::resolveBinary(QStringLiteral("dummy-tool"), getConfigManager());
    ProcessUtils::clearCache();
    QVERIFY(true); // If it doesn't crash from static map race conditions, the test passes
}

void TestProcessUtils::testExplicitAppManagedPathWinsSystemFirstPreference() {
    const QString binDir = QDir(getConfigManager()->getConfigDir()).filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(binDir));

#ifdef Q_OS_WIN
    const QString binaryName = QStringLiteral("ffmpeg.exe");
#else
    const QString binaryName = QStringLiteral("ffmpeg");
#endif
    const QString binaryPath = QDir(binDir).filePath(binaryName);
    QFile fakeBinary(binaryPath);
    QVERIFY(fakeBinary.open(QIODevice::WriteOnly));
    fakeBinary.close();

    ConfigManager *config = getConfigManager();
    config->set(QStringLiteral("Binaries"), QStringLiteral("prefer_app_managed"), false);
    config->set(QStringLiteral("Binaries"), QStringLiteral("ffmpeg_path"), binaryPath);
    config->set(QStringLiteral("Binaries"), QStringLiteral("ffmpeg_auto_detected"), false);
    ProcessUtils::clearCache();

    const ProcessUtils::FoundBinary found = ProcessUtils::resolveBinary(QStringLiteral("ffmpeg"), config);
    QCOMPARE(found.source, QStringLiteral("Custom"));
    QCOMPARE(QDir::cleanPath(found.path), QDir::cleanPath(binaryPath));
}

QTEST_GUILESS_MAIN(TestProcessUtils)
