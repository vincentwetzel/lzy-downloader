#include "TestArchiveManager.h"
#include <QUrl>

void TestArchiveManager::testUrlNormalization_data() {
    QTest::addColumn<QString>("inputUrl");
    QTest::addColumn<QString>("expectedNormalizedUrl");

    // YouTube URLs
    QTest::newRow("YouTube short URL") << "https://youtu.be/dQw4w9WgXcQ" << "youtu.be/dqw4w9wgxcq";
    QTest::newRow("YouTube watch URL") << "https://www.youtube.com/watch?v=dQw4w9WgXcQ" << "www.youtube.com/watch?v=dqw4w9wgxcq";
    QTest::newRow("YouTube watch URL with playlist") << "https://www.youtube.com/watch?v=dQw4w9WgXcQ&list=PLMC9KNkIncK_bBHBwQfLkYzzu-DGgSg9" << "www.youtube.com/watch?list=plmc9knkinck_bbhbwqflkyzzu-dggsg9&v=dqw4w9wgxcq";
    QTest::newRow("YouTube watch URL with other params") << "https://www.youtube.com/watch?v=dQw4w9WgXcQ&feature=share&utm_source=some" << "www.youtube.com/watch?v=dqw4w9wgxcq";
    QTest::newRow("YouTube playlist URL") << "https://www.youtube.com/playlist?list=PLMC9KNkIncK_bBHBwQfLkYzzu-DGgSg9" << "www.youtube.com/playlist?list=plmc9knkinck_bbhbwqflkyzzu-dggsg9";

    // SoundCloud URLs (example, no specific query param filtering, so all are stripped)
    QTest::newRow("SoundCloud track URL with params") << "https://soundcloud.com/user-832168923/track-title?in=playlist-name" << "soundcloud.com/user-832168923/track-title";
    QTest::newRow("SoundCloud track URL clean") << "https://soundcloud.com/user-832168923/track-title" << "soundcloud.com/user-832168923/track-title";

    // Generic URLs (expect all query params and scheme removed)
    QTest::newRow("Generic HTTP URL") << "http://example.com/path/to/resource.html?param=value&another=test" << "example.com/path/to/resource.html";
    QTest::newRow("Generic HTTPS URL") << "https://example.com/path/to/resource.html?param=value" << "example.com/path/to/resource.html";
    QTest::newRow("URL with trailing slash") << "https://example.com/path/" << "example.com/path";
    QTest::newRow("URL without trailing slash") << "https://example.com/path" << "example.com/path";
    QTest::newRow("URL with fragment") << "https://example.com/path#fragment" << "example.com/path";
    QTest::newRow("URL with fragment only") << "#fragment" << "";
    QTest::newRow("Empty URL") << "" << "";
    QTest::newRow("Invalid URL string") << "this is not a url" << "";
}

void TestArchiveManager::testUrlNormalization() {
    QFETCH(QString, inputUrl);
    QFETCH(QString, expectedNormalizedUrl);

    QString result = m_archiveManager->normalizeUrl(inputUrl);
    QCOMPARE(result, expectedNormalizedUrl);
}

QTEST_MAIN(TestArchiveManager)

#include "TestArchiveManager.moc"
