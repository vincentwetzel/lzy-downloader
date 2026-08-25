#include "TestGalleryDlArgsBuilder.h"
#include "core/GalleryDlArgsBuilder.h"
#include "core/DownloadTempCleanup.h"

void TestGalleryDlArgsBuilder::testBasicBuild() {
    GalleryDlArgsBuilder builder(getConfigManager());
    
    QVariantMap options;
    options.insert(QStringLiteral("id"), QStringLiteral("test_id"));
    
    QStringList args = builder.build(QStringLiteral("https://example.com/gallery"), options);
    
    QVERIFY(args.contains(QStringLiteral("--verbose")));
    QVERIFY(args.contains(QStringLiteral("--directory")));
    QVERIFY(args.contains(QStringLiteral("-f")));
    QCOMPARE(args.last(), QStringLiteral("https://example.com/gallery"));
}

void TestGalleryDlArgsBuilder::testRateLimit() {
    GalleryDlArgsBuilder builder(getConfigManager());
    
    QVariantMap options;
    options.insert(QStringLiteral("id"), QStringLiteral("test_id"));
    
    // Test from options
    options.insert(QStringLiteral("rate_limit"), QStringLiteral("5 MB/s"));
    QStringList args = builder.build(QStringLiteral("https://example.com/gallery"), options);
    QVERIFY(args.contains(QStringLiteral("--limit-rate")));
    QCOMPARE(args.at(args.indexOf(QStringLiteral("--limit-rate")) + 1), QStringLiteral("5M"));
    
    // Test fallback to config
    options.remove(QStringLiteral("rate_limit"));
    getConfigManager()->set(QStringLiteral("General"), QStringLiteral("rate_limit"), QStringLiteral("10 KB/s"));
    args = builder.build(QStringLiteral("https://example.com/gallery"), options);
    QVERIFY(args.contains(QStringLiteral("--limit-rate")));
    QCOMPARE(args.at(args.indexOf(QStringLiteral("--limit-rate")) + 1), QStringLiteral("10K"));
}

void TestGalleryDlArgsBuilder::testCookiesFromBrowser() {
    GalleryDlArgsBuilder builder(getConfigManager());
    
    QVariantMap options;
    options.insert(QStringLiteral("id"), QStringLiteral("test_id"));
    
    // Test specific gallery setting
    getConfigManager()->set(QStringLiteral("General"), QStringLiteral("gallery_cookies_from_browser"), QStringLiteral("Firefox"));
    QStringList args = builder.build(QStringLiteral("https://example.com/gallery"), options);
    QVERIFY(args.contains(QStringLiteral("--cookies-from-browser")));
    QCOMPARE(args.at(args.indexOf(QStringLiteral("--cookies-from-browser")) + 1), QStringLiteral("firefox"));
    
    // Test fallback to yt-dlp setting
    getConfigManager()->set(QStringLiteral("General"), QStringLiteral("gallery_cookies_from_browser"), QStringLiteral("None"));
    getConfigManager()->set(QStringLiteral("General"), QStringLiteral("cookies_from_browser"), QStringLiteral("Chrome"));
    args = builder.build(QStringLiteral("https://example.com/gallery"), options);
    QVERIFY(args.contains(QStringLiteral("--cookies-from-browser")));
    QCOMPARE(args.at(args.indexOf(QStringLiteral("--cookies-from-browser")) + 1), QStringLiteral("chrome"));
}

void TestGalleryDlArgsBuilder::testOverrideArchive() {
    GalleryDlArgsBuilder builder(getConfigManager());
    
    QVariantMap options;
    options.insert(QStringLiteral("id"), QStringLiteral("test_id"));
    options.insert(QStringLiteral("override_archive"), true);
    
    QStringList args = builder.build(QStringLiteral("https://example.com/gallery"), options);
    QVERIFY(args.contains(QStringLiteral("--no-skip")));
}

void TestGalleryDlArgsBuilder::testRestrictFilenames() {
    GalleryDlArgsBuilder builder(getConfigManager());
    
    QVariantMap options;
    options.insert(QStringLiteral("id"), QStringLiteral("test_id"));
    
    getConfigManager()->set(QStringLiteral("General"), QStringLiteral("restrict_filenames"), true);
    QStringList args = builder.build(QStringLiteral("https://example.com/gallery"), options);
    QVERIFY(args.contains(QStringLiteral("--restrict-filenames")));
}

QTEST_MAIN(TestGalleryDlArgsBuilder)
