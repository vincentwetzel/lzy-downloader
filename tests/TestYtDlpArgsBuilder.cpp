#include "TestYtDlpArgsBuilder.h" // Include the new header
#include "core/YtDlpArgsBuilder.h"
#include "core/ConfigManager.h"
#include "core/DownloadTempCleanup.h"
#include "core/ProcessUtils.h"
#include "core/YtDlpLiveStatus.h"

#include <QUrl> // Add QUrl include
#include <QDir>
#include <QFile>

#include <algorithm>

namespace {
    const QString TEST_URL = QStringLiteral("https://media.example.test/watch/abc123");
}

void TestYtDlpArgsBuilder::testBasicVideoArguments() {
    ConfigManager *mockConfig = getConfigManager();
    mockConfig->set(QStringLiteral("General"), QStringLiteral("restrict_filenames"), true);
    mockConfig->set(QStringLiteral("Video"), QStringLiteral("video_quality"), QStringLiteral("best"));
    mockConfig->set(QStringLiteral("Video"), QStringLiteral("video_extension"), QStringLiteral("mp4"));
    mockConfig->set(QStringLiteral("Video"), QStringLiteral("video_codec"), QStringLiteral("Default")); // Explicitly set to Default
    mockConfig->set(QStringLiteral("Video"), QStringLiteral("video_audio_codec"), QStringLiteral("Default")); // Explicitly set to Default

    YtDlpArgsBuilder builder; // Use default constructor

    QVariantMap options;
    options[QStringLiteral("type")] = QStringLiteral("video");
    options[QStringLiteral("videoQuality")] = QStringLiteral("best");
    options[QStringLiteral("videoExtension")] = QStringLiteral("mp4");
    // Add other relevant options here if needed for specific tests

    QStringList args = builder.build(mockConfig, QUrl(TEST_URL).toString(), options);

    QString expectedFormatArg = QStringLiteral("bestvideo+bestaudio/bestvideo+bestaudio/bestvideo+bestaudio/bestvideo+bestaudio/bestvideo/bestvideo/best");

    QVERIFY(args.contains(QStringLiteral("--ignore-config")));
    QVERIFY(args.contains(QStringLiteral("--restrict-filenames"))); // Now this should be true
    QVERIFY(args.contains(TEST_URL));
    QVERIFY(args.contains(QStringLiteral("-f")));
    QVERIFY(args.contains(expectedFormatArg)); // Check for the expected simple format
    QVERIFY(args.contains(QStringLiteral("--merge-output-format")));
    QVERIFY(args.contains(QStringLiteral("mp4")));
}

void TestYtDlpArgsBuilder::testSponsorBlockArguments() {
    ConfigManager *mockConfig = getConfigManager();
    mockConfig->set(QStringLiteral("General"), QStringLiteral("sponsorblock"), true); // Enable SponsorBlock
    mockConfig->set(QStringLiteral("Video"), QStringLiteral("video_quality"), QStringLiteral("best"));
    mockConfig->set(QStringLiteral("Video"), QStringLiteral("video_extension"), QStringLiteral("mp4"));

    YtDlpArgsBuilder builder; // Use default constructor

    QVariantMap options;
    options[QStringLiteral("type")] = QStringLiteral("video");
    options[QStringLiteral("videoQuality")] = QStringLiteral("best");
    options[QStringLiteral("videoExtension")] = QStringLiteral("mp4");
    // Add other relevant options here if needed for specific tests

    QStringList args = builder.build(mockConfig, QUrl(TEST_URL).toString(), options);

    QVERIFY(args.contains(QStringLiteral("--sponsorblock-remove")));
    QVERIFY(args.contains(QStringLiteral("all")));
    QVERIFY(args.contains(QStringLiteral("--force-keyframes-at-cuts")));
    QVERIFY(args.contains(QStringLiteral("--ppa")));
    QVERIFY(args.contains(QStringLiteral("ModifyChapters+ffmpeg_o:-c:a aac -b:a 192k -af aresample=async=1:first_pts=0 -avoid_negative_ts make_zero -threads 2 -filter_threads 1 -max_muxing_queue_size 2048")));
    QVERIFY(!args.contains(QStringLiteral("-c:a copy")));
}

