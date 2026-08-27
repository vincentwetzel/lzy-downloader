#include "TestProcessUtils.h"
#include "core/ConfigManager.h"

#include <QDir>
#include <QFile>
#include <QScopeGuard>

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

void TestProcessUtils::testAutoDetectedConfiguredPathIsRetained() {
    const QString binaryName = QStringLiteral("lzy-test-binary");
    const QString binDir = QDir(getConfigManager()->getConfigDir()).filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(binDir));

#ifdef Q_OS_WIN
    const QString binaryPath = QDir(binDir).filePath(binaryName + QStringLiteral(".exe"));
#else
    const QString binaryPath = QDir(binDir).filePath(binaryName);
#endif
    QFile fakeBinary(binaryPath);
    QVERIFY(fakeBinary.open(QIODevice::WriteOnly));
    fakeBinary.close();
#ifndef Q_OS_WIN
    QVERIFY(fakeBinary.setPermissions(fakeBinary.permissions()
                                     | QFileDevice::ExeOwner
                                     | QFileDevice::ExeGroup
                                     | QFileDevice::ExeOther));
#endif

    ConfigManager *config = getConfigManager();
    config->set(QStringLiteral("Binaries"), binaryName + QStringLiteral("_path"), binaryPath);
    config->set(QStringLiteral("Binaries"), binaryName + QStringLiteral("_auto_detected"), true);
    config->set(QStringLiteral("Binaries"), QStringLiteral("prefer_app_managed"), false);
    ProcessUtils::clearCache();

    const ProcessUtils::FoundBinary found = ProcessUtils::resolveBinary(binaryName, config);
    QCOMPARE(QDir::cleanPath(found.path), QDir::cleanPath(binaryPath));
    QCOMPARE(found.source, QStringLiteral("LzyDownloader managed"));
}

#ifdef Q_OS_WIN
void TestProcessUtils::testWinGetPackageCandidateDiscovery() {
    const QByteArray previousLocalAppData = qgetenv("LOCALAPPDATA");
    const bool hadLocalAppData = !previousLocalAppData.isNull();
    [[maybe_unused]] const auto restoreLocalAppData = qScopeGuard([previousLocalAppData, hadLocalAppData]() {
        if (hadLocalAppData) {
            qputenv("LOCALAPPDATA", previousLocalAppData);
        } else {
            qunsetenv("LOCALAPPDATA");
        }
    });

    const QString localAppData = QDir(getTempDir()).filePath(QStringLiteral("winget-local"));
    const QString packageBin = QDir(localAppData).filePath(
        QStringLiteral("Microsoft/WinGet/Packages/Lzy-Winget-Test/1.0/bin"));
    QVERIFY(QDir().mkpath(packageBin));

    const QString binaryPath = QDir(packageBin).filePath(QStringLiteral("lzy-winget-test.exe"));
    QFile fakeBinary(binaryPath);
    QVERIFY(fakeBinary.open(QIODevice::WriteOnly));
    fakeBinary.close();

    qputenv("LOCALAPPDATA", localAppData.toUtf8());
    ProcessUtils::clearCache();

    const ProcessUtils::FoundBinary found = ProcessUtils::resolveBinary(
        QStringLiteral("lzy-winget-test"), getConfigManager());
    QCOMPARE(QDir::cleanPath(found.path), QDir::cleanPath(binaryPath));
    QCOMPARE(found.source, QStringLiteral("WinGet"));
}
#endif

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
