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

    label.replace(':', '-');
    label.replace('/', '-');
    label.replace('\\', '-');
    label.replace(' ', '_');
    label.remove(QRegularExpression(R"([<>:"/\\|?*])"));
    label.replace(QRegularExpression(R"(_{2,})"), "_");
    label.replace(QRegularExpression(R"(-{2,})"), "-");
    return label.left(90);
}

QString appendSectionLabelToTemplate(const QString &outputTemplate, const QString &sectionLabel)
{
    const QString cleanedLabel = sanitizeSectionFilenameLabel(sectionLabel);
    if (cleanedLabel.isEmpty()) {
        return outputTemplate;
    }

    const QString suffix = QString(" [section %1]").arg(cleanedLabel);
    const QString extToken = ".%(ext)s";
    const int extIndex = outputTemplate.lastIndexOf(extToken);
    if (extIndex >= 0) {
        return outputTemplate.left(extIndex) + suffix + outputTemplate.mid(extIndex);
    }
    return outputTemplate + suffix;
}

QString canonicalizeCodecSetting(QString codecName)
{
    codecName = codecName.trimmed();
    if (codecName.compare("H.264", Qt::CaseInsensitive) == 0) {
        return "H.264 (AVC)";
    }
    if (codecName.compare("H.265", Qt::CaseInsensitive) == 0) {
        return "H.265 (HEVC)";
    }
    return codecName;
}

QString siteSpecificReferer(const QString &url)
{
    if (url.contains("bilibili.com", Qt::CaseInsensitive)) {
        return url;
    }
    if (url.contains("bilibili.tv", Qt::CaseInsensitive)) {
        return "https://www.bilibili.tv/";
    }
    if (url.contains("nicovideo.jp", Qt::CaseInsensitive)
        || url.contains("nico.ms", Qt::CaseInsensitive)) {
        return url;
    }
    return QString();
}

void appendSiteSpecificRefererWorkarounds(QStringList &args, const QString &url)
{
    const QString referer = siteSpecificReferer(url);
    if (!referer.isEmpty()) {
        args << "--referer" << referer;
    }
}

QString ffmpegCutEncoderArgs(ConfigManager *configManager)
{
    const QString encoder = configManager->get("DownloadOptions", "ffmpeg_cut_encoder", "cpu").toString();
    if (encoder == "custom") {
        return configManager->get("DownloadOptions", "ffmpeg_cut_custom_args", "").toString().trimmed();
    }
    if (encoder == "nvenc_h264") {
        return "-c:v h264_nvenc -preset p1 -cq 24 -multipass disabled";
    }
    if (encoder == "qsv_h264") {
        return "-c:v h264_qsv -preset veryfast -global_quality 24";
    }
    if (encoder == "amf_h264") {
        return "-c:v h264_amf -quality speed -qp_i 24 -qp_p 24";
    }
    if (encoder == "videotoolbox_h264") {
        return "-c:v h264_videotoolbox -q:v 65";
    }
    return QString();
}

void appendForcedKeyframeCutArgs(QStringList &args, ConfigManager *configManager)
{
    args << "--force-keyframes-at-cuts";

    QStringList ppaArgs;
    const QString encoder = configManager->get("DownloadOptions", "ffmpeg_cut_encoder", "cpu").toString();
    const QString encoderArgs = ffmpegCutEncoderArgs(configManager);

    if (encoder == "custom") {
        if (!encoderArgs.isEmpty()) ppaArgs << encoderArgs;
    } else {
        if (!encoderArgs.isEmpty()) ppaArgs << encoderArgs;
        // Add essential A/V sync and timestamp preservation arguments.
        // Copy audio explicitly and reset timestamps to prevent drift and
        // edit-list desync issues in MP4 containers when cutting segments.
        ppaArgs << "-c:a copy" << "-avoid_negative_ts make_zero" << "-fflags +genpts" << "-max_muxing_queue_size 2048";
    }

    args << "--ppa" << "ModifyChapters+ffmpeg_i:-ignore_editlist 1";
    args << "--ppa" << "SponsorBlock+ffmpeg_i:-ignore_editlist 1";
    if (!ppaArgs.isEmpty()) {
        QString joinedArgs = ppaArgs.join(" ");
        args << "--ppa" << QString("ModifyChapters+ffmpeg_o:%1").arg(joinedArgs);
        args << "--ppa" << QString("SponsorBlock+ffmpeg_o:%1").arg(joinedArgs);
    }
}
}

