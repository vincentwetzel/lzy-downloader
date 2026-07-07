#include "TestYtDlpArgsBuilder.h" // Include the new header
#include "core/YtDlpArgsBuilder.h"
#include "core/ConfigManager.h"
#include "core/ProcessUtils.h"

#include <QUrl> // Add QUrl include

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
    QVERIFY(args.contains(QStringLiteral("ModifyChapters+ffmpeg_o:-c:a copy -avoid_negative_ts make_zero -fflags +genpts -max_muxing_queue_size 2048")));
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
    QVERIFY(args.contains(QStringLiteral("--merge-output-format")));
    QVERIFY(args.contains(QStringLiteral("mkv")));
    QVERIFY(args.contains(QStringLiteral("--remux-video")));
    QVERIFY(args.contains(QStringLiteral("mp4")));
    QVERIFY(args.contains(QStringLiteral("-f")));
    QVERIFY(args.contains(QStringLiteral("bestvideo[height<=?720]+bestaudio/best")));

    // Test with MPEG-TS
    mockConfig->set(QStringLiteral("Livestream"), QStringLiteral("download_as"), QStringLiteral("MPEG-TS"));
    args = builder.build(mockConfig, QUrl(TEST_URL).toString(), options);
    QVERIFY(args.contains(QStringLiteral("--hls-use-mpegts")));
    QVERIFY(!args.contains(QStringLiteral("--merge-output-format")));
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

void TestYtDlpArgsBuilder::testLiveUrlHintBypassesAria2ForVideoArguments() {
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

    QVERIFY(!args.contains(QStringLiteral("--external-downloader")));
    QVERIFY(args.contains(QStringLiteral("--merge-output-format")));
    QVERIFY(args.contains(QStringLiteral("mp4")));
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

// Generates the main() function for the test executable
QTEST_MAIN(TestYtDlpArgsBuilder)