void TestYtDlpArgsBuilder::testLivestreamArguments() {
    ConfigManager *mockConfig = getConfigManager();
    mockConfig->set(QStringLiteral("Livestream"), QStringLiteral("quality"), QStringLiteral("720p"));
    mockConfig->set(QStringLiteral("Livestream"), QStringLiteral("download_as"), QStringLiteral("MKV"));
    mockConfig->set(QStringLiteral("Livestream"), QStringLiteral("convert_to"), QStringLiteral("mp4"));
    mockConfig->set(QStringLiteral("Livestream"), QStringLiteral("live_from_start"), true);
    mockConfig->set(QStringLiteral("Livestream"), QStringLiteral("wait_for_video"), true);
    mockConfig->set(QStringLiteral("Livestream"), QStringLiteral("wait_for_video_min"), 45);
    mockConfig->set(QStringLiteral("Livestream"), QStringLiteral("wait_for_video_max"), 180);
    mockConfig->set(QStringLiteral("Livestream"), QStringLiteral("use_part"), true);

    YtDlpArgsBuilder builder;

    QVariantMap options;
    options[QStringLiteral("type")] = QStringLiteral("video");
    options[QStringLiteral("is_live")] = true; // This triggers livestream logic
    options[QStringLiteral("live_from_start")] = true;
    options[QStringLiteral("record_from_start")] = true;
    options[QStringLiteral("live_status")] = QStringLiteral("is_live");

    QStringList args = builder.build(mockConfig, QUrl(TEST_URL).toString(), options);

    QVERIFY(args.contains(QStringLiteral("--live-from-start")) || args.contains(QStringLiteral("--no-live-from-start")));
    QVERIFY(args.contains(QStringLiteral("--wait-for-video")));
    QVERIFY(args.contains(QStringLiteral("45-180")));
    QVERIFY(args.contains(QStringLiteral("--part")));
    QVERIFY(args.contains(QStringLiteral("--remux-video")));
    QVERIFY(args.contains(QStringLiteral("mp4")));
    QVERIFY(args.contains(QStringLiteral("-f")));
    QVERIFY(args.contains(QStringLiteral("bestvideo[height<=?720]+bestaudio/best")));

    mockConfig->set(QStringLiteral("Metadata"), QStringLiteral("use_aria2c"), true);
    ProcessUtils::cacheBinary(QStringLiteral("aria2c"), {QStringLiteral("aria2c"), QStringLiteral("System PATH")});
    args = builder.build(mockConfig, QUrl(TEST_URL).toString(), options);
    QVERIFY(!args.contains(QStringLiteral("--external-downloader")));

    QVariantMap upcomingItem;
    upcomingItem.insert(QStringLiteral("type"), QStringLiteral("video"));
    QVERIFY(YtDlpLiveStatus::isExplicitUpcomingDiagnostic(QStringLiteral("ERROR: [youtube] Premieres in 5 minutes")));
    YtDlpLiveStatus::markUpcoming(&upcomingItem);
    const QStringList upcomingArgs = builder.build(mockConfig, QUrl(TEST_URL).toString(), upcomingItem);
    QVERIFY(!upcomingArgs.contains(QStringLiteral("--external-downloader")));
    QVERIFY(upcomingArgs.contains(QStringLiteral("--wait-for-video")));
    QVERIFY(!YtDlpLiveStatus::isExplicitUpcomingDiagnostic(QStringLiteral("[youtube] Title: Starting in the morning")));
    ProcessUtils::clearCache();

    // Test with MPEG-TS
    mockConfig->set(QStringLiteral("Livestream"), QStringLiteral("convert_to"), QStringLiteral("None"));
    mockConfig->set(QStringLiteral("Livestream"), QStringLiteral("download_as"), QStringLiteral("MPEG-TS"));
    args = builder.build(mockConfig, QUrl(TEST_URL).toString(), options);
    const int remuxIndex = args.indexOf(QStringLiteral("--remux-video"));
    QVERIFY(remuxIndex >= 0);
    QCOMPARE(args.value(remuxIndex + 1), QStringLiteral("ts"));
    QVERIFY(!args.contains(QStringLiteral("--hls-use-mpegts")));
}

void TestYtDlpArgsBuilder::testPostLiveReplayUsesVideoArguments() {
    ConfigManager *mockConfig = getConfigManager();
    mockConfig->set(QStringLiteral("Livestream"), QStringLiteral("live_from_start"), true);
    mockConfig->set(QStringLiteral("Livestream"), QStringLiteral("wait_for_video"), true);
    mockConfig->set(QStringLiteral("Video"), QStringLiteral("video_quality"), QStringLiteral("best"));
    mockConfig->set(QStringLiteral("Video"), QStringLiteral("video_extension"), QStringLiteral("mp4"));
    mockConfig->set(QStringLiteral("Metadata"), QStringLiteral("use_aria2c"), true);
    ProcessUtils::cacheBinary(QStringLiteral("aria2c"), {QStringLiteral("aria2c"), QStringLiteral("System PATH")});

    YtDlpArgsBuilder builder;

    QVariantMap options;
    options[QStringLiteral("type")] = QStringLiteral("video");
    options[QStringLiteral("is_live")] = true;
    options[QStringLiteral("live_status")] = QStringLiteral("post_live");

    const QStringList args = builder.build(mockConfig, QUrl(TEST_URL).toString(), options);
    ProcessUtils::clearCache();

    QVERIFY(!args.contains(QStringLiteral("--live-from-start")));
    QVERIFY(!args.contains(QStringLiteral("--hls-use-mpegts")));
    const bool hasWaitForVideo = std::any_of(args.cbegin(), args.cend(), [](const QString &arg) {
        return arg.startsWith(QStringLiteral("--wait-for-video"));
    });
    QVERIFY(!hasWaitForVideo);
    QVERIFY(!args.contains(QStringLiteral("--external-downloader")));
    QVERIFY(args.contains(QStringLiteral("--merge-output-format")));
    QVERIFY(args.contains(QStringLiteral("mp4")));
}