YtDlpArgsBuilder::YtDlpArgsBuilder() {
}

QString YtDlpArgsBuilder::getCodecMapping(const QString& codecName) {
    const QString canonicalCodec = canonicalizeCodecSetting(codecName);

    if (canonicalCodec == "H.264 (AVC)") return "(avc|avc1|h264)";
    if (canonicalCodec == "H.265 (HEVC)") return "(hevc|h265|hev1|hvc1)";
    if (canonicalCodec == "avc1 (h264)") return "(avc|avc1|h264)";
    if (canonicalCodec == "VP9") return "vp0?9";
    if (canonicalCodec == "AV1") return "(av0?1|av01)";
    if (codecName == "ProRes (Archive)") return "prores";
    if (codecName == "Theora") return "theora";

    // Audio codecs
    if (canonicalCodec == "AAC") return "(aac|mp4a)";
    if (canonicalCodec == "Opus") return "opus";
    if (canonicalCodec == "Vorbis") return "vorbis";
    if (canonicalCodec == "MP3") return "(mp3|mpga)";
    if (canonicalCodec == "FLAC") return "flac";
    if (canonicalCodec == "PCM") return "(pcm|lpcm|wav)";
    if (canonicalCodec == "WAV") return "(wav|pcm|lpcm)";
    if (canonicalCodec == "ALAC") return "alac";
    if (canonicalCodec == "AC3") return "ac-?3";
    if (canonicalCodec == "EAC3") return "(e-?ac-?3|ec-?3)";
    if (codecName == "DTS") return "dts";

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
    args << "--ignore-config";
    args << "--simulate";

    // --- Cookies ---
    QString cookiesBrowser = configManager->get("General", "cookies_from_browser", "None").toString();
    if (cookiesBrowser != "None") {
        args << "--cookies-from-browser" << cookiesBrowser.toLower();
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
    rawArgs << "--ignore-config";
    rawArgs << "--verbose";
    rawArgs << "--write-info-json";
    rawArgs << "--encoding" << "utf-8";
    if (configManager->get("General", "restrict_filenames", false).toBool()) rawArgs << "--restrict-filenames";
    else rawArgs << "--no-restrict-filenames";
    rawArgs << "--newline";
    rawArgs << "--force-overwrites";
    rawArgs << "--ignore-errors"; // Continue on non-fatal errors (like subtitle failures)

    QString downloadType = options.value("type").toString();
    QString finalOutputExtension;
    bool forceKeyframesAtCuts = false;

    // --- Format Selection ---
    bool isLivestream = options.value("is_live", false).toBool() || options.value("wait_for_video", false).toBool();

    if (isLivestream) {
        QString quality = configManager->get("Livestream", "quality", "best").toString();
        
        if (quality.toLower() == "best" || quality.toLower() == "worst") {
            rawArgs << "-f" << quality.toLower();
        } else {
            QString res = quality.split(' ').first().remove('p');
            rawArgs << "-f" << QString("bestvideo[height<=?%1]+bestaudio/best").arg(res);
        }

        QString downloadAs = configManager->get("Livestream", "download_as", "MPEG-TS").toString();
        if (downloadAs == "MPEG-TS") {
            rawArgs << "--hls-use-mpegts";
            finalOutputExtension = "ts"; // With --hls-use-mpegts, the output is .ts for HLS streams.
        } else {
            rawArgs << "--merge-output-format" << "mkv";
            finalOutputExtension = "mkv";
        }

        QString convertTo = configManager->get("Livestream", "convert_to", "None").toString();
        if (convertTo != "None" && !convertTo.isEmpty()) {
            rawArgs << "--remux-video" << convertTo.toLower();
            finalOutputExtension = convertTo.toLower();
        }
        
        if (configManager->get("Livestream", "live_from_start", false).toBool()) rawArgs << "--live-from-start";
        else rawArgs << "--no-live-from-start";

        if (configManager->get("Livestream", "wait_for_video", true).toBool() || options.value("wait_for_video", false).toBool()) {
            int minWait = options.value("livestream_wait_min", configManager->get("Livestream", "wait_for_video_min")).toInt();
            int maxWait = options.value("livestream_wait_max", configManager->get("Livestream", "wait_for_video_max")).toInt();

            rawArgs << QString("--wait-for-video=%1-%2")
                       .arg(minWait)
                       .arg(maxWait);
        } else {
            rawArgs << "--no-wait-for-video";
        }

        if (configManager->get("Livestream", "use_part", true).toBool()) rawArgs << "--part";
        else rawArgs << "--no-part";

    } else if (downloadType == "video") {
        QString videoQuality = options.contains("video_quality") ? options.value("video_quality").toString() : configManager->get("Video", "video_quality", "1080p (HD)").toString();
        QString videoCodecSetting = options.contains("video_codec") ? options.value("video_codec").toString() : configManager->get("Video", "video_codec", "Default").toString();
        QString audioCodecSetting = options.contains("video_audio_codec") ? options.value("video_audio_codec").toString() : configManager->get("Video", "video_audio_codec", "Default").toString();
        QString requestedExtension = configManager->get("Video", "video_extension", "mp4").toString();
        finalOutputExtension = requestedExtension;
        const QString directFormatOverride = options.value("format").toString().trimmed();
        const QString runtimeVideoFormat = options.value("runtime_video_format").toString().trimmed();
        const QString runtimeAudioFormat = options.value("runtime_audio_format").toString().trimmed();

        if (videoQuality == "Select at Runtime") videoQuality = "best";
        videoCodecSetting = canonicalizeCodecSetting(videoCodecSetting);
        if (videoCodecSetting == "Select at Runtime") videoCodecSetting = "Default";
        audioCodecSetting = canonicalizeCodecSetting(audioCodecSetting);
        if (audioCodecSetting == "Select at Runtime") audioCodecSetting = "Default";

        QString vcodec = getCodecMapping(videoCodecSetting);
        QString acodec = getCodecMapping(audioCodecSetting);
        QString videoFormatSelector = "bestvideo";

        if (videoQuality.toLower() == "best" || videoQuality.toLower() == "worst") {
            videoFormatSelector = videoQuality.toLower() + "video";
        } else {
            videoFormatSelector += QString("[height<=?%1]").arg(videoQuality.split(' ').first().remove('p'));
        }
        if (videoCodecSetting != "Default") videoFormatSelector += QString("[vcodec~='(?i)%1']").arg(vcodec);

        QString audioFormatSelector = "bestaudio";
        if (audioCodecSetting != "Default") audioFormatSelector += QString("[acodec~='(?i)%1']").arg(acodec);

        if (!directFormatOverride.isEmpty()) {
            rawArgs << "-f" << directFormatOverride;
            rawArgs << "--merge-output-format" << requestedExtension;
        } else if (!runtimeVideoFormat.isEmpty() || !runtimeAudioFormat.isEmpty()) {
            const QString selectedVideoFormat = runtimeVideoFormat.isEmpty() ? videoFormatSelector : runtimeVideoFormat;
            const QString selectedAudioFormat = runtimeAudioFormat.isEmpty() ? audioFormatSelector : runtimeAudioFormat;
            rawArgs << "-f" << QString("%1+%2/%1+bestaudio/bestvideo+%2/bestvideo+bestaudio/%1/bestvideo/best").arg(selectedVideoFormat, selectedAudioFormat);
            rawArgs << "--merge-output-format" << requestedExtension;
        } else {
            rawArgs << "-f" << QString("%1+%2/%1+bestaudio/bestvideo+%2/bestvideo+bestaudio/%1/bestvideo/best").arg(videoFormatSelector, audioFormatSelector);
            rawArgs << "--merge-output-format" << requestedExtension;
        }

    } else if (downloadType == "audio") {
        QString audioQuality = options.contains("audio_quality") ? options.value("audio_quality").toString() : configManager->get("Audio", "audio_quality", "Best").toString();
        QString audioCodecSetting = options.contains("audio_codec") ? options.value("audio_codec").toString() : configManager->get("Audio", "audio_codec", "Default").toString();
        finalOutputExtension = configManager->get("Audio", "audio_extension", "mp3").toString();
        const QString directFormatOverride = options.value("format").toString().trimmed();
        const QString runtimeAudioFormat = options.value("runtime_audio_format").toString().trimmed();

        if (audioQuality == "Select at Runtime") audioQuality = "best";
        audioCodecSetting = canonicalizeCodecSetting(audioCodecSetting);
        if (audioCodecSetting == "Select at Runtime") audioCodecSetting = "Default";

        if (!directFormatOverride.isEmpty()) {
            rawArgs << "-f" << directFormatOverride;
        } else if (!runtimeAudioFormat.isEmpty()) {
            rawArgs << "-f" << runtimeAudioFormat;
        } else {
            QString acodec = getCodecMapping(audioCodecSetting);
            QString formatSelector = "bestaudio";

            if (audioQuality.toLower() == "best" || audioQuality.toLower() == "worst") {
                formatSelector = audioQuality.toLower() + "audio";
            } else {
                // Strip any non-digit characters so "320K" or "128 kbps" safely becomes "320" / "128"
                formatSelector += QString("[abr<=?%1]").arg(QString(audioQuality).remove(QRegularExpression("[a-zA-Z\\s]")));
            }
            if (audioCodecSetting != "Default") formatSelector += QString("[acodec~='(?i)%1']").arg(acodec);

            rawArgs << "-f" << formatSelector + "/bestaudio/best";
        }
        rawArgs << "-x";
        if (audioCodecSetting != "Default") rawArgs << "--audio-format" << finalOutputExtension;
        rawArgs << "--audio-quality" << "0";
    }

    // --- Playlist Logic ---
    QString playlistLogic = options.value("playlist_logic", "Ask").toString();
    if (playlistLogic == "Download All (no prompt)") rawArgs << "--yes-playlist";
    else if (playlistLogic == "Download Single (ignore playlist)") rawArgs << "--no-playlist";

    // --- Duplicate Check Override ---
    if (options.value("override_archive", false).toBool()) rawArgs << "--force-download";

    // --- General Options ---
    if (configManager->get("General", "sponsorblock", false).toBool()) {
        rawArgs << "--sponsorblock-remove" << "all";
        if (downloadType == "video" || isLivestream) {
            const bool sponsorBlockSegmentsChecked = options.value("sponsorblock_segments_checked", false).toBool();
            const bool sponsorBlockHasSegments = options.value("sponsorblock_has_segments", false).toBool();
            if (!sponsorBlockSegmentsChecked || sponsorBlockHasSegments) {
                forceKeyframesAtCuts = true;
            } else {
                qInfo() << "YtDlpArgsBuilder: SponsorBlock has no removable segments for this video; skipping forced keyframe cut encoder args.";
            }
        }
    }
    const ProcessUtils::FoundBinary aria2Binary = ProcessUtils::findBinary("aria2c", configManager);
    if (configManager->get("Metadata", "use_aria2c", false).toBool() && aria2Binary.source != "Not Found" && aria2Binary.source != "Invalid Custom") {
        QString aria2cPath = aria2Binary.path;
        QStringList aria2Args;
        aria2Args << "--summary-interval=1";
        const QString referer = siteSpecificReferer(url);
        if (!referer.isEmpty()) {
            aria2Args << QString("--referer=%1").arg(referer);
        }
        rawArgs << "--external-downloader" << aria2cPath;
        rawArgs << "--external-downloader-args" << "aria2c:" + aria2Args.join(' ');
        qInfo() << "YtDlpArgsBuilder: Using aria2c as external downloader (" << aria2cPath << ")";
    } else {
        qInfo() << "YtDlpArgsBuilder: Using native yt-dlp downloader";
    }
    
    QString geoProxy = configManager->get("DownloadOptions", "geo_verification_proxy", "").toString();
    if (!geoProxy.isEmpty()) {
        rawArgs << "--geo-verification-proxy" << geoProxy;
    }

    if (configManager->get("Metadata", "embed_chapters", true).toBool()) rawArgs << "--embed-chapters";
    if (configManager->get("DownloadOptions", "split_chapters", false).toBool()) rawArgs << "--split-chapters";
    if (configManager->get("Metadata", "embed_metadata", true).toBool()) rawArgs << "--embed-metadata";

    // Inject LzyDownloader's internal ID into yt-dlp's metadata engine.
    // This gives users a %(lzy_id)s token for their output templates, guaranteeing
    // unique filenames for independent URLs from "dumb" sites where autonumber fails.
    QString internalId = options.value("id").toString();
    if (!internalId.isEmpty()) {
        rawArgs << "--parse-metadata" << QString("%1:%(lzy_id)s").arg(internalId);
    }

    bool forceSingleAlbum = (downloadType == "audio" && configManager->get("Metadata", "force_playlist_as_album", false).toBool() && options.value("playlist_index", -1).toInt() > 0);
    if (forceSingleAlbum) {
        const QString playlistTitle = options.value("playlist_title").toString().trimmed();
        if (!playlistTitle.isEmpty()) {
            rawArgs << "--parse-metadata" << QString("%1:%(album)s").arg(playlistTitle);
        } else {
            rawArgs << "--parse-metadata" << "playlist_title:%(album)s";
        }
        rawArgs << "--parse-metadata" << "Various Artists:%(album_artist)s";
    }

    const QStringList supportedThumbnailExts = {"mp3", "mkv", "mka", "ogg", "opus", "flac", "m4a", "mp4", "m4v", "mov"};
    
    bool embedThumb = configManager->get("Metadata", "embed_thumbnail", true).toBool();
    bool genFolderJpg = (downloadType == "audio" && configManager->get("Metadata", "generate_folder_jpg", false).toBool() && options.value("playlist_index", -1).toInt() > 0);

    bool canEmbed = embedThumb && supportedThumbnailExts.contains(finalOutputExtension, Qt::CaseInsensitive);
    // We want to write a thumbnail for the UI even if we can't embed it.
    bool shouldWrite = (downloadType == "video" || isLivestream || genFolderJpg);

    if (canEmbed) {
        rawArgs << "--embed-thumbnail";
    } else if (shouldWrite) {
        rawArgs << "--write-thumbnail";
    }

    if (canEmbed || shouldWrite) {
        QStringList ppaArgs;
        if (configManager->get("Metadata", "high_quality_thumbnail", false).toBool()) {
            ppaArgs << "-q:v 0";
        }
        
        // Crop to square if downloading audio
        if (downloadType == "audio" && configManager->get("Metadata", "crop_artwork_to_square", true).toBool()) {
            ppaArgs << "-vf crop=(iw+ih-abs(iw-ih))/2:(iw+ih-abs(iw-ih))/2";
        }

        if (!ppaArgs.isEmpty()) {
            rawArgs << "--ppa" << QString("ThumbnailsConvertor+ffmpeg_o:%1").arg(ppaArgs.join(" "));
        }

        QString convertThumb = configManager->get("Metadata", "convert_thumbnail_to", "jpg").toString();
        if (convertThumb != "None") {
            rawArgs << "--convert-thumbnails" << convertThumb;
        } else if (genFolderJpg && !canEmbed) {
            // If we are only writing for folder.jpg, we must convert to jpg.
            rawArgs << "--convert-thumbnails" << "jpg";
        }
    }

    QString tempPath = configManager->get("Paths", "temporary_downloads_directory").toString();
    if (tempPath.isEmpty()) tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/LzyDownloader";

    // Isolate the temporary directory per-download to prevent concurrent corruption
    // when multiple independent URLs evaluate to the exact same output filename.
    if (!internalId.isEmpty()) {
        tempPath = QDir(tempPath).filePath(internalId);
    }

    if (genFolderJpg) {
        rawArgs << "--write-thumbnail";
        rawArgs << "-o" << QString("thumbnail:%1").arg(QDir(tempPath).filePath(options.value("id").toString() + "_folder.%(ext)s"));
    }

    // --- Subtitles ---
    bool embedSubs = configManager->get("Subtitles", "embed_subtitles", false).toBool();
    bool writeSubs = configManager->get("Subtitles", "write_subtitles", false).toBool();
    if (embedSubs || writeSubs) {
        QString subLangsRaw = configManager->get("Subtitles", "languages", "en").toString();
        QStringList subLangsList = subLangsRaw.split(',', Qt::SkipEmptyParts);
        subLangsList.removeAll("runtime"); // Exclude 'runtime' from being passed to yt-dlp

        if (options.contains("runtime_subtitles")) {
            subLangsList.append(options.value("runtime_subtitles").toString().split(',', Qt::SkipEmptyParts));
            subLangsList.removeDuplicates();
        }

        if (!subLangsList.isEmpty()) {
            if (subLangsList.contains("all")) {
                rawArgs << "--all-subs";
            } else {
                rawArgs << "--sub-langs" << subLangsList.join(',');
            }
            if (configManager->get("Subtitles", "write_auto_subtitles", false).toBool()) rawArgs << "--write-auto-subs";
            if (embedSubs) rawArgs << "--embed-subs";
            if (writeSubs) {
                rawArgs << "--write-subs";
                rawArgs << "--sub-format" << configManager->get("Subtitles", "format", "srt").toString();
            }
        }
    }

    // --- JS Runtime ---
    ProcessUtils::FoundBinary denoBinary = ProcessUtils::findBinary("deno", configManager);
    if (denoBinary.source != "Not Found") {
        rawArgs << "--js-runtimes" << "deno:" + denoBinary.path;
    }

    // --- Filename restrictions ---
    rawArgs << "--windows-filenames";

    // --- Cookies ---
    QString cookiesBrowser = configManager->get("General", "cookies_from_browser", "None").toString();
    if (cookiesBrowser != "None") rawArgs << "--cookies-from-browser" << cookiesBrowser.toLower();

    // --- Custom ffmpeg path ---
    // yt-dlp needs the directory containing ffmpeg and ffprobe
    QString ffmpegPath = ProcessUtils::findBinary("ffmpeg", configManager).path;
    if (ffmpegPath != "ffmpeg") { // Only add if we found a specific path
        rawArgs << "--ffmpeg-location" << QFileInfo(ffmpegPath).path();
    }

    // --- Download Sections ---
    QString downloadSections = options.value("download_sections").toString();
    if (!downloadSections.isEmpty()) {
        rawArgs << "--download-sections" << downloadSections;

        // Preserve the user's requested output container instead of forcing an
        // intermediate MKV remux, which can leave clipped MP4s with bogus
        // duration metadata in players like VLC.
        if (downloadType == "video" || isLivestream) {
            forceKeyframesAtCuts = true;
        }
    }

    if (forceKeyframesAtCuts) {
        appendForcedKeyframeCutArgs(rawArgs, configManager);
    }
    qDebug() << "YtDlpArgsBuilder: forceKeyframesAtCuts before final check:" << forceKeyframesAtCuts; // Add this debug
    // --- Rate Limit ---
    QString rateLimit = options.value("rate_limit", "Unlimited").toString();
    if (rateLimit != "Unlimited") {
        rawArgs << "--limit-rate" << QString(rateLimit).replace(" MB/s", "M").replace(" KB/s", "K").replace(" ", "");
    }

    // --- Output paths ---
    QDir().mkpath(tempPath);
    
    QString outputTemplate;
    if (downloadType == "audio") {
        outputTemplate = configManager->get("General", "output_template_audio").toString();
    } else {
        outputTemplate = configManager->get("General", "output_template_video").toString();
    }
    
    // Fallback to legacy combined setting if the specific ones aren't set yet
    if (outputTemplate.isEmpty()) {
        outputTemplate = configManager->get("General", "output_template").toString();
    }

    if (outputTemplate.isEmpty()) outputTemplate = "%(title)s [%(uploader)s][%(upload_date>%m-%d-%Y)s][%(id)s].%(ext)s";

    QString sectionFilenameLabel = options.value("download_sections_label").toString();
    if (sectionFilenameLabel.isEmpty() && !downloadSections.isEmpty()) {
        sectionFilenameLabel = downloadSections;
    }
    outputTemplate = appendSectionLabelToTemplate(outputTemplate, sectionFilenameLabel);
    if (!sectionFilenameLabel.isEmpty()) {
        qDebug() << "YtDlpArgsBuilder: applied section filename suffix:" << sectionFilenameLabel;
    }

    rawArgs << "-o" << QDir(tempPath).filePath(outputTemplate);

    // --- Site-specific Referer Workarounds ---
    appendSiteSpecificRefererWorkarounds(rawArgs, url);

    // --- Print final filepath ---
    rawArgs << "--print" << "after_move:LZY_FINAL_PATH:%(filepath)s";
    rawArgs << url;

    qDebug() << "YtDlpArgsBuilder::build final rawArgs:" << rawArgs; // Debug statement

    return rawArgs;
}
