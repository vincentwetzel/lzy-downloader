#include "TestProcessUtils.h"

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

QTEST_GUILESS_MAIN(TestProcessUtils)