#include "YtDlpArgsBuilder.h"
#include <QDir>
#include "core/ProcessUtils.h"
#include <QRegularExpression>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFile>
#include <QDebug>

namespace {
QString sanitizeSectionFilenameLabel(QString label)
{
    label = label.trimmed();
    if (label.isEmpty()) {
        return QString();
    }

    label.replace(QLatin1Char(':'), QLatin1Char('-'));
    label.replace(QLatin1Char('/'), QLatin1Char('-'));
    label.replace(QLatin1Char('\\'), QLatin1Char('-'));
    label.replace(QLatin1Char(' '), QLatin1Char('_'));
    static const QRegularExpression illegalCharsRe(QStringLiteral(R"([<>:"/\\|?*])"));
    static const QRegularExpression multipleUnderscoresRe(QStringLiteral(R"(_{2,})"));
    static const QRegularExpression multipleDashesRe(QStringLiteral(R"(-{2,})"));
    label.remove(illegalCharsRe);
    label.replace(multipleUnderscoresRe, QStringLiteral("_"));
    label.replace(multipleDashesRe, QStringLiteral("-"));
    return label.left(90);
}

QString appendSectionLabelToTemplate(const QString &outputTemplate, const QString &sectionLabel)
{
    const QString cleanedLabel = sanitizeSectionFilenameLabel(sectionLabel);
    if (cleanedLabel.isEmpty()) {
        return outputTemplate;
    }

    const QString suffix = QStringLiteral(" [section %1]").arg(cleanedLabel);
    const QString extToken = QStringLiteral(".%(ext)s");
    const int extIndex = outputTemplate.lastIndexOf(extToken);
    if (extIndex >= 0) {
        return QStringLiteral("%1%2%3").arg(outputTemplate.left(extIndex), suffix, outputTemplate.mid(extIndex));
    }
    return QStringLiteral("%1%2").arg(outputTemplate, suffix);
}

QString canonicalizeCodecSetting(QString codecName)
{
    codecName = codecName.trimmed();
    if (codecName.compare(QStringLiteral("H.264"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("H.264 (AVC)");
    }
    if (codecName.compare(QStringLiteral("H.265"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("H.265 (HEVC)");
    }
    return codecName;
}

QString siteSpecificReferer(const QString &url)
{
    if (url.contains(QStringLiteral("bilibili.com"), Qt::CaseInsensitive)) {
        return url;
    }
    if (url.contains(QStringLiteral("bilibili.tv"), Qt::CaseInsensitive)) {
        return QStringLiteral("https://www.bilibili.tv/");
    }
    if (url.contains(QStringLiteral("nicovideo.jp"), Qt::CaseInsensitive)
        || url.contains(QStringLiteral("nico.ms"), Qt::CaseInsensitive)) {
        return url;
    }
    return QString();
}

void appendSiteSpecificRefererWorkarounds(QStringList &args, const QString &url)
{
    const QString referer = siteSpecificReferer(url);
    if (!referer.isEmpty()) {
        args << QStringLiteral("--referer") << referer;
    }
}

QString ffmpegCutEncoderArgs(ConfigManager *configManager)
{
    const QString encoder = configManager->get(QStringLiteral("DownloadOptions"), QStringLiteral("ffmpeg_cut_encoder"), QStringLiteral("cpu")).toString();
    if (encoder == QStringLiteral("custom")) {
        return configManager->get(QStringLiteral("DownloadOptions"), QStringLiteral("ffmpeg_cut_custom_args"), QStringLiteral("")).toString().trimmed();
    }
    if (encoder == QStringLiteral("nvenc_h264")) {
        return QStringLiteral("-c:v h264_nvenc -preset p1 -cq 24 -multipass disabled");
    }
    if (encoder == QStringLiteral("qsv_h264")) {
        return QStringLiteral("-c:v h264_qsv -preset veryfast -global_quality 24");
    }
    if (encoder == QStringLiteral("amf_h264")) {
        return QStringLiteral("-c:v h264_amf -quality speed -qp_i 24 -qp_p 24");
    }
    if (encoder == QStringLiteral("videotoolbox_h264")) {
        return QStringLiteral("-c:v h264_videotoolbox -q:v 65");
    }
    return QString();
}

void appendForcedKeyframeCutArgs(QStringList &args, ConfigManager *configManager)
{
    args << QStringLiteral("--force-keyframes-at-cuts");

    QStringList ppaArgs;
    const QString encoder = configManager->get(QStringLiteral("DownloadOptions"), QStringLiteral("ffmpeg_cut_encoder"), QStringLiteral("cpu")).toString();
    const QString encoderArgs = ffmpegCutEncoderArgs(configManager);

    if (encoder == QStringLiteral("custom")) {
        if (!encoderArgs.isEmpty()) ppaArgs << encoderArgs;
    } else {
        if (!encoderArgs.isEmpty()) ppaArgs << encoderArgs;
        // Add essential A/V sync and timestamp preservation arguments.
        // Copy audio explicitly and reset timestamps to prevent drift and
        // edit-list desync issues in MP4 containers when cutting segments.
        ppaArgs << QStringLiteral("-c:a copy") << QStringLiteral("-avoid_negative_ts make_zero") << QStringLiteral("-fflags +genpts") << QStringLiteral("-max_muxing_queue_size 2048");
    }

    args << QStringLiteral("--ppa") << QStringLiteral("ModifyChapters+ffmpeg_i:-ignore_editlist 1");
    args << QStringLiteral("--ppa") << QStringLiteral("SponsorBlock+ffmpeg_i:-ignore_editlist 1");
    if (!ppaArgs.isEmpty()) {
        QString joinedArgs = ppaArgs.join(QLatin1Char(' '));
        args << QStringLiteral("--ppa") << QStringLiteral("ModifyChapters+ffmpeg_o:%1").arg(joinedArgs);
        args << QStringLiteral("--ppa") << QStringLiteral("SponsorBlock+ffmpeg_o:%1").arg(joinedArgs);
    }
}
}

YtDlpArgsBuilder::YtDlpArgsBuilder() {
}

QString YtDlpArgsBuilder::getCodecMapping(const QString& codecName) {
    const QString canonicalCodec = canonicalizeCodecSetting(codecName);

    if (canonicalCodec == QLatin1String("H.264 (AVC)")) return QStringLiteral("(avc|avc1|h264)");
    if (canonicalCodec == QLatin1String("H.265 (HEVC)")) return QStringLiteral("(hevc|h265|hev1|hvc1)");
    if (canonicalCodec == QLatin1String("VP9")) return QStringLiteral("vp0?9");
    if (canonicalCodec == QLatin1String("AV1")) return QStringLiteral("(av0?1|av01)");
    if (codecName == QLatin1String("ProRes (Archive)")) return QStringLiteral("prores");
    if (codecName == QLatin1String("Theora")) return QStringLiteral("theora");

    // Audio codecs
    if (canonicalCodec == QLatin1String("AAC")) return QStringLiteral("(aac|mp4a)");
    if (canonicalCodec == QLatin1String("Opus")) return QStringLiteral("opus");
    if (canonicalCodec == QLatin1String("Vorbis")) return QStringLiteral("vorbis");
    if (canonicalCodec == QLatin1String("MP3")) return QStringLiteral("(mp3|mpga)");
    if (canonicalCodec == QLatin1String("FLAC")) return QStringLiteral("flac");
    if (canonicalCodec == QLatin1String("PCM")) return QStringLiteral("(pcm|lpcm|wav)");
    if (canonicalCodec == QLatin1String("WAV")) return QStringLiteral("(wav|pcm|lpcm)");
    if (canonicalCodec == QLatin1String("ALAC")) return QStringLiteral("alac");
    if (canonicalCodec == QLatin1String("AC3")) return QStringLiteral("ac-?3");
    if (canonicalCodec == QLatin1String("EAC3")) return QStringLiteral("(e-?ac-?3|ec-?3)");
    if (codecName == QLatin1String("DTS")) return QStringLiteral("dts");

    // Fallback for any unmapped or simple names
    return QRegularExpression::escape(canonicalCodec.toLower());
}

QStringList YtDlpArgsBuilder::buildValidationArgs(ConfigManager *configManager, const QString &url)
{
    if (!configManager) {
        qCritical() << "YtDlpArgsBuilder::buildValidationArgs called without a ConfigManager";
        return {};
    }
    QStringList args;
    args << QStringLiteral("--ignore-config");
    args << QStringLiteral("--simulate");

    // --- Cookies ---
    QString cookiesBrowser = configManager->get(QStringLiteral("General"), QStringLiteral("cookies_from_browser"), QStringLiteral("None")).toString();
    if (cookiesBrowser != QLatin1String("None")) {
        args << QStringLiteral("--cookies-from-browser") << cookiesBrowser.toLower();
    }

    // --- Site-specific Referer Workarounds ---
    appendSiteSpecificRefererWorkarounds(args, url);

    args << url;
    return args;
}

QStringList YtDlpArgsBuilder::build(ConfigManager *configManager, const QString &url, const QVariantMap &options) {
    if (!configManager) {
        qCritical() << "YtDlpArgsBuilder::build called without a ConfigManager";
        return {};
    }
    QStringList rawArgs;

    // --- Basic arguments ---
    rawArgs << QStringLiteral("--ignore-config");
    rawArgs << QStringLiteral("--verbose");
    rawArgs << QStringLiteral("--write-info-json");
    rawArgs << QStringLiteral("--encoding") << QStringLiteral("utf-8");
    if (configManager->get(QStringLiteral("General"), QStringLiteral("restrict_filenames"), false).toBool()) rawArgs << QStringLiteral("--restrict-filenames");
    else rawArgs << QStringLiteral("--no-restrict-filenames");
    rawArgs << QStringLiteral("--newline");
    rawArgs << QStringLiteral("--force-overwrites");
    rawArgs << QStringLiteral("--ignore-errors"); // Continue on non-fatal errors (like subtitle failures)

    QString downloadType = options.value(QStringLiteral("type")).toString();
    QString finalOutputExtension;
    bool forceKeyframesAtCuts = false;

    // --- Format Selection ---
    bool isLivestream = options.value(QStringLiteral("is_live"), false).toBool() || options.value(QStringLiteral("wait_for_video"), false).toBool();

    if (isLivestream) {
        QString quality = configManager->get(QStringLiteral("Livestream"), QStringLiteral("quality"), QStringLiteral("best")).toString();
        
        if (quality.toLower() == QLatin1String("best") || quality.toLower() == QLatin1String("worst")) {
            rawArgs << QStringLiteral("-f") << quality.toLower();
        } else {
            QString res = quality.split(' ').first().remove('p');
            rawArgs << QStringLiteral("-f") << QStringLiteral("bestvideo[height<=?%1]+bestaudio/best").arg(res);
        }

        QString downloadAs = configManager->get(QStringLiteral("Livestream"), QStringLiteral("download_as"), QStringLiteral("MPEG-TS")).toString();
        if (downloadAs == QLatin1String("MPEG-TS")) {
            rawArgs << QStringLiteral("--hls-use-mpegts");
            finalOutputExtension = QStringLiteral("ts"); // With --hls-use-mpegts, the output is .ts for HLS streams.
        } else {
            rawArgs << QStringLiteral("--merge-output-format") << QStringLiteral("mkv");
            finalOutputExtension = QStringLiteral("mkv");
        }

        QString convertTo = configManager->get(QStringLiteral("Livestream"), QStringLiteral("convert_to"), QStringLiteral("None")).toString();
        if (convertTo != QLatin1String("None") && !convertTo.isEmpty()) {
            rawArgs << QStringLiteral("--remux-video") << convertTo.toLower();
            finalOutputExtension = convertTo.toLower();
        }
        
        if (configManager->get(QStringLiteral("Livestream"), QStringLiteral("live_from_start"), false).toBool()) rawArgs << QStringLiteral("--live-from-start");
        else rawArgs << QStringLiteral("--no-live-from-start");

        if (configManager->get(QStringLiteral("Livestream"), QStringLiteral("wait_for_video"), true).toBool() || options.value(QStringLiteral("wait_for_video"), false).toBool()) {
            int minWait = options.value(QStringLiteral("livestream_wait_min"), configManager->get(QStringLiteral("Livestream"), QStringLiteral("wait_for_video_min"))).toInt();
            int maxWait = options.value(QStringLiteral("livestream_wait_max"), configManager->get(QStringLiteral("Livestream"), QStringLiteral("wait_for_video_max"))).toInt();

            rawArgs << QStringLiteral("--wait-for-video=%1-%2")
                       .arg(minWait)
                       .arg(maxWait);
        } else {
            rawArgs << QStringLiteral("--no-wait-for-video");
        }

        if (configManager->get(QStringLiteral("Livestream"), QStringLiteral("use_part"), true).toBool()) rawArgs << QStringLiteral("--part");
        else rawArgs << QStringLiteral("--no-part");

    } else if (downloadType == QLatin1String("video")) {
        QString videoQuality = options.contains(QStringLiteral("video_quality")) ? options.value(QStringLiteral("video_quality")).toString() : configManager->get(QStringLiteral("Video"), QStringLiteral("video_quality"), QStringLiteral("1080p (HD)")).toString();
        QString videoCodecSetting = options.contains(QStringLiteral("video_codec")) ? options.value(QStringLiteral("video_codec")).toString() : configManager->get(QStringLiteral("Video"), QStringLiteral("video_codec"), QStringLiteral("Default")).toString();
        QString audioCodecSetting = options.contains(QStringLiteral("video_audio_codec")) ? options.value(QStringLiteral("video_audio_codec")).toString() : configManager->get(QStringLiteral("Video"), QStringLiteral("video_audio_codec"), QStringLiteral("Default")).toString();
        QString requestedExtension = configManager->get(QStringLiteral("Video"), QStringLiteral("video_extension"), QStringLiteral("mp4")).toString();
        finalOutputExtension = requestedExtension;
        const QString directFormatOverride = options.value(QStringLiteral("format")).toString().trimmed();
        const QString runtimeVideoFormat = options.value(QStringLiteral("runtime_video_format")).toString().trimmed();
        const QString runtimeAudioFormat = options.value(QStringLiteral("runtime_audio_format")).toString().trimmed();

        if (videoQuality == QLatin1String("Select at Runtime")) videoQuality = QStringLiteral("best");
        videoCodecSetting = canonicalizeCodecSetting(videoCodecSetting);
        if (videoCodecSetting == QLatin1String("Select at Runtime")) videoCodecSetting = QStringLiteral("Default");
        audioCodecSetting = canonicalizeCodecSetting(audioCodecSetting);
        if (audioCodecSetting == QLatin1String("Select at Runtime")) audioCodecSetting = QStringLiteral("Default");

        QString vcodec = getCodecMapping(videoCodecSetting);
        QString acodec = getCodecMapping(audioCodecSetting);
        QString videoFormatSelector = QStringLiteral("bestvideo");

        if (videoQuality.toLower() == QLatin1String("best") || videoQuality.toLower() == QLatin1String("worst")) {
            videoFormatSelector = QStringLiteral("%1video").arg(videoQuality.toLower());
        } else {
            videoFormatSelector += QStringLiteral("[height<=?%1]").arg(videoQuality.split(' ').first().remove('p'));
        }
        if (videoCodecSetting != QLatin1String("Default")) videoFormatSelector += QStringLiteral("[vcodec~='(?i)%1']").arg(vcodec);

        QString audioFormatSelector = QStringLiteral("bestaudio");
        if (audioCodecSetting != QLatin1String("Default")) audioFormatSelector += QStringLiteral("[acodec~='(?i)%1']").arg(acodec);

        if (!directFormatOverride.isEmpty()) {
            rawArgs << QStringLiteral("-f") << directFormatOverride;
            rawArgs << QStringLiteral("--merge-output-format") << requestedExtension;
        } else if (!runtimeVideoFormat.isEmpty() || !runtimeAudioFormat.isEmpty()) {
            const QString selectedVideoFormat = runtimeVideoFormat.isEmpty() ? videoFormatSelector : runtimeVideoFormat;
            const QString selectedAudioFormat = runtimeAudioFormat.isEmpty() ? audioFormatSelector : runtimeAudioFormat;
            rawArgs << QStringLiteral("-f") << QStringLiteral("%1+%2/%1+bestaudio/bestvideo+%2/bestvideo+bestaudio/%1/bestvideo/best").arg(selectedVideoFormat, selectedAudioFormat);
            rawArgs << QStringLiteral("--merge-output-format") << requestedExtension;
        } else {
            rawArgs << QStringLiteral("-f") << QStringLiteral("%1+%2/%1+bestaudio/bestvideo+%2/bestvideo+bestaudio/%1/bestvideo/best").arg(videoFormatSelector, audioFormatSelector);
            rawArgs << QStringLiteral("--merge-output-format") << requestedExtension;
        }

    } else if (downloadType == QLatin1String("audio")) {
        QString audioQuality = options.contains(QStringLiteral("audio_quality")) ? options.value(QStringLiteral("audio_quality")).toString() : configManager->get(QStringLiteral("Audio"), QStringLiteral("audio_quality"), QStringLiteral("Best")).toString();
        QString audioCodecSetting = options.contains(QStringLiteral("audio_codec")) ? options.value(QStringLiteral("audio_codec")).toString() : configManager->get(QStringLiteral("Audio"), QStringLiteral("audio_codec"), QStringLiteral("Default")).toString();
        finalOutputExtension = configManager->get(QStringLiteral("Audio"), QStringLiteral("audio_extension"), QStringLiteral("mp3")).toString();
        const QString directFormatOverride = options.value(QStringLiteral("format")).toString().trimmed();
        const QString runtimeAudioFormat = options.value(QStringLiteral("runtime_audio_format")).toString().trimmed();

        if (audioQuality == QLatin1String("Select at Runtime")) audioQuality = QStringLiteral("best");
        audioCodecSetting = canonicalizeCodecSetting(audioCodecSetting);
        if (audioCodecSetting == QLatin1String("Select at Runtime")) audioCodecSetting = QStringLiteral("Default");

        if (!directFormatOverride.isEmpty()) {
            rawArgs << QStringLiteral("-f") << directFormatOverride;
        } else if (!runtimeAudioFormat.isEmpty()) {
            rawArgs << QStringLiteral("-f") << runtimeAudioFormat;
        } else {
            QString acodec = getCodecMapping(audioCodecSetting);
            QString formatSelector = QStringLiteral("bestaudio");

            if (audioQuality.toLower() == QLatin1String("best") || audioQuality.toLower() == QLatin1String("worst")) {
                formatSelector = QStringLiteral("%1audio").arg(audioQuality.toLower());
            } else {
                // Strip any non-digit characters so "320K" or "128 kbps" safely becomes "320" / "128"
                static const QRegularExpression nonDigitRe(QStringLiteral("[a-zA-Z\\s]"));
                formatSelector += QStringLiteral("[abr<=?%1]").arg(QString(audioQuality).remove(nonDigitRe));
            }
            if (audioCodecSetting != QLatin1String("Default")) formatSelector += QStringLiteral("[acodec~='(?i)%1']").arg(acodec);

            rawArgs << QStringLiteral("-f") << QStringLiteral("%1/bestaudio/best").arg(formatSelector);
        }
        rawArgs << QStringLiteral("-x");
        if (audioCodecSetting != QLatin1String("Default")) rawArgs << QStringLiteral("--audio-format") << finalOutputExtension;
        rawArgs << QStringLiteral("--audio-quality") << QStringLiteral("0");
    }

    // --- Playlist Logic ---
    QString playlistLogic = options.value(QStringLiteral("playlist_logic"), QStringLiteral("Ask")).toString(); // "Ask" is fine, it's a UI string
    if (playlistLogic == QLatin1String("Download All (no prompt)")) rawArgs << QStringLiteral("--yes-playlist");
    else if (playlistLogic == QLatin1String("Download Single (ignore playlist)")) rawArgs << QStringLiteral("--no-playlist");

    // --- Duplicate Check Override ---
    if (options.value(QStringLiteral("override_archive"), false).toBool()) rawArgs << QStringLiteral("--force-download");

    // --- General Options ---
    if (configManager->get(QStringLiteral("General"), QStringLiteral("sponsorblock"), false).toBool()) {
        rawArgs << QStringLiteral("--sponsorblock-remove") << QStringLiteral("all");
        if (downloadType == "video" || isLivestream) {
            const bool sponsorBlockSegmentsChecked = options.value(QStringLiteral("sponsorblock_segments_checked"), false).toBool();
            const bool sponsorBlockHasSegments = options.value(QStringLiteral("sponsorblock_has_segments"), false).toBool();
            if (!sponsorBlockSegmentsChecked || sponsorBlockHasSegments) {
                forceKeyframesAtCuts = true;
            } else {
                qInfo() << "YtDlpArgsBuilder: SponsorBlock has no removable segments for this video; skipping forced keyframe cut encoder args.";
            }
        }
    }
    const ProcessUtils::FoundBinary aria2Binary = ProcessUtils::findBinary(QStringLiteral("aria2c"), configManager);
    if (configManager->get(QStringLiteral("Metadata"), QStringLiteral("use_aria2c"), false).toBool() && aria2Binary.source != QLatin1String("Not Found") && aria2Binary.source != QLatin1String("Invalid Custom")) {
        QString aria2cPath = aria2Binary.path;
        QStringList aria2Args;
        aria2Args << QStringLiteral("--summary-interval=1");
        const QString referer = siteSpecificReferer(url);
        if (!referer.isEmpty()) {
            aria2Args << QStringLiteral("--referer=%1").arg(referer);
        }
        rawArgs << QStringLiteral("--external-downloader") << aria2cPath;
        rawArgs << QStringLiteral("--external-downloader-args") << QStringLiteral("aria2c:%1").arg(aria2Args.join(QLatin1Char(' ')));
        qInfo() << "YtDlpArgsBuilder: Using aria2c as external downloader (" << aria2cPath << ")";
    } else {
        qInfo() << "YtDlpArgsBuilder: Using native yt-dlp downloader";
    }
    
    QString geoProxy = configManager->get(QStringLiteral("DownloadOptions"), QStringLiteral("geo_verification_proxy"), QString()).toString();
    if (!geoProxy.isEmpty()) {
        rawArgs << QStringLiteral("--geo-verification-proxy") << geoProxy;
    }

    if (configManager->get(QStringLiteral("Metadata"), QStringLiteral("embed_chapters"), true).toBool()) rawArgs << QStringLiteral("--embed-chapters");
    if (configManager->get(QStringLiteral("DownloadOptions"), QStringLiteral("split_chapters"), false).toBool()) rawArgs << QStringLiteral("--split-chapters");
    if (configManager->get(QStringLiteral("Metadata"), QStringLiteral("embed_metadata"), true).toBool()) rawArgs << QStringLiteral("--embed-metadata");

    // Inject LzyDownloader's internal ID into yt-dlp's metadata engine.
    // This gives users a %(lzy_id)s token for their output templates, guaranteeing
    // unique filenames for independent URLs from "dumb" sites where autonumber fails.
    QString internalId = options.value(QStringLiteral("id")).toString();
    if (!internalId.isEmpty()) {
        rawArgs << QStringLiteral("--parse-metadata") << QStringLiteral("%1:%(lzy_id)s").arg(internalId);
    }

    bool forceSingleAlbum = (downloadType == QLatin1String("audio") && configManager->get(QStringLiteral("Metadata"), QStringLiteral("force_playlist_as_album"), false).toBool() && options.value(QStringLiteral("playlist_index"), -1).toInt() > 0);
    if (forceSingleAlbum) {
        const QString playlistTitle = options.value(QStringLiteral("playlist_title")).toString().trimmed();
        if (!playlistTitle.isEmpty()) {
            rawArgs << QStringLiteral("--parse-metadata") << QStringLiteral("%1:%(album)s").arg(playlistTitle);
        } else {
            rawArgs << QStringLiteral("--parse-metadata") << QStringLiteral("playlist_title:%(album)s");
        }
        rawArgs << QStringLiteral("--parse-metadata") << QStringLiteral("Various Artists:%(album_artist)s");
    }

    const QStringList supportedThumbnailExts = {QStringLiteral("mp3"), QStringLiteral("mkv"), QStringLiteral("mka"), QStringLiteral("ogg"), QStringLiteral("opus"), QStringLiteral("flac"), QStringLiteral("m4a"), QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mov")};
    
    bool embedThumb = configManager->get(QStringLiteral("Metadata"), QStringLiteral("embed_thumbnail"), true).toBool();
    bool genFolderJpg = (downloadType == QLatin1String("audio") && configManager->get(QStringLiteral("Metadata"), QStringLiteral("generate_folder_jpg"), false).toBool() && options.value(QStringLiteral("playlist_index"), -1).toInt() > 0);

    bool canEmbed = embedThumb && supportedThumbnailExts.contains(finalOutputExtension, Qt::CaseInsensitive);
    // We want to write a thumbnail for the UI even if we can't embed it.
    bool shouldWrite = (downloadType == QLatin1String("video") || isLivestream || genFolderJpg);

    if (canEmbed) {
        rawArgs << QStringLiteral("--embed-thumbnail");
    } else if (shouldWrite) {
        rawArgs << QStringLiteral("--write-thumbnail");
    }

    if (canEmbed || shouldWrite) {
        QStringList ppaArgs;
        if (configManager->get(QStringLiteral("Metadata"), QStringLiteral("high_quality_thumbnail"), false).toBool()) {
            ppaArgs << QStringLiteral("-q:v 0");
        }
        
        // Crop to square if downloading audio
        if (downloadType == QLatin1String("audio") && configManager->get(QStringLiteral("Metadata"), QStringLiteral("crop_artwork_to_square"), true).toBool()) {
            ppaArgs << QStringLiteral("-vf crop=(iw+ih-abs(iw-ih))/2:(iw+ih-abs(iw-ih))/2");
        }

        if (!ppaArgs.isEmpty()) {
            rawArgs << QStringLiteral("--ppa") << QStringLiteral("ThumbnailsConvertor+ffmpeg_o:%1").arg(ppaArgs.join(QLatin1Char(' ')));
        }

        QString convertThumb = configManager->get(QStringLiteral("Metadata"), QStringLiteral("convert_thumbnail_to"), QStringLiteral("jpg")).toString();
        if (convertThumb != QLatin1String("None")) {
            rawArgs << QStringLiteral("--convert-thumbnails") << convertThumb;
        } else if (genFolderJpg && !canEmbed) {
            // If we are only writing for folder.jpg, we must convert to jpg.
            rawArgs << QStringLiteral("--convert-thumbnails") << QStringLiteral("jpg");
        }
    }

    QString tempPath = configManager->get(QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory")).toString();
    if (tempPath.isEmpty()) tempPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("LzyDownloader"));

    // Isolate the temporary directory per-download to prevent concurrent corruption
    // when multiple independent URLs evaluate to the exact same output filename.
    if (!internalId.isEmpty()) {
        tempPath = QDir(tempPath).filePath(internalId);
    }

    if (genFolderJpg) {
        rawArgs << QStringLiteral("--write-thumbnail");
        rawArgs << QStringLiteral("-o") << QStringLiteral("thumbnail:%1").arg(QDir(tempPath).filePath(QStringLiteral("%1_folder.%(ext)s").arg(options.value(QStringLiteral("id")).toString())));
    }

    // --- Subtitles ---
    bool embedSubs = configManager->get(QStringLiteral("Subtitles"), QStringLiteral("embed_subtitles"), false).toBool();
    bool writeSubs = configManager->get(QStringLiteral("Subtitles"), QStringLiteral("write_subtitles"), false).toBool();
    if (embedSubs || writeSubs) {
        QString subLangsRaw = configManager->get(QStringLiteral("Subtitles"), QStringLiteral("languages"), QStringLiteral("en")).toString();
        QStringList subLangsList = subLangsRaw.split(QLatin1Char(','), Qt::SkipEmptyParts);
        subLangsList.removeAll(QStringLiteral("runtime")); // Exclude 'runtime' from being passed to yt-dlp

        if (options.contains(QStringLiteral("runtime_subtitles"))) {
            subLangsList.append(options.value(QStringLiteral("runtime_subtitles")).toString().split(QLatin1Char(','), Qt::SkipEmptyParts));
            subLangsList.removeDuplicates();
        }

        if (!subLangsList.isEmpty()) {
            if (subLangsList.contains(QStringLiteral("all"))) {
                rawArgs << QStringLiteral("--all-subs");
            } else {
                rawArgs << QStringLiteral("--sub-langs") << subLangsList.join(',');
            }
            if (configManager->get(QStringLiteral("Subtitles"), QStringLiteral("write_auto_subtitles"), false).toBool()) rawArgs << QStringLiteral("--write-auto-subs");
            if (embedSubs) rawArgs << QStringLiteral("--embed-subs");
            if (writeSubs) {
                rawArgs << QStringLiteral("--write-subs");
                rawArgs << QStringLiteral("--sub-format") << configManager->get(QStringLiteral("Subtitles"), QStringLiteral("format"), QStringLiteral("srt")).toString();
            }
        }
    }

    // --- JS Runtime ---
    ProcessUtils::FoundBinary denoBinary = ProcessUtils::findBinary(QStringLiteral("deno"), configManager);
    if (denoBinary.source != QLatin1String("Not Found")) {
        rawArgs << QStringLiteral("--js-runtimes") << QStringLiteral("deno:%1").arg(denoBinary.path);
    }

    // --- Filename restrictions ---
    rawArgs << QStringLiteral("--windows-filenames");

    // --- Cookies ---
    QString cookiesBrowser = configManager->get(QStringLiteral("General"), QStringLiteral("cookies_from_browser"), QStringLiteral("None")).toString();
    if (cookiesBrowser != QLatin1String("None")) rawArgs << QStringLiteral("--cookies-from-browser") << cookiesBrowser.toLower();

    // --- Custom ffmpeg path ---
    // yt-dlp needs the directory containing ffmpeg and ffprobe
    QString ffmpegPath = ProcessUtils::findBinary(QStringLiteral("ffmpeg"), configManager).path;
    if (ffmpegPath != QLatin1String("ffmpeg")) { // Only add if we found a specific path
        rawArgs << QStringLiteral("--ffmpeg-location") << QFileInfo(ffmpegPath).path();
    }

    // --- Download Sections ---
    QString downloadSections = options.value(QStringLiteral("download_sections")).toString();
    if (!downloadSections.isEmpty()) {
        rawArgs << QStringLiteral("--download-sections") << downloadSections;

        // Preserve the user's requested output container instead of forcing an
        // intermediate MKV remux, which can leave clipped MP4s with bogus
        // duration metadata in players like VLC.
        if (downloadType == QLatin1String("video") || isLivestream) {
            forceKeyframesAtCuts = true;
        }
    }

    if (forceKeyframesAtCuts) {
        appendForcedKeyframeCutArgs(rawArgs, configManager);
    }
    // --- Rate Limit ---
    QString rateLimit = options.value(QStringLiteral("rate_limit"), QStringLiteral("Unlimited")).toString(); // "Unlimited" is a UI string
    if (rateLimit != QLatin1String("Unlimited")) {
        rawArgs << QStringLiteral("--limit-rate") << rateLimit.replace(QLatin1String(" MB/s"), QLatin1String("M")).replace(QLatin1String(" KB/s"), QLatin1String("K")).replace(QLatin1Char(' '), QString());
    }

    // --- Output paths ---
    if (!options.value(QStringLiteral("skip_dir_creation"), false).toBool()) {
        QDir().mkpath(tempPath);
    }
    
    QString outputTemplate;
    if (downloadType == QLatin1String("audio")) {
        outputTemplate = configManager->get(QStringLiteral("General"), QStringLiteral("output_template_audio")).toString();
    } else {
        outputTemplate = configManager->get(QStringLiteral("General"), QStringLiteral("output_template_video")).toString();
    }
    
    // Fallback to legacy combined setting if the specific ones aren't set yet
    if (outputTemplate.isEmpty()) {
        outputTemplate = configManager->get(QStringLiteral("General"), QStringLiteral("output_template")).toString();
    }

    if (outputTemplate.isEmpty()) outputTemplate = QStringLiteral("%(title)s [%(uploader)s][%(upload_date>%m-%d-%Y)s][%(id)s].%(ext)s");

    QString sectionFilenameLabel = options.value(QStringLiteral("download_sections_label")).toString();
    if (sectionFilenameLabel.isEmpty() && !downloadSections.isEmpty()) {
        sectionFilenameLabel = downloadSections;
    }
    outputTemplate = appendSectionLabelToTemplate(outputTemplate, sectionFilenameLabel);
    if (!sectionFilenameLabel.isEmpty()) {
        qDebug() << "YtDlpArgsBuilder: applied section filename suffix:" << sectionFilenameLabel;
    }

    rawArgs << QStringLiteral("-o") << QDir(tempPath).filePath(outputTemplate);

    // --- Site-specific Referer Workarounds ---
    appendSiteSpecificRefererWorkarounds(rawArgs, url);

    // --- Print final filepath ---
    rawArgs << QStringLiteral("--print") << QStringLiteral("after_move:LZY_FINAL_PATH:%(filepath)s");
    rawArgs << url;

    qDebug() << "YtDlpArgsBuilder::build final rawArgs:" << rawArgs; // Debug statement

    return rawArgs;
}
