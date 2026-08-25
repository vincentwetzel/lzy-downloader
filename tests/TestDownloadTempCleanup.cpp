#include "BaseTest.h"
#include "core/ConfigManager.h"
#include "core/DownloadTempCleanup.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QtTest/QtTest>

class TestDownloadTempCleanup : public BaseTest
{
    Q_OBJECT

private slots:
    void testResolveRootPreferenceOrder();
    void testPathForId();
    void testEmptyAndOwnedDirectoryRules();
    void testOrphanSweepPreservesNonUuidDirectories();
};

void TestDownloadTempCleanup::testResolveRootPreferenceOrder()
{
    ConfigManager config(QStringLiteral(":memory:"), true);
    const QString configured = QDir(getTempDir()).filePath(QStringLiteral("configured"));
    const QString completed = QDir(getTempDir()).filePath(QStringLiteral("completed"));
    config.set(QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory"), configured);
    config.set(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory"), completed);
    QCOMPARE(DownloadTempCleanup::resolveRoot(&config), QDir(configured).absolutePath());

    config.set(QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory"), QString());
    QCOMPARE(DownloadTempCleanup::resolveRoot(&config), QDir(completed).filePath(QStringLiteral("temp_downloads")));

    config.set(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory"), QString());
    const QString fallback = DownloadTempCleanup::resolveRoot(&config);
    QCOMPARE(fallback, QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                           .filePath(QStringLiteral("LzyDownloader")));
}

void TestDownloadTempCleanup::testPathForId()
{
    const QString root = QDir(getTempDir()).filePath(QStringLiteral("temp_downloads"));
    QCOMPARE(DownloadTempCleanup::pathForId(root, QStringLiteral("download-id")),
             QDir(root).filePath(QStringLiteral("download-id")));
}

void TestDownloadTempCleanup::testEmptyAndOwnedDirectoryRules()
{
    const QString root = QDir(getTempDir()).filePath(QStringLiteral("owned"));
    const QString id = QStringLiteral("44444444-4444-4444-8444-444444444444");
    const QString ownedPath = DownloadTempCleanup::pathForId(root, id);
    QVERIFY(QDir().mkpath(ownedPath));
    QVERIFY(DownloadTempCleanup::removeEmptyOwnedDirectory(id, ownedPath));
    QVERIFY(!QDir(ownedPath).exists());

    QVERIFY(QDir().mkpath(ownedPath));
    QFile partial(QDir(ownedPath).filePath(QStringLiteral("media.part")));
    QVERIFY(partial.open(QIODevice::WriteOnly));
    partial.close();
    QVERIFY(!DownloadTempCleanup::removeEmptyOwnedDirectory(id, ownedPath));
    QVERIFY(QDir(ownedPath).exists());
    QVERIFY(DownloadTempCleanup::removeOwnedDirectory(id, ownedPath));
    QVERIFY(!QDir(ownedPath).exists());

    const QString sharedRoot = QDir(getTempDir()).filePath(QStringLiteral("shared-root"));
    QVERIFY(QDir().mkpath(sharedRoot));
    QVERIFY(!DownloadTempCleanup::removeOwnedDirectory(id, sharedRoot));
    QVERIFY(QDir(sharedRoot).exists());
}

void TestDownloadTempCleanup::testOrphanSweepPreservesNonUuidDirectories()
{
    const QString root = QDir(getTempDir()).filePath(QStringLiteral("sweep"));
    const QString orphanId = QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    const QString preservedId = QStringLiteral("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
    QVERIFY(QDir().mkpath(QDir(root).filePath(orphanId)));
    QVERIFY(QDir().mkpath(QDir(root).filePath(preservedId)));
    QVERIFY(QDir().mkpath(QDir(root).filePath(QStringLiteral("not-a-uuid"))));

    QCOMPARE(DownloadTempCleanup::removeOrphanedUuidDirectories(root, {preservedId}), 1);
    QVERIFY(!QDir(QDir(root).filePath(orphanId)).exists());
    QVERIFY(QDir(QDir(root).filePath(preservedId)).exists());
    QVERIFY(QDir(QDir(root).filePath(QStringLiteral("not-a-uuid"))).exists());
}

QTEST_GUILESS_MAIN(TestDownloadTempCleanup)
#include "TestDownloadTempCleanup.moc"