void TestYtDlpArgsBuilder::testLivePathIsNotLivestreamEvidence() {
    ConfigManager *mockConfig = getConfigManager();
    mockConfig->set(QStringLiteral("Video"), QStringLiteral("video_quality"), QStringLiteral("best"));
    mockConfig->set(QStringLiteral("Video"), QStringLiteral("video_extension"), QStringLiteral("mp4"));
    mockConfig->set(QStringLiteral("Metadata"), QStringLiteral("use_aria2c"), true);
    ProcessUtils::cacheBinary(QStringLiteral("aria2c"), {QStringLiteral("aria2c"), QStringLiteral("System PATH")});

    YtDlpArgsBuilder builder;

    QVariantMap options;
    options[QStringLiteral("type")] = QStringLiteral("video");

    const QStringList args = builder.build(mockConfig, QStringLiteral("https://media.example.test/live/abc123"), options);
    ProcessUtils::clearCache();

    QVERIFY(args.contains(QStringLiteral("--external-downloader")));
    QVERIFY(args.contains(QStringLiteral("--merge-output-format")));
    QVERIFY(args.contains(QStringLiteral("mp4")));
}

void TestYtDlpArgsBuilder::testAria2RetryPolicyArguments() {
    ConfigManager *mockConfig = getConfigManager();
    mockConfig->set(QStringLiteral("Metadata"), QStringLiteral("use_aria2c"), true);
    mockConfig->set(QStringLiteral("Video"), QStringLiteral("video_quality"), QStringLiteral("best"));
    mockConfig->set(QStringLiteral("Video"), QStringLiteral("video_extension"), QStringLiteral("mp4"));
    ProcessUtils::cacheBinary(QStringLiteral("aria2c"), {QStringLiteral("aria2c"), QStringLiteral("System PATH")});

    YtDlpArgsBuilder builder;
    QVariantMap options;
    options[QStringLiteral("type")] = QStringLiteral("video");

    const QStringList args = builder.build(mockConfig, QUrl(TEST_URL).toString(), options);
    ProcessUtils::clearCache();

    const qsizetype externalArgsIndex = args.indexOf(QStringLiteral("--external-downloader-args"));
    QVERIFY(externalArgsIndex >= 0);
    QVERIFY(externalArgsIndex + 1 < args.size());
    const QString externalArgs = args.at(externalArgsIndex + 1);
    QVERIFY(externalArgs.contains(QStringLiteral("--max-tries=6")));
    QVERIFY(externalArgs.contains(QStringLiteral("--retry-wait=3")));
    QVERIFY(externalArgs.contains(QStringLiteral("--max-connection-per-server=4")));
}

void TestYtDlpArgsBuilder::testAudioThumbnailEmbedding() {
    ConfigManager *mockConfig = getConfigManager();
    mockConfig->set(QStringLiteral("Metadata"), QStringLiteral("embed_thumbnail"), true);
    mockConfig->set(QStringLiteral("Metadata"), QStringLiteral("crop_audio_thumbnails"), true);
    mockConfig->set(QStringLiteral("Audio"), QStringLiteral("audio_extension"), QStringLiteral("m4a")); // Use m4a to support thumbnail embedding

    YtDlpArgsBuilder builder;

    QVariantMap options;
    options[QStringLiteral("type")] = QStringLiteral("audio");

    QStringList args = builder.build(mockConfig, QUrl(TEST_URL).toString(), options);

    QVERIFY(args.contains(QStringLiteral("--embed-thumbnail")));
}

void TestYtDlpArgsBuilder::testAudioPlaylistFolderJpg() {
    ConfigManager *mockConfig = getConfigManager();
    mockConfig->set(QStringLiteral("Metadata"), QStringLiteral("generate_folder_jpg"), true);

    YtDlpArgsBuilder builder;

    QVariantMap options;
    options[QStringLiteral("type")] = QStringLiteral("audio");
    options[QStringLiteral("id")] = QStringLiteral("uuid123");
    options[QStringLiteral("playlist_title")] = QStringLiteral("My Playlist");
    options[QStringLiteral("is_full_playlist_download")] = true;

    QStringList args = builder.build(mockConfig, QUrl(TEST_URL).toString(), options);

    QVERIFY(args.contains(QStringLiteral("--write-thumbnail")));
    bool hasFolderJpg = false;
    for (const QString &arg : args) {
        if (arg.startsWith(QStringLiteral("thumbnail:")) && arg.contains(QStringLiteral("uuid123_folder.%(ext)s"))) {
            hasFolderJpg = true;
            break;
        }
    }
    QVERIFY(hasFolderJpg);
}

