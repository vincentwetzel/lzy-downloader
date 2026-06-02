#include "TestYtDlpArgsBuilder.h" // Include the new header
#include <QUrl> // Add QUrl include
#include "core/YtDlpArgsBuilder.h"
#include "core/ConfigManager.h"
#include "BaseTest.h" // Include BaseTest, now in src/tests


namespace {
    const QString TEST_URL = "https://www.youtube.com/watch?v=dQw4w9WgXcQ";
}

void TestYtDlpArgsBuilder::testBasicVideoArguments() {
    ConfigManager *mockConfig = getConfigManager();
    mockConfig->set("General", "restrict_filenames", true);
    mockConfig->set("Video", "video_quality", "best");
    mockConfig->set("Video", "video_extension", "mp4");
    mockConfig->set("Video", "video_codec", "Default"); // Explicitly set to Default
    mockConfig->set("Video", "video_audio_codec", "Default"); // Explicitly set to Default

    YtDlpArgsBuilder builder; // Use default constructor

    QVariantMap options;
    options["type"] = "video";
    options["videoQuality"] = "best";
    options["videoExtension"] = "mp4";
    // Add other relevant options here if needed for specific tests

    QStringList args = builder.build(mockConfig, QUrl(TEST_URL).toString(), options);

    QString expectedFormatArg = "bestvideo+bestaudio/bestvideo+bestaudio/bestvideo+bestaudio/bestvideo+bestaudio/bestvideo/bestvideo/best";

    QVERIFY(args.contains("--ignore-config"));
    QVERIFY(args.contains("--restrict-filenames")); // Now this should be true
    QVERIFY(args.contains(TEST_URL));
    QVERIFY(args.contains("-f"));
    QVERIFY(args.contains(expectedFormatArg)); // Check for the expected simple format
    QVERIFY(args.contains("--merge-output-format"));
    QVERIFY(args.contains("mp4"));
}

void TestYtDlpArgsBuilder::testSponsorBlockArguments() {
    ConfigManager *mockConfig = getConfigManager();
    mockConfig->set("General", "sponsorblock", true); // Enable SponsorBlock
    mockConfig->set("Video", "video_quality", "best");
    mockConfig->set("Video", "video_extension", "mp4");

    YtDlpArgsBuilder builder; // Use default constructor

    QVariantMap options;
    options["type"] = "video";
    options["videoQuality"] = "best";
    options["videoExtension"] = "mp4";
    // Add other relevant options here if needed for specific tests

    QStringList args = builder.build(mockConfig, QUrl(TEST_URL).toString(), options);

    QVERIFY(args.contains("--sponsorblock-remove"));
    QVERIFY(args.contains("all"));
    QVERIFY(args.contains("--force-keyframes-at-cuts"));
    QVERIFY(args.contains("--ppa"));
    QVERIFY(args.contains("ModifyChapters+ffmpeg_i:-ignore_editlist 1"));
    QVERIFY(args.contains("ModifyChapters+ffmpeg_o:-c:a copy -avoid_negative_ts make_zero -fflags +genpts -max_muxing_queue_size 2048"));
}

void TestYtDlpArgsBuilder::testLivestreamArguments() {
    ConfigManager *mockConfig = getConfigManager();
    mockConfig->set("Livestream", "quality", "720p");
    mockConfig->set("Livestream", "download_as", "MKV");
    mockConfig->set("Livestream", "convert_to", "mp4");
    mockConfig->set("Livestream", "live_from_start", true);
    mockConfig->set("Livestream", "wait_for_video", true);
    mockConfig->set("Livestream", "wait_for_video_min", 45);
    mockConfig->set("Livestream", "wait_for_video_max", 180);
    mockConfig->set("Livestream", "use_part", true);

    YtDlpArgsBuilder builder;

    QVariantMap options;
    options["type"] = "video";
    options["is_live"] = true; // This triggers livestream logic

    QStringList args = builder.build(mockConfig, QUrl(TEST_URL).toString(), options);

    QVERIFY(args.contains("--live-from-start"));
    QVERIFY(args.contains("--wait-for-video=45-180"));
    QVERIFY(args.contains("--part"));
    QVERIFY(args.contains("--merge-output-format"));
    QVERIFY(args.contains("mkv"));
    QVERIFY(args.contains("--remux-video"));
    QVERIFY(args.contains("mp4"));
    QVERIFY(args.contains("-f"));
    QVERIFY(args.contains("bestvideo[height<=?720]+bestaudio/best"));

    // Test with MPEG-TS
    mockConfig->set("Livestream", "download_as", "MPEG-TS");
    args = builder.build(mockConfig, QUrl(TEST_URL).toString(), options);
    QVERIFY(args.contains("--hls-use-mpegts"));
    QVERIFY(!args.contains("--merge-output-format"));
}

// Generates the main() function for the test executable
QTEST_MAIN(TestYtDlpArgsBuilder)