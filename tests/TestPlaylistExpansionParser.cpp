#include "TestPlaylistExpansionParser.h"
#include "core/PlaylistExpansionParser.h"
#include <QJsonObject>
#include <QJsonArray>

void TestPlaylistExpansionParser::testSingleVideoItem() {
    QJsonObject root;
    root.insert(QStringLiteral("id"), QStringLiteral("12345"));
    root.insert(QStringLiteral("title"), QStringLiteral("Test Video"));
    root.insert(QStringLiteral("is_live"), true);
    
    PlaylistExpansionParseResult result = PlaylistExpansionParser::parse(root, QStringLiteral("https://example.com/watch?v=12345"));
    
    QCOMPARE(result.isPlaylist, false);
    QCOMPARE(result.items.size(), 1);
    
    QVariantMap item = result.items.first();
    QCOMPARE(item.value(QStringLiteral("url")).toString(), QStringLiteral("https://example.com/watch?v=12345"));
    QCOMPARE(item.value(QStringLiteral("title")).toString(), QStringLiteral("Test Video"));
    QCOMPARE(item.value(QStringLiteral("is_live")).toBool(), true);
    QCOMPARE(item.value(QStringLiteral("is_playlist")).toBool(), false);
}

void TestPlaylistExpansionParser::testFlatPlaylist() {
    QJsonArray entries;
    
    QJsonObject entry1;
    entry1.insert(QStringLiteral("url"), QStringLiteral("https://example.com/watch?v=111"));
    entry1.insert(QStringLiteral("title"), QStringLiteral("Video 1"));
    entry1.insert(QStringLiteral("playlist_index"), 1);
    entries.append(entry1);
    
    QJsonObject entry2;
    entry2.insert(QStringLiteral("webpage_url"), QStringLiteral("https://example.com/watch?v=222"));
    entry2.insert(QStringLiteral("title"), QStringLiteral("Video 2"));
    entry2.insert(QStringLiteral("playlist_index"), 2);
    entries.append(entry2);

    QJsonObject root;
    root.insert(QStringLiteral("entries"), entries);
    root.insert(QStringLiteral("playlist_title"), QStringLiteral("My Playlist"));
    
    PlaylistExpansionParseResult result = PlaylistExpansionParser::parse(root, QStringLiteral("https://example.com/playlist?list=abc"));
    
    QCOMPARE(result.isPlaylist, true);
    QCOMPARE(result.items.size(), 2);
    
    QVariantMap item1 = result.items.at(0);
    QCOMPARE(item1.value(QStringLiteral("url")).toString(), QStringLiteral("https://example.com/watch?v=111"));
    QCOMPARE(item1.value(QStringLiteral("title")).toString(), QStringLiteral("Video 1"));
    QCOMPARE(item1.value(QStringLiteral("playlist_title")).toString(), QStringLiteral("My Playlist"));
    
    QVariantMap item2 = result.items.at(1);
    QCOMPARE(item2.value(QStringLiteral("url")).toString(), QStringLiteral("https://example.com/watch?v=222"));
    QCOMPARE(item2.value(QStringLiteral("title")).toString(), QStringLiteral("Video 2"));
}

void TestPlaylistExpansionParser::testNestedPlaylistEntries() {
    QJsonArray entries;
    
    // Testing YouTube ID fallback
    QJsonObject entry1;
    entry1.insert(QStringLiteral("id"), QStringLiteral("abcde"));
    entry1.insert(QStringLiteral("extractor_key"), QStringLiteral("Youtube"));
    entry1.insert(QStringLiteral("title"), QStringLiteral("Video 3"));
    entries.append(entry1);
    
    QJsonObject root;
    root.insert(QStringLiteral("entries"), entries);
    
    PlaylistExpansionParseResult result = PlaylistExpansionParser::parse(root, QStringLiteral("https://example.com/playlist?list=xyz"));
    
    QCOMPARE(result.isPlaylist, true);
    QCOMPARE(result.items.size(), 1);
    
    QVariantMap item1 = result.items.at(0);
    QCOMPARE(item1.value(QStringLiteral("url")).toString(), QStringLiteral("https://www.youtube.com/watch?v=abcde"));
}

void TestPlaylistExpansionParser::testMalformedData() {
    // Missing entries but it's empty
    QJsonObject root;
    PlaylistExpansionParseResult result = PlaylistExpansionParser::parse(root, QStringLiteral("https://example.com/bad"));
    
    QCOMPARE(result.isPlaylist, false);
    QCOMPARE(result.items.size(), 1);
    QCOMPARE(result.items.first().value(QStringLiteral("url")).toString(), QStringLiteral("https://example.com/bad"));
}

void TestPlaylistExpansionParser::testEmptyData() {
    QJsonObject root;
    QJsonArray emptyEntries;
    root.insert(QStringLiteral("entries"), emptyEntries);
    
    PlaylistExpansionParseResult result = PlaylistExpansionParser::parse(root, QStringLiteral("https://example.com/empty"));
    
    QCOMPARE(result.isPlaylist, true);
    QCOMPARE(result.items.size(), 0);
}

void TestPlaylistExpansionParser::testRequestedIndexUsesGenericPlaylistIndex() {
    QJsonArray entries;
    for (int index = 1; index <= 3; ++index) {
        QJsonObject entry;
        entry.insert(QStringLiteral("webpage_url"),
                     QStringLiteral("https://example.com/item/%1").arg(index));
        entry.insert(QStringLiteral("title"), QStringLiteral("Entry %1").arg(index));
        entry.insert(QStringLiteral("playlist_index"), index);
        entries.append(entry);
    }

    QJsonObject root;
    root.insert(QStringLiteral("entries"), entries);

    const PlaylistExpansionParseResult result = PlaylistExpansionParser::parse(
        root, QStringLiteral("https://example.com/collection?item=2"));

    QCOMPARE(result.items.size(), 1);
    QCOMPARE(result.items.first().value(QStringLiteral("url")).toString(),
             QStringLiteral("https://example.com/item/2"));
    QCOMPARE(result.items.first().value(QStringLiteral("playlist_index")).toInt(), 2);
}

void TestPlaylistExpansionParser::testReplayLiveStatusIsNotActive() {
    QJsonObject root;
    root.insert(QStringLiteral("webpage_url"), QStringLiteral("https://example.com/replay"));
    root.insert(QStringLiteral("title"), QStringLiteral("Archived replay"));
    root.insert(QStringLiteral("live_status"), QStringLiteral("post_live"));

    const PlaylistExpansionParseResult result = PlaylistExpansionParser::parse(
        root, QStringLiteral("https://example.com/replay"));

    QCOMPARE(result.items.size(), 1);
    QCOMPARE(result.items.first().value(QStringLiteral("live_status")).toString(),
             QStringLiteral("post_live"));
    QVERIFY(!result.items.first().value(QStringLiteral("is_live")).toBool());
}

QTEST_MAIN(TestPlaylistExpansionParser)