void TestYtDlpArgsBuilder::testAudioPlaylistArtistMetadataFallback()
{
    ConfigManager *mockConfig = getConfigManager();
    mockConfig->set(QStringLiteral("Metadata"), QStringLiteral("embed_metadata"), true);

    YtDlpArgsBuilder builder;
    QVariantMap options;
    options[QStringLiteral("type")] = QStringLiteral("audio");
    options[QStringLiteral("is_playlist")] = true;
    options[QStringLiteral("playlist_title")] = QStringLiteral("Radiohead playlist");
    options[QStringLiteral("playlist_index")] = 1;

    const QStringList args = builder.build(mockConfig, QUrl(TEST_URL).toString(), options);
    const qsizetype parseMetadataIndex = args.indexOf(QStringLiteral("--parse-metadata"));

    QVERIFY(parseMetadataIndex >= 0);
    QVERIFY(parseMetadataIndex + 1 < args.size());
    QCOMPARE(args.at(parseMetadataIndex + 1), QStringLiteral("%(artist,artists,creator,channel,uploader)s:%(artist)s"));
    QVERIFY(!args.at(parseMetadataIndex + 1).contains(QStringLiteral("playlist_uploader")));
    QVERIFY(!args.at(parseMetadataIndex + 1).contains(QStringLiteral("playlist_owner")));

    options[QStringLiteral("is_playlist_expansion")] = true;
    const QStringList probeArgs = builder.build(mockConfig, QUrl(TEST_URL).toString(), options);
    QVERIFY(!probeArgs.contains(QStringLiteral("%(artist,artists,creator,channel,uploader)s:%(artist)s")));
}

void TestYtDlpArgsBuilder::testOrphanedTemporaryDirectorySweep()
{
    const QString root = QDir(getTempDir()).filePath(QStringLiteral("temp_downloads"));
    const QString orphanId = QStringLiteral("11111111-1111-4111-8111-111111111111");
    const QString preservedId = QStringLiteral("22222222-2222-4222-8222-222222222222");
    const QString userDirectory = QStringLiteral("keep-me");

    QVERIFY(QDir().mkpath(QDir(root).filePath(orphanId)));
    QVERIFY(QDir().mkpath(QDir(root).filePath(preservedId)));
    QVERIFY(QDir().mkpath(QDir(root).filePath(userDirectory)));
    QFile marker(QDir(root).filePath(orphanId + QStringLiteral("/partial.part")));
    QVERIFY(marker.open(QIODevice::WriteOnly));
    marker.close();

    QSet<QString> preservedIds;
    preservedIds.insert(preservedId);
    QCOMPARE(DownloadTempCleanup::removeOrphanedUuidDirectories(root, preservedIds), 1);
    QVERIFY(!QDir(QDir(root).filePath(orphanId)).exists());
    QVERIFY(QDir(QDir(root).filePath(preservedId)).exists());
    QVERIFY(QDir(QDir(root).filePath(userDirectory)).exists());
}

void TestYtDlpArgsBuilder::testTemporaryDirectoryOwnershipGuard()
{
    const QString root = QDir(getTempDir()).filePath(QStringLiteral("temp_downloads"));
    const QString id = QStringLiteral("33333333-3333-4333-8333-333333333333");
    const QString ownedPath = QDir(root).filePath(id);
    QVERIFY(QDir().mkpath(ownedPath));
    QVERIFY(QFile(QDir(ownedPath).filePath(QStringLiteral("partial.part"))).open(QIODevice::WriteOnly));

    QVERIFY(!DownloadTempCleanup::removeEmptyOwnedDirectory(id, ownedPath));
    QVERIFY(QDir(ownedPath).exists());
    QVERIFY(DownloadTempCleanup::removeOwnedDirectory(id, ownedPath));
    QVERIFY(!QDir(ownedPath).exists());

    const QString unexpectedPath = QDir(root).filePath(QStringLiteral("not-the-id"));
    QVERIFY(QDir().mkpath(unexpectedPath));
    QVERIFY(!DownloadTempCleanup::removeOwnedDirectory(id, unexpectedPath));
    QVERIFY(QDir(unexpectedPath).exists());
}

// Generates the main() function for the test executable
QTEST_MAIN(TestYtDlpArgsBuilder)
