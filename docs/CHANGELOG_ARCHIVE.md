# LzyDownloader Changelog Archive

This document archives the historical release notes for LzyDownloader (v1.1.16 and older).
To keep this archive optimized for AI context limits (under 500 lines), highly verbose historical logs for the obsolete Python-based predecessor (v0.0.10 and older) have been truncated.

The format is based on Keep a Changelog, and this project adheres to Semantic Versioning.

## [1.1.16] - 2026-05-01

### Changed
- **Advanced Settings restructuring**: Reworked the Advanced Settings navigation into broader beginner-friendly sections: Essentials, Formats, Download Flow, Files & Tags, and External Tools. Single-setting pages like Authentication are now grouped with related basics, and Download Flow is split into smaller labeled groups for downloader, clipboard/queue, chapters/sections/SponsorBlock, and filename/display behavior.
- **SponsorBlock hardware cut speed**: SponsorBlock-enabled video downloads now preflight the SponsorBlock segment API before starting yt-dlp and skip the expensive `--force-keyframes-at-cuts` / `ModifyChapters+ffmpeg_o` cut-encoder path when the video has no removable segments. Built-in NVENC, Quick Sync, and AMF cut encoder presets are also tuned toward faster single-pass encoding for videos that do need accurate cuts.
- **Headless logging isolation**: Headless/server runs now write timestamped logs under the `Server/` app-data subfolder, matching their settings, API token, and queue-state isolation so Discord-bot activity is not mixed with GUI logs.
- **yt-dlp config isolation**: LzyDownloader now passes `--ignore-config` to yt-dlp commands so user-level yt-dlp config files cannot silently override app-controlled output templates, post-processing, or bot downloads.

### Added
- **Local API Download Type**: The `/enqueue` endpoint now accepts a `type` field in the JSON payload (e.g., `"video"`, `"audio"`, `"gallery"`) to specify the download type, enabling full support for remote clients like the Discord bot.
- **Guided missing-binary setup**: Added a welcome-style setup dialog for missing required binaries. Startup checks, download enqueue checks, and Start-tab format checks now show a checklist with Install, Browse, and Refresh actions instead of only sending users to Advanced Settings.
- **Binary install refresh**: Successful in-app binary installs now refresh detection in the running app instead of automatically restarting LzyDownloader; restart is only suggested if the new tool is still not visible.
- **Hardware encoder support for accurate cuts**: Added Advanced Settings controls for yt-dlp's FFmpeg cut encoder so SponsorBlock/section cuts that require `--force-keyframes-at-cuts` can use NVENC, Quick Sync, AMF, VideoToolbox, or custom FFmpeg output arguments. The encoder dropdown now probes FFmpeg and the local GPU list asynchronously, hiding hardware options that do not apply to the current machine.
- **Live Chat Downloading**: Added support for downloading live chat transcripts from livestreams by selecting the "Live Chat" option in the subtitle language settings.

### Fixed
- **Server settings ownership**: GUI and `--server`/`--headless` launches now share the main app-local `settings.ini` for user preferences, while server queue backups, API tokens, and logs remain isolated under `Server/`. Server mode also starts the API explicitly without flipping the GUI `General/enable_local_api` preference.
- **Playlist sorting scope**: Audio/video playlist sorting rules now rely on app-owned playlist state instead of incidental yt-dlp playlist metadata, preventing "Audio Playlist Downloads" rules from catching normal single audio downloads.
- **Recovered audio finalization**: Audio downloads with portrait thumbnails no longer fail square-crop conversion, and downloads that already produced a final media file now continue finalization when yt-dlp reports a thumbnail/post-processing warning. Temporary sidecar cleanup now compares literal filename stems so titles containing square brackets do not trigger invalid wildcard/regex cleanup warnings.
- **Audio playlist album metadata**: Audio playlist entries now carry the playlist title through expansion, yt-dlp metadata args, sorting, and the app's FFmpeg metadata pass. This prevents playlist tracks from sorting into `{year} - Unknown` folders when per-track yt-dlp metadata omits album context, and ensures the `Force Playlist as Single Album` option embeds a consistent album title and `Various Artists` album artist.
- **Abandoned thumbnail remuxing**: Fixed an issue where livestreams downloaded as MPEG-TS and converted to MP4 would abandon the `.jpg` thumbnail in the temporary directory. The post-processor now automatically detects abandoned thumbnails and embeds them into the final container using FFmpeg before sweeping the temporary folder.
- **Single-item playlist prompts**: Fixed an issue where single-item playlists redundantly prompted the user to choose between "Download All" and "Download Single Item". The prompt is now automatically bypassed.
- **Headless queue-state path alignment**: Queue resume state now uses the same app-local data root as settings and API tokens, preventing headless `downloads_backup.json` from drifting into a different Qt config location.
- **Discord Bot exit-after completion**: Fixed an issue where the Discord bot incorrectly reported "Connection to LzyDownloader lost" when the application successfully closed itself due to the "Exit after all downloads complete" setting. The bot now checks the queue backup file to verify the final completed state.

## [1.1.13] - 2026-04-24

### Changed
- **Active Downloads UI Cleanup**: Merged the redundant "Clear Completed" button into a single unified "Clear Inactive" button on the Active Downloads tab, simplifying the toolbar.
- **Active Downloads toolbar controls**: The tab now uses compact icon-first actions and adds a `Resume All` control for stopped or failed rows alongside the unified clear action.
- **CLI/API downloads are non-interactive**: Direct URL launches and local API enqueues now bypass GUI prompts by forcing archive override for completed items, queueing playlists without asking, skipping runtime picker dialogs, and logging UI-only warnings instead of blocking automation.
- **External Binaries Python-free GUI policy**: The in-app installer no longer offers `pip` for `yt-dlp` or `gallery-dl`. Windows users are now steered toward standalone executable downloads first, while pre-existing advanced-user installs can still be detected and used.
- **Supported Sites Dialog Access**: Removed the top application menu bar ("Help") and moved the "Supported Sites" access to a dedicated button on the Start tab.
- **Initial setup prompt timing**: The first-run completed-downloads directory prompt is now deferred until after the main window is shown, avoiding awkward startup-time modal behavior before the shell finishes initializing.
- **Exit after complete reset**: The "Exit after all downloads complete" toggle now automatically resets to the off state whenever the application restarts instead of persisting between sessions.
- **Post-processing progress animation**: The progress bar now enters an indeterminate (scrolling) animation mode during post-processing and finalizing stages (like FFmpeg merges) instead of freezing statically at 100%, providing clear visual feedback that the application is still working.

### Added
- **Isolated Headless Server Mode**: Running the application with `--headless`, `--server`, or `--background` now automatically routes application data (settings, queue backups, API tokens) to a dedicated `Server` subfolder and branches the single-instance memory lock. This allows a headless background instance (like a Discord bot integration) to run simultaneously alongside the standard GUI without lock conflicts or queue corruption.
- **External Binaries inline descriptions**: Added brief, user-friendly descriptions next to each binary on the External Binaries page explaining what tools like `ffmpeg`, `deno`, and `aria2c` do.
- **Force Playlist as Single Album**: Added a new "Force Playlist as Single Album" toggle in Advanced Settings -> Metadata. When enabled for audio playlist downloads, this forces the `album` metadata tag to the playlist's title and sets a uniform `album_artist` tag ("Various Artists"), ensuring local music players group all tracks into a single album.
- **Supported Sites Dialog**: Added a searchable dialog that lists all supported websites and indicates whether they support Video/Audio (yt-dlp) or Image Galleries (gallery-dl), populated dynamically from the application's extractor lists.

### Fixed
- **vcpkg PCRE2 configure warning**: Added a workspace overlay for the transitive `pcre2` dependency and pointed the CMake presets at it so vcpkg treats `PCRE2_STATIC_RUNTIME` as intentionally maybe-unused with newer PCRE2 CMake files.
- **FFmpeg merge stalls**: FFmpeg-based merge/post-processing wrappers now continuously drain process output and start ffmpeg with `-nostdin`, preventing long merges from hanging when stderr/stdout buffers fill.
- **Windows OpenSSL runtime deployment**: Release packaging now explicitly copies both `libcrypto-3-x64.dll` and `libssl-3-x64.dll` beside `LzyDownloader.exe` so Qt's `qopensslbackend` can initialize HTTPS correctly on clean installs and the app update checker can reach GitHub Releases.
- **Application update prompt wiring**: Fixed the app self-updater so startup now actually runs the GitHub release check, shows the update prompt when a newer installer is available, compares dotted versions numerically instead of lexicographically, and prefers the `LzyDownloader-Setup-*.exe` asset when selecting the installer to download.
- **Mass-stop GUI freeze**: Fixed severe GUI locking when stopping multiple downloads at once (like "Stop All"). Process-tree cleanup (`taskkill`) is now offloaded to a detached background process, and queue evaluation is safely deferred to the end of the event loop, eliminating synchronous cascades that used to freeze the main thread.
- **SponsorBlock A/V desync**: Added `--force-keyframes-at-cuts` to yt-dlp arguments when SponsorBlock removal is enabled for video and livestream downloads, ensuring the audio and video streams remain perfectly in sync after ad segments are cut out.
- **Premature queue completion notification**: Fixed an issue where enqueueing the first download would instantly trigger a "Downloads Complete" desktop notification because saving the concurrency setting temporarily evaluated an empty queue.
- **Pause-triggered completion**: Pausing the last active download no longer incorrectly triggers the "Downloads Complete" notification or the "Exit after all downloads complete" action.
- **Exit-after queue completeness**: Automatic exit now waits for pending playlist-expansion placeholders as well as active and runnable queued downloads, so "Exit after all downloads complete" will not shut down while items are still stuck in the "Checking for playlist..." stage.
- **Exit-after delayed shutdown guard**: The 2-second post-queue cleanup delay now re-checks the live queued/active counts before calling `quit()`, preventing stale queue-finished signals from closing the app after new downloads have appeared or resumed.
- **Windows installer startup fix (`zlib1.dll`)**: Fixed the NSIS packaging path so the installer no longer deletes `zlib1.dll` from the deployed app directory. `Qt6Network.dll` depends on that runtime on Windows, so removing it caused fresh installs to fail at launch with "`zlib1.dll` was not found".
- **Auto-paste URL replacement**: Clipboard-driven auto-paste now replaces the Start tab URL box contents instead of appending a new copied URL onto an existing one, preventing accidental multi-line mashups when auto-paste is enabled.
- **Sorting-rule metadata consistency**: Sorting rules and subfolder tokens now resolve metadata keys more defensively across fresh, playlist, audio, and resumed downloads. Queue backups persist per-item metadata, the finalizer no longer aborts just because a fallback `.info.json` is unavailable, and album/playlist/uploader-style fields now use alias-aware lookup so matching is more consistent after resumes and across extractor metadata variations.
- **Dynamic Max Concurrent rescheduling**: Changing `Max Concurrent` while downloads are already queued now immediately starts enough waiting items to fill any newly opened slots, while lowering the limit leaves current downloads running and simply delays future starts until capacity is available again.
- **Start tab settings persistence**: Fixed an issue where changing operational controls like Max Concurrent, Playlist Logic, and Rate Limit on the Start tab only updated the UI visually but did not save to disk. These settings now save instantly, trigger immediate backend reactions, and persist across app restarts.
- **Concurrency startup cap**: Enforced the session restart cap for Max Concurrent downloads. Users can temporarily set up to 8 concurrent threads during a session, but the application will automatically cap this back down to 4 upon restart to prevent accidental aggressive spam.
- **Playlist placeholder replacement flow**: Video/audio playlist URLs now replace the initial "Checking for playlist..." placeholder correctly. Single-video URLs update that existing row in place, true playlists remove the placeholder and enqueue one progress bar per expanded entry, and the default "Ask" playlist prompt is wired back through `MainWindow` so it follows the same replacement behavior.
- **Playlist child target resolution**: Expanded playlist items now prefer canonical entry page URLs over flat-playlist `url` values when available, and yt-dlp command lines now place playlist-control flags before the final media URL so per-entry jobs do not accidentally keep playlist semantics.
- **Playlist placeholder start race**: Video/audio placeholder rows are now marked as pending playlist expansion before they enter the queue, preventing the first placeholder item from starting a real playlist download before expansion finishes and causing the first GUI item to consume the whole playlist.
- **Playlist item title hydration**: Expanded playlist rows now carry the flat-playlist title into the queue UI immediately, so progress bars no longer have to wait for later `info.json` parsing before showing the correct video title.
- **Single-item queue stalling**: Fixed an issue where single videos or 1-item playlists would remain permanently stuck in the "Queued" state after expansion because the download queue was not signaled to wake up after the in-place UI update.
- **Sorting rule evaluation**: Fixed a mismatch between human-readable UI labels (e.g., "Audio Playlist Downloads") and internal backend keys, ensuring sorting conditions apply correctly.
- **Playlist sorting recognition**: Fixed playlist rules falling back to single-item rules by ensuring `is_playlist` and `playlist_title` flags are consistently injected into metadata.
- **Resumed download metadata**: Fixed an issue where `yt-dlp` skipping an already-downloaded file caused the app to prematurely delete the `info.json` file, which broke sorting tokens like `{album}` and caused fallbacks to `Unknown Year - Unknown`.
- **Single-video expansion bypass**: Fixed an issue where single videos with known errors (e.g., Private, Geo-Restricted, Scheduled Livestreams) would fail silently during the initial playlist-check phase. These URLs now safely bypass the expansion failure and enter the main queue so the proper error popups or "Wait for Video" prompts can be displayed.
- **Livestream pre-wait thumbnails**: Fixed a bug where background thumbnail fetching for upcoming livestreams would fail to render or result in 0-byte files. The metadata fetcher now properly follows HTTP redirects, automatically detects `.webp` image extensions, and cleans up empty files.
- **Premature exit-after completion**: Fixed a bug where the application would immediately close on launch if "Exit after all downloads complete" was enabled and the queue was empty.
- **Playlist metadata carry-through**: Single-item playlists, expanded playlist entries, resumed items, and fallback finalization now preserve `playlist_title`, `is_playlist`, and other core metadata so sorting rules and cleanup stay consistent.
- **Bilibili HTTP 412 Error**: Added Bilibili referer handling to yt-dlp arguments, using the source video URL for `bilibili.com` media requests and the `bilibili.tv` site root for international URLs, to fix "HTTP Error 412: Precondition Failed" download errors.
- **Bilibili aria2c HTTP 412 Error**: Bilibili downloads that use aria2c now pass the same source-aware referer to aria2c itself, fixing cases where yt-dlp validation succeeded but the external downloader's media request was rejected with HTTP 412.
- **Niconico HTTP 403 Error**: Added `--referer <URL>` to yt-dlp arguments for all Niconico URLs (`nicovideo.jp`, `nico.ms`) to fix "HTTP Error 403: Forbidden" download errors.
- **Pre-download validation failures**: Centralized site-specific workarounds (e.g., Bilibili/Niconico referer) into `YtDlpArgsBuilder` and applied them to the pre-download URL validation step, including the standalone `UrlValidator` path, fixing cases where valid URLs were incorrectly rejected before being queued.

## [1.1.4] - 04-18-2026

### Added
- **External Binaries version/update controls**: The External Binaries page now shows live per-binary version strings and hosts the inline `Update` actions for `yt-dlp` and `gallery-dl`, alongside install methods that include official-download options and direct standalone `curl` downloads where available.
- **Windows debug console toggle**: A new `Show Debug Console` option in Advanced Settings can show or hide the app-owned console window at runtime on Windows builds.

### Changed
- **Dynamic missing binary labels**: The Start tab now automatically refreshes its `(missing binaries)` warnings whenever you switch back to it, ensuring that resolving issues in the External Binaries settings is immediately reflected without requiring an app restart.
- **Start tab format selection**: Renamed "View Formats" to "View Video/Audio Formats" and added explicit tooltips to clarify that this feature uses yt-dlp and is not supported for gallery-dl links.
- **Immediate download action feedback**: Clicking Pause, Resume, or Cancel on an active download now immediately updates the item's status label (e.g., "Pausing...", "Cancelling...") and disables the relevant buttons while waiting for the background worker to process the request.
- **Clipboard auto-paste debounce**: Auto-paste now uses a short 500 ms debounce instead of a 5-second lockout, while still preventing duplicate enqueues through queue-state checks.
- **Updater repository lookup**: App update checks now try the lowercase `vincentwetzel/lzy-downloader` repo first and then fall back to legacy repository API URLs if needed.
- **Binary-management layout**: The old dedicated Updates page has been removed; external binary version display and downloader update actions now live directly inside the External Binaries page.
- **Shutdown prompt wording**: Closing the app now warns specifically about queued or active downloads and explains that queue state will be saved and resumed on the next launch.

### Fixed
- **Stopped-download persistence**: Failed, cancelled, and stopped downloads now retain their latest temp paths and cleanup candidates in `downloads_backup.json`, so resume and `Clear Temp Files` continue to work after restarting the app.
- **Configured-binary version checks**: The yt-dlp/gallery-dl startup version probes and inline External Binaries version labels now use the configured or auto-detected executable instead of assuming a bundled app-directory binary.
- **Package-managed updater safety**: The built-in yt-dlp and gallery-dl updaters now refuse to overwrite package-managed installations and instead direct the user back to their package manager or the External Binaries page.
- **aria2 fallback on enqueue**: If `aria2c` is enabled in settings but no longer installed, the app now auto-reverts to yt-dlp's native downloader before queueing the download instead of letting the job fail immediately.
- **Stable yt-dlp guidance**: When the app detects a stable three-part yt-dlp version, it now explains why nightly builds are preferred and offers direct shortcuts to the relevant settings pages.

## [1.1.1] - 04-03-2026

### Fixed
- **Windows post-install relaunch path**: Fixed the built-in binary installer restart flow on Windows after package-manager installs such as `winget ffmpeg`. The app now normalizes the concrete `LzyDownloader.exe` path, strips any Windows `\\?\` prefix, and relaunches through a delayed PowerShell-to-Explorer handoff instead of `cmd /c start`, preventing the invalid `\\` target error during auto-restart.
- **gallery-dl Winget install ambiguity**: Fixed the built-in gallery-dl installer on Windows so Winget commands now use explicit package IDs (`mikf.gallery-dl` and `mikf.gallery-dl.Nightly`) instead of the ambiguous `gallery-dl` name that can match multiple packages and fail before install starts.
- **Windows shutdown cleanup for orphaned mergers**: All tracked `QProcess` downloads and helpers now join a Windows job object with kill-on-close semantics, so quitting the app while `yt-dlp`, `ffmpeg`, `aria2c`, or related helpers are still active no longer leaves orphaned background processes behind. The standalone ffmpeg merge worker now also uses the shared process environment/tracking path and cancels via the same process-tree termination helper.
- **Advanced Settings downloader wiring audit**: Fixed several settings paths that had drifted out of sync with the real yt-dlp arguments. `YtDlpArgsBuilder` now translates codec preferences into selectors that match real stream aliases such as `avc1`, `hev1`, and `mp4a`, so choosing `H.264 (AVC)` no longer falls through to AV1-heavy site defaults. The builder also now respects the `Restrict filenames` toggle, honors direct `format` overrides emitted by the runtime format picker, and merges runtime-selected audio format IDs into video downloads instead of silently dropping them.
- **FFmpeg version string display**: Cleaned up the version string extraction for FFmpeg and ffprobe so the UI displays a concise version number or build date instead of the verbose build configuration string.
- **Clickable source links in yt-dlp error dialogs**: User-facing error popups now render rich text and include the failing source URL when one is available, making it easier to inspect, retry, or open the original page while still showing the cleaned technical details.
- **Transient helper process cleanup**: Process-tree shutdown now also covers utility `QProcess` lifetimes used by cookie validation, output-template validation, updater runs, and other UI-spawned helpers, preventing stray background tools from surviving cancellation or app exit.
- **Legacy codec label drift**: The default saved video codec label is now the canonical `H.264 (AVC)`, and the video settings page normalizes older `H.264`/`H.265` values when loading existing configs so the UI stays aligned with the downloader mappings.
- **Audio-stage fallback from stream size**: When yt-dlp delays a clean filename handoff, `YtDlpWorker` now also matches the active stream by emitted total size before falling back to generic progress-reset ordering, which helps the audio stage switch at the right time even for ambiguous temp names.
- **Audio-only WebM stream labeling**: `YtDlpWorker` now prefers yt-dlp `format_id` values embedded in temp filenames (for example `.f251.webm.part`) before falling back to container extensions, preventing audio-only WebM/Opus transfers from being mislabeled as video.
- **Audio-only extract fallback for ambiguous containers**: When yt-dlp is running an explicit extract-audio job and only exposes an intermediate container path such as `.webm` or `.mp4`, `YtDlpWorker` now reports `Downloading audio stream...` instead of assuming those temporary containers always mean video.
- **Missing `requested_downloads` fallback for audio/video stage labels**: Some yt-dlp runs do not populate `requested_downloads` in `info.json`, so `YtDlpWorker` now seeds stream order from `[info] ... Downloading 1 format(s): 399+251-13` and also trusts aria2 command-line clues like `itag=251` plus `mime=audio/webm` to switch the GUI from video to audio at the correct handoff.
- **Late info.json stream-label regression**: When `info.json` arrives after stderr has already identified the active streams, `YtDlpWorker` now preserves the inferred stream order if `requested_downloads` is empty instead of clearing it and accidentally flipping the audio phase back to `Downloading video stream...`.
- **Exit cleanup for background tools**: Closing the app now triggers an explicit `DownloadManager` shutdown that terminates descendant process trees for active downloads and helpers, preventing orphaned `ffmpeg`, `aria2c`, and similar child processes from surviving after the window exits.
- **yt-dlp/aria2 progress regression after small overall bar update**: Restored robust progress parsing by treating pre-download extraction as indeterminate instead of leaving the UI parked at 0%, accepting slightly noisier aria2 summary lines, and using aria2 `FILE:` output to keep active stream tracking aligned with the file actually being transferred.
- **Authentication Settings hardcoded binary paths**: Fixed the browser cookies tester so it uses the user-configured or dynamically detected `yt-dlp` and `deno` paths instead of assuming they are always in a bundled `bin/` folder.
- **Package manager installation PATH detection**: The built-in package-manager installer (winget, scoop, pip) now automatically restarts LzyDownloader through the OS shell upon success, ensuring the app instantly inherits the newly updated system PATH and detects the installed tool without requiring manual intervention.

### Added
- **Strict dependency enforcement**: The application now actively prevents users from queuing video/audio downloads if `yt-dlp`, `ffmpeg`, `ffprobe`, or `deno` are missing, and blocks gallery downloads if `gallery-dl`, `ffmpeg`, or `ffprobe` are missing. When a missing binary is detected during enqueue, a helpful popup directs the user to the External Binaries settings page to resolve the issue.
- **Advanced Metadata Settings**: Added new toggles for "Crop audio thumbnails to square" and "Generate folder.jpg for audio playlists" in the Metadata settings page. Thumbnails can now also be explicitly converted to `jpg` or `png` formats.
- **Metadata UI Decoupling**: Extracted metadata and thumbnail settings from `AdvancedSettingsTab` into a dedicated `MetadataPage` component to improve modularity.
- **Source layout cleanup**: Moved the Start tab helper classes into `src/ui/start_tab/` and grouped the aria2 download pipeline into `src/core/download_pipeline/`. The aria2 RPC wrapper, yt-dlp download-info extractor, and ffmpeg merge worker were also renamed to `Aria2RpcClient`, `YtDlpDownloadInfoExtractor`, and `FfmpegMuxer` to make their roles clearer and reduce collisions with other core classes.
- **Open folder buttons on Active Downloads tab**: Added "Open Temporary Folder" and "Open Downloads Folder" buttons to the Active Downloads tab toolbar, providing quick access to download directories without switching to the Start tab. Both buttons include tooltips and show warning dialogs if directories are not configured.
- **Download Sections Support**: Added an option in Advanced Settings to download specific sections of a video by time range or chapter name. When enabled, a dialog appears before downloading, allowing the user to define one or more sections to be downloaded. This dialog also includes helper instructions on how to disable the feature if enabled accidentally.
- **External Downloader dropdown in Advanced Settings**: Replaced the "External Downloader (aria2c)" toggle switch with a dropdown selector offering "yt-dlp (default)" and "aria2c" options. The setting automatically hides when aria2c is not installed/discovered. Default changed from aria2c to yt-dlp to align with the unbundled binary model, ensuring users without aria2c installed won't encounter download failures.
- **yt-dlp error popup notifications**: The application now detects specific yt-dlp error messages during downloads and displays user-friendly popup dialogs with clear explanations. Supported error types include:
- **Refactored StartTab into smaller, more focused classes**: `StartTab.cpp` has been broken down into `StartTabUrlHandler` (manages URL input, clipboard, auto-switching), `StartTabDownloadActions` (handles download button, type changes, format checking, folder opening), and `StartTabCommandPreviewUpdater` (updates command preview). This improves modularity and maintainability.
# LzyDownloader Changelog Archive

This document archives historical changes for LzyDownloader (v1.1.16 and older, including older Python-based builds).

## [1.1.16] - 2026-05-01

### Changed
- **Advanced Settings restructuring**: Reworked the Advanced Settings navigation into broader beginner-friendly sections: Essentials, Formats, Download Flow, Files & Tags, and External Tools. Single-setting pages like Authentication are now grouped with related basics, and Download Flow is split into smaller labeled groups for downloader, clipboard/queue, chapters/sections/SponsorBlock, and filename/display behavior.
- **SponsorBlock hardware cut speed**: SponsorBlock-enabled video downloads now preflight the SponsorBlock segment API before starting yt-dlp and skip the expensive `--force-keyframes-at-cuts` / `ModifyChapters+ffmpeg_o` cut-encoder path when the video has no removable segments. Built-in NVENC, Quick Sync, and AMF cut encoder presets are also tuned toward faster single-pass encoding for videos that do need accurate cuts.
- **Headless logging isolation**: Headless/server runs now write timestamped logs under the `Server/` app-data subfolder, matching their settings, API token, and queue-state isolation so Discord-bot activity is not mixed with GUI logs.
- **yt-dlp config isolation**: LzyDownloader now passes `--ignore-config` to yt-dlp commands so user-level yt-dlp config files cannot silently override app-controlled output templates, post-processing, or bot downloads.

### Added
- **Local API Download Type**: The `/enqueue` endpoint now accepts a `type` field in the JSON payload (e.g., `"video"`, `"audio"`, `"gallery"`) to specify the download type, enabling full support for remote clients like the Discord bot.
- **Guided missing-binary setup**: Added a welcome-style setup dialog for missing required binaries. Startup checks, download enqueue checks, and Start-tab format checks now show a checklist with Install, Browse, and Refresh actions instead of only sending users to Advanced Settings.
- **Binary install refresh**: Successful in-app binary installs now refresh detection in the running app instead of automatically restarting LzyDownloader; restart is only suggested if the new tool is still not visible.
- **Hardware encoder support for accurate cuts**: Added Advanced Settings controls for yt-dlp's FFmpeg cut encoder so SponsorBlock/section cuts that require `--force-keyframes-at-cuts` can use NVENC, Quick Sync, AMF, VideoToolbox, or custom FFmpeg output arguments. The encoder dropdown now probes FFmpeg and the local GPU list asynchronously, hiding hardware options that do not apply to the current machine.
- **Live Chat Downloading**: Added support for downloading live chat transcripts from livestreams by selecting the "Live Chat" option in the subtitle language settings.

### Fixed
- **Server settings ownership**: GUI and `--server`/`--headless` launches now share the main app-local `settings.ini` for user preferences, while server queue backups, API tokens, and logs remain isolated under `Server/`. Server mode also starts the API explicitly without flipping the GUI `General/enable_local_api` preference.
- **Playlist sorting scope**: Audio/video playlist sorting rules now rely on app-owned playlist state instead of incidental yt-dlp playlist metadata, preventing "Audio Playlist Downloads" rules from catching normal single audio downloads.
- **Recovered audio finalization**: Audio downloads with portrait thumbnails no longer fail square-crop conversion, and downloads that already produced a final media file now continue finalization when yt-dlp reports a thumbnail/post-processing warning. Temporary sidecar cleanup now compares literal filename stems so titles containing square brackets do not trigger invalid wildcard/regex cleanup warnings.
- **Audio playlist album metadata**: Audio playlist entries now carry the playlist title through expansion, yt-dlp metadata args, sorting, and the app's FFmpeg metadata pass. This prevents playlist tracks from sorting into `{year} - Unknown` folders when per-track yt-dlp metadata omits album context, and ensures the `Force Playlist as Single Album` option embeds a consistent album title and `Various Artists` album artist.
- **Abandoned thumbnail remuxing**: Fixed an issue where livestreams downloaded as MPEG-TS and converted to MP4 would abandon the `.jpg` thumbnail in the temporary directory. The post-processor now automatically detects abandoned thumbnails and embeds them into the final container using FFmpeg before sweeping the temporary folder.
- **Single-item playlist prompts**: Fixed an issue where single-item playlists redundantly prompted the user to choose between "Download All" and "Download Single Item". The prompt is now automatically bypassed.
- **Headless queue-state path alignment**: Queue resume state now uses the same app-local data root as settings and API tokens, preventing headless `downloads_backup.json` from drifting into a different Qt config location.
- **Discord bridge request cleanup**: Local Discord bridge posts now use explicit timeouts and let replies clean themselves up through Qt deferred deletion.
- **Discord bridge callback lifetime**: Discord webhook request callbacks are now anchored to the main window context so queued network completions cannot outlive the UI object that owns bridge state.
- **yt-dlp metadata and thumbnail cleanup**: yt-dlp workers now flush trailing stdout/stderr through the shared UTF-8 line parser, recover `info.json` by scanning the UUID temp directory if the expected path is stale, move wait-state thumbnails into managed cleanup scope when possible, share validated `info.json` parsing and file cleanup helpers, clean empty UUID temp folders through the same fallback path on finish/error, and surface file cleanup/write failures instead of silently ignoring them.

## [1.1.13] - 2026-04-24

### Changed
- **Active Downloads UI Cleanup**: Merged the redundant "Clear Completed" button into a single unified "Clear Inactive" button on the Active Downloads tab, simplifying the toolbar.
- **Active Downloads toolbar controls**: The tab now uses compact icon-first actions and adds a `Resume All` control for stopped or failed rows alongside the unified clear action.
- **CLI/API downloads are non-interactive**: Direct URL launches and local API enqueues now bypass GUI prompts by forcing archive override for completed items, queueing playlists without asking, skipping runtime picker dialogs, and logging UI-only warnings instead of blocking automation.
- **External Binaries Python-free GUI policy**: The in-app installer no longer offers `pip` for `yt-dlp` or `gallery-dl`. Windows users are now steered toward standalone executable downloads first, while pre-existing advanced-user installs can still be detected and used.
- **Supported Sites Dialog Access**: Removed the top application menu bar ("Help") and moved the "Supported Sites" access to a dedicated button on the Start tab.
- **Initial setup prompt timing**: The first-run completed-downloads directory prompt is now deferred until after the main window is shown, avoiding awkward startup-time modal behavior before the shell finishes initializing.
- **Exit after complete reset**: The "Exit after all downloads complete" toggle now automatically resets to the off state whenever the application restarts instead of persisting between sessions.
- **Post-processing progress animation**: The progress bar now enters an indeterminate (scrolling) animation mode during post-processing and finalizing stages (like FFmpeg merges) instead of freezing statically at 100%, providing clear visual feedback that the application is still working.

### Added
- **Isolated Headless Server Mode**: Running the application with `--headless`, `--server`, or `--background` now automatically routes application data (settings, queue backups, API tokens) to a dedicated `Server` subfolder and branches the single-instance memory lock. This allows a headless background instance (like a Discord bot integration) to run simultaneously alongside the standard GUI without lock conflicts or queue corruption.
- **External Binaries inline descriptions**: Added brief, user-friendly descriptions next to each binary on the External Binaries page explaining what tools like `ffmpeg`, `deno`, and `aria2c` do.
- **Force Playlist as Single Album**: Added a new "Force Playlist as Single Album" toggle in Advanced Settings -> Metadata. When enabled for audio playlist downloads, this forces the `album` metadata tag to the playlist's title and sets a uniform `album_artist` tag ("Various Artists"), ensuring local music players group all tracks into a single album.
- **Supported Sites Dialog**: Added a searchable dialog that lists all supported websites and indicates whether they support Video/Audio (yt-dlp) or Image Galleries (gallery-dl), populated dynamically from the application's extractor lists.

### Fixed
- **vcpkg PCRE2 configure warning**: Added a workspace overlay for the transitive `pcre2` dependency and pointed the CMake presets at it so vcpkg treats `PCRE2_STATIC_RUNTIME` as intentionally maybe-unused with newer PCRE2 CMake files.
- **FFmpeg merge stalls**: FFmpeg-based merge/post-processing wrappers now continuously drain process output and start ffmpeg with `-nostdin`, preventing long merges from hanging when stderr/stdout buffers fill.
- **Windows OpenSSL runtime deployment**: Release packaging now explicitly copies both `libcrypto-3-x64.dll` and `libssl-3-x64.dll` beside `LzyDownloader.exe` so Qt's `qopensslbackend` can initialize HTTPS correctly on clean installs and the app update checker can reach GitHub Releases.
- **Application update prompt wiring**: Fixed the app self-updater so startup now actually runs the GitHub release check, shows the update prompt when a newer installer is available, compares dotted versions numerically instead of lexicographically, and prefers the `LzyDownloader-Setup-*.exe` asset when selecting the installer to download.
- **Mass-stop GUI freeze**: Fixed severe GUI locking when stopping multiple downloads at once (like "Stop All"). Process-tree cleanup (`taskkill`) is now offloaded to a detached background process, and queue evaluation is safely deferred to the end of the event loop, eliminating synchronous cascades that used to freeze the main thread.
- **SponsorBlock A/V desync**: Added `--force-keyframes-at-cuts` to yt-dlp arguments when SponsorBlock removal is enabled for video and livestream downloads, ensuring the audio and video streams remain perfectly in sync after ad segments are cut out.
- **Premature queue completion notification**: Fixed an issue where enqueueing the first download would instantly trigger a "Downloads Complete" desktop notification because saving the concurrency setting temporarily evaluated an empty queue.
- **Pause-triggered completion**: Pausing the last active download no longer incorrectly triggers the "Downloads Complete" notification or the "Exit after all downloads complete" action.
- **Exit-after queue completeness**: Automatic exit now waits for pending playlist-expansion placeholders as well as active and runnable queued downloads, so "Exit after all downloads complete" will not shut down while items are still stuck in the "Checking for playlist..." stage.
- **Exit-after delayed shutdown guard**: The 2-second post-queue cleanup delay now re-checks the live queued/active counts before calling `quit()`, preventing stale queue-finished signals from closing the app after new downloads have appeared or resumed.
- **Windows installer startup fix (`zlib1.dll`)**: Fixed the NSIS packaging path so the installer no longer deletes `zlib1.dll` from the deployed app directory. `Qt6Network.dll` depends on that runtime on Windows, so removing it caused fresh installs to fail at launch with "`zlib1.dll` was not found".
- **Auto-paste URL replacement**: Clipboard-driven auto-paste now replaces the Start tab URL box contents instead of appending a new copied URL onto an existing one, preventing accidental multi-line mashups when auto-paste is enabled.
- **Sorting-rule metadata consistency**: Sorting rules and subfolder tokens now resolve metadata keys more defensively across fresh, playlist, audio, and resumed downloads. Queue backups persist per-item metadata, the finalizer no longer aborts just because a fallback `.info.json` is unavailable, and album/playlist/uploader-style fields now use alias-aware lookup so matching is more consistent after resumes and across extractor metadata variations.
- **Dynamic Max Concurrent rescheduling**: Changing `Max Concurrent` while downloads are already queued now immediately starts enough waiting items to fill any newly opened slots, while lowering the limit leaves current downloads running and simply delays future starts until capacity is available again.
- **Start tab settings persistence**: Fixed an issue where changing operational controls like Max Concurrent, Playlist Logic, and Rate Limit on the Start tab only updated the UI visually but did not save to disk. These settings now save instantly, trigger immediate backend reactions, and persist across app restarts.
- **Concurrency startup cap**: Enforced the session restart cap for Max Concurrent downloads. Users can temporarily set up to 8 concurrent threads during a session, but the application will automatically cap this back down to 4 upon restart to prevent accidental aggressive spam.
- **Playlist placeholder replacement flow**: Video/audio playlist URLs now replace the initial "Checking for playlist..." placeholder correctly. Single-video URLs update that existing row in place, true playlists remove the placeholder and enqueue one progress bar per expanded entry, and the default "Ask" playlist prompt is wired back through `MainWindow` so it follows the same replacement behavior.
- **Playlist child target resolution**: Expanded playlist items now prefer canonical entry page URLs over flat-playlist `url` values when available, and yt-dlp command lines now place playlist-control flags before the final media URL so per-entry jobs do not accidentally keep playlist semantics.
- **Playlist placeholder start race**: Video/audio placeholder rows are now marked as pending playlist expansion before they enter the queue, preventing the first placeholder item from starting a real playlist download before expansion finishes and causing the first GUI item to consume the whole playlist.
- **Playlist item title hydration**: Expanded playlist rows now carry the flat-playlist title into the queue UI immediately, so progress bars no longer have to wait for later `info.json` parsing before showing the correct video title.
- **Single-item queue stalling**: Fixed an issue where single videos or 1-item playlists would remain permanently stuck in the "Queued" state after expansion because the download queue was not signaled to wake up after the in-place UI update.
- **Sorting rule evaluation**: Fixed a mismatch between human-readable UI labels (e.g., "Audio Playlist Downloads") and internal backend keys, ensuring sorting conditions apply correctly.
- **Playlist sorting recognition**: Fixed playlist rules falling back to single-item rules by ensuring `is_playlist` and `playlist_title` flags are consistently injected into metadata.
- **Resumed download metadata**: Fixed an issue where `yt-dlp` skipping an already-downloaded file caused the app to prematurely delete the `info.json` file, which broke sorting tokens like `{album}` and caused fallbacks to `Unknown Year - Unknown`.
- **Single-video expansion bypass**: Fixed an issue where single videos with known errors (e.g., Private, Geo-Restricted, Scheduled Livestreams) would fail silently during the initial playlist-check phase. These URLs now safely bypass the expansion failure and enter the main queue so the proper error popups or "Wait for Video" prompts can be displayed.
- **Livestream pre-wait thumbnails**: Fixed a bug where background thumbnail fetching for upcoming livestreams would fail to render or result in 0-byte files. The metadata fetcher now properly follows HTTP redirects, automatically detects `.webp` image extensions, and cleans up empty files.
- **Premature exit-after completion**: Fixed a bug where the application would immediately close on launch if "Exit after all downloads complete" was enabled and the queue was empty.
- **Playlist metadata carry-through**: Single-item playlists, expanded playlist entries, resumed items, and fallback finalization now preserve `playlist_title`, `is_playlist`, and other core metadata so sorting rules and cleanup stay consistent.
- **Bilibili HTTP 412 Error**: Added Bilibili referer handling to yt-dlp arguments, using the source video URL for `bilibili.com` media requests and the `bilibili.tv` site root for international URLs, to fix "HTTP Error 412: Precondition Failed" download errors.
- **Bilibili aria2c HTTP 412 Error**: Bilibili downloads that use aria2c now pass the same source-aware referer to aria2c itself, fixing cases where yt-dlp validation succeeded but the external downloader's media request was rejected with HTTP 412.
- **Niconico HTTP 403 Error**: Added `--referer <URL>` to yt-dlp arguments for all Niconico URLs (`nicovideo.jp`, `nico.ms`) to fix "HTTP Error 403: Forbidden" download errors.
- **Pre-download validation failures**: Centralized site-specific workarounds (e.g., Bilibili/Niconico referer) into `YtDlpArgsBuilder` and applied them to the pre-download URL validation step, including the standalone `UrlValidator` path, fixing cases where valid URLs were incorrectly rejected before being queued.

## [1.1.4] - 04-18-2026

### Added
- **External Binaries version/update controls**: The External Binaries page now shows live per-binary version strings and hosts the inline `Update` actions for `yt-dlp` and `gallery-dl`, alongside install methods that include official-download options and direct standalone `curl` downloads where available.
- **Windows debug console toggle**: A new `Show Debug Console` option in Advanced Settings can show or hide the app-owned console window at runtime on Windows builds.

### Changed
- **Dynamic missing binary labels**: The Start tab now automatically refreshes its `(missing binaries)` warnings whenever you switch back to it, ensuring that resolving issues in the External Binaries settings is immediately reflected without requiring an app restart.
- **Start tab format selection**: Renamed "View Formats" to "View Video/Audio Formats" and added explicit tooltips to clarify that this feature uses yt-dlp and is not supported for gallery-dl links.
- **Immediate download action feedback**: Clicking Pause, Resume, or Cancel on an active download now immediately updates the item's status label (e.g., "Pausing...", "Cancelling...") and disables the relevant buttons while waiting for the background worker to process the request.
- **Clipboard auto-paste debounce**: Auto-paste now uses a short 500 ms debounce instead of a 5-second lockout, while still preventing duplicate enqueues through queue-state checks.
- **Updater repository lookup**: App update checks now try the lowercase `vincentwetzel/lzy-downloader` repo first and then fall back to legacy repository API URLs if needed.
- **Binary-management layout**: The old dedicated Updates page has been removed; external binary version display and downloader update actions now live directly inside the External Binaries page.
- **Shutdown prompt wording**: Closing the app now warns specifically about queued or active downloads and explains that queue state will be saved and resumed on the next launch.

### Fixed
- **Stopped-download persistence**: Failed, cancelled, and stopped downloads now retain their latest temp paths and cleanup candidates in `downloads_backup.json`, so resume and `Clear Temp Files` continue to work after restarting the app.
- **Configured-binary version checks**: The yt-dlp/gallery-dl startup version probes and inline External Binaries version labels now use the configured or auto-detected executable instead of assuming a bundled app-directory binary.
- **Package-managed updater safety**: The built-in yt-dlp and gallery-dl updaters now refuse to overwrite package-managed installations and instead direct the user back to their package manager or the External Binaries page.
- **aria2c fallback on enqueue**: If `aria2c` is enabled in settings but no longer installed, the app now auto-reverts to yt-dlp's native downloader before queueing the download instead of letting the job fail immediately.
- **Stable yt-dlp guidance**: When the app detects a stable three-part yt-dlp version, it now explains why nightly builds are preferred and offers direct shortcuts to the relevant settings pages.

## [1.1.1] - 04-03-2026

### Fixed
- **Windows post-install relaunch path**: Fixed the built-in binary installer restart flow on Windows after package-manager installs such as `winget ffmpeg`. The app now normalizes the concrete `LzyDownloader.exe` path, strips any Windows `\\?\` prefix, and relaunches through a delayed PowerShell-to-Explorer handoff instead of `cmd /c start`, preventing the invalid `\\` target error during auto-restart.
- **gallery-dl Winget install ambiguity**: Fixed the built-in gallery-dl installer on Windows so Winget commands now use explicit package IDs (`mikf.gallery-dl` and `mikf.gallery-dl.Nightly`) instead of the ambiguous `gallery-dl` name that can match multiple packages and fail before install starts.
- **Windows shutdown cleanup for orphaned mergers**: All tracked `QProcess` downloads and helpers now join a Windows job object with kill-on-close semantics, so quitting the app while `yt-dlp`, `ffmpeg`, `aria2c`, or related helpers are still active no longer leaves orphaned background processes behind. The standalone ffmpeg merge worker now also uses the shared process environment/tracking path and cancels via the same process-tree termination helper.
- **Advanced Settings downloader wiring audit**: Fixed several settings paths that had drifted out of sync with the real yt-dlp arguments. `YtDlpArgsBuilder` now translates codec preferences into selectors that match real stream aliases such as `avc1`, `hev1`, and `hvc1`, so choosing `H.264 (AVC)` no longer falls through to AV1-heavy site defaults. The builder also now respects the `Restrict filenames` toggle, honors direct `format` overrides emitted by the runtime format picker, and merges runtime-selected audio format IDs into video downloads instead of silently dropping them.
- **FFmpeg version string display**: Cleaned up the version string extraction for FFmpeg and ffprobe so the UI displays a concise version number or build date instead of the verbose build configuration string.
- **Clickable source links in yt-dlp error dialogs**: User-facing error popups now render rich text and include the failing source URL when one is available, making it easier to inspect, retry, or open the original page while still showing the cleaned technical details.
- **Transient helper process cleanup**: Process-tree shutdown now also covers utility `QProcess` lifetimes used by cookie validation, output-template validation, updater runs, and other UI-spawned helpers, preventing stray background tools from surviving cancellation or app exit.
- **Legacy codec label drift**: The default saved video codec label is now the canonical `H.264 (AVC)`, and the video settings page normalizes older `H.264`/`H.265` values when loading existing configs so the UI stays aligned with the downloader mappings.
- **Audio-stage fallback from stream size**: When yt-dlp delays a clean filename handoff, `YtDlpWorker` now also matches the active stream by emitted total size before falling back to generic progress-reset ordering, which helps the audio stage switch at the right time even for ambiguous temp names.
- **Audio-only WebM stream labeling**: `YtDlpWorker` now prefers yt-dlp `format_id` values embedded in temp filenames (for example `.f251.webm.part`) before falling back to container extensions, preventing audio-only WebM/Opus transfers from being mislabeled as video.
- **Audio-only extract fallback for ambiguous containers**: When yt-dlp is running an explicit extract-audio job and only exposes an intermediate container path such as `.webm` or `.mp4`, `YtDlpWorker` now reports `Downloading audio stream...` instead of assuming those temporary containers always mean video.
- **Missing `requested_downloads` fallback for audio/video stage labels**: Some yt-dlp runs do not populate `requested_downloads` in `info.json`, so `YtDlpWorker` now seeds stream order from `[info] ... Downloading 1 format(s): 399+251-13` and also trusts aria2 command-line clues like `itag=251` plus `mime=audio/webm` to switch the GUI from video to audio at the correct handoff.
- **Late info.json stream-label regression**: When `info.json` arrives after stderr has already identified the active streams, `YtDlpWorker` now preserves the inferred stream order if `requested_downloads` is empty instead of clearing it and accidentally flipping the audio phase back to `Downloading video stream...`.
- **Exit cleanup for background tools**: Closing the app now triggers an explicit `DownloadManager` shutdown that terminates descendant process trees for active downloads and helpers, preventing orphaned `ffmpeg`, `aria2c`, and similar child processes from surviving after the window exits.
- **yt-dlp/aria2 progress regression after small overall bar update**: Restored robust progress parsing by treating pre-download extraction as indeterminate instead of leaving the UI parked at 0%, accepting slightly noisier aria2 summary lines, and using aria2 `FILE:` output to keep active stream tracking aligned with the file actually being transferred.
- **Authentication Settings hardcoded binary paths**: Fixed the browser cookies tester so it uses the user-configured or dynamically detected `yt-dlp` and `deno` paths instead of assuming they are always in a bundled `bin/` folder.
- **Package manager installation PATH detection**: The built-in package-manager installer (winget, scoop, pip) now automatically restarts LzyDownloader through the OS shell upon success, ensuring the app instantly inherits the newly updated system PATH and detects the installed tool without requiring manual intervention.

### Added
- **Strict dependency enforcement**: The application now actively prevents users from queuing video/audio downloads if `yt-dlp`, `ffmpeg`, `ffprobe`, or `deno` are missing, and blocks gallery downloads if `gallery-dl`, `ffmpeg`, or `ffprobe` are missing. When a missing binary is detected during enqueue, a helpful popup directs the user to the External Binaries settings page to resolve the issue.
- **Advanced Metadata Settings**: Added new toggles for "Crop audio thumbnails to square" and "Generate folder.jpg for audio playlists" in the Metadata settings page. Thumbnails can now also be explicitly converted to `jpg` or `png` formats.
- **Metadata UI Decoupling**: Extracted metadata and thumbnail settings from `AdvancedSettingsTab` into a dedicated `MetadataPage` component to improve modularity.
- **Source layout cleanup**: Moved the Start tab helper classes into `src/ui/start_tab/` and grouped the aria2 download pipeline into `src/core/download_pipeline/`. The aria2 RPC wrapper, yt-dlp download-info extractor, and ffmpeg merge worker were also renamed to `Aria2RpcClient`, `YtDlpDownloadInfoExtractor`, and `FfmpegMuxer` to make their roles clearer and reduce collisions with other core classes.
- **Open folder buttons on Active Downloads tab**: Added "Open Temporary Folder" and "Open Downloads Folder" buttons to the Active Downloads tab toolbar, providing quick access to download directories without switching to the Start tab. Both buttons include tooltips and show warning dialogs if directories are not configured.
- **Download Sections Support**: Added an option in Advanced Settings to download specific sections of a video by time range or chapter name. When enabled, a dialog appears before downloading, allowing the user to define one or more sections to be downloaded. This dialog also includes helper instructions on how to disable the feature if enabled accidentally.
- **External Downloader dropdown in Advanced Settings**: Replaced the "External Downloader (aria2c)" toggle switch with a dropdown selector offering "yt-dlp (default)" and "aria2c" options. The setting automatically hides when aria2c is not installed/discovered. Default changed from aria2c to yt-dlp to align with the unbundled binary model, ensuring users without aria2c installed won't encounter download failures.
- **yt-dlp error popup notifications**: The application now detects specific yt-dlp error messages during downloads and displays user-friendly popup dialogs with clear explanations. Supported error types include:
- **Refactored StartTab into smaller, more focused classes**: `StartTab.cpp` has been broken down into `StartTabUrlHandler` (manages URL input, clipboard, auto-switching), `StartTabDownloadActions` (handles download button, type changes, format checking, folder opening), and `StartTabCommandPreviewUpdater` (updates command preview). This improves modularity and maintainability.

---

## [0.0.10] - 02-18-2026 (Python Version)

### Added
- **Active download thumbnail previews**: The Active Downloads list now shows per-item thumbnail previews (when available) to the left of each title/progress row.
- **Audio playlist track-number tagging**: Playlist expansion now propagates `playlist_index` per entry, and audio playlist downloads write `track`/`tracknumber` tags so player ordering matches playlist order.
- **Developer Discord footer link**: Added a Discord icon button beside "Contact Developer" at the bottom of the main window; clicking opens `https://discord.gg/NfWaqKgYRG` and shows tooltip text "Developer Discord".

### Changed
- **Rotating application logs**: Switched the main file logger to size-based rotation (`LzyDownloader.log`, 10 MB per file, 5 backups) to prevent unbounded log growth.

### Fixed
- **Sorting subfolder token sanitization**: Sorting subfolder token values (for example `{album}`) are now sanitized before path assembly so illegal path characters like `/` and `\` are replaced with `_` instead of creating unintended nested folders or truncating names.
- **Thumbnail signal chain for UI rendering**: Wired `DownloadWorker` thumbnail events through `DownloadManager` to `ActiveDownloadsTab`, ensuring downloaded thumbnail images are actually rendered in the GUI.
- **Queued-item thumbnail timing**: Added early thumbnail prefetch during metadata preloading so queued downloads can show thumbnails before transfer starts.
- **Thumbnail preview persistence**: Thumbnail images used by the Active Downloads UI are now stored in a session-only temp cache and cleaned up automatically on app exit.
- **Audio active-thumbnail framing**: Active Downloads thumbnail previews now center-crop audio-only artwork to a square before display, matching the existing high-quality thumbnail conversion behavior used during download postprocessing.
- **OPUS artwork embedding regression**: The worker now skips the custom ffmpeg attached-pic remux path for `.opus` outputs and preserves yt-dlp's native OPUS artwork embedding behavior.
- **Playlist metadata continuity across expansion fallbacks**: All playlist expansion paths now preserve per-entry `playlist_index`/`playlist_count` metadata so downstream worker logic receives stable ordering data.
- **OPUS playlist track tagging reliability**: Playlist `track`/`tracknumber` tagging for `.opus` outputs now uses in-place tag updates that avoid artwork-stripping remux paths.
- **OPUS artwork loss after playlist track tagging**: `.opus` playlist track tags are now written in-place with `mutagen` instead of ffmpeg remux, preserving embedded artwork while still applying `track`/`tracknumber`.
- **Playlist track tag formatting**: Playlist `track`/`tracknumber` values are now zero-padded for single-digit indices (for example, `01`..`09`) to improve ordering consistency in players.
- **Playlist audio filename ordering**: Audio playlist downloads are now renamed on move to include a zero-padded playlist index prefix (`NN - `), for example `01 - <original name>.opus`.
- **Audio title truncation on dotted movement names**: Active Downloads title cleanup now strips extensions only for known media/container suffixes, preventing titles like `I. Molto allegro...` from being cut at the first movement period.

## [0.0.9] - 02-17-2026

### Added
-

### Changed
-

### Deprecated
-

### Removed
-

### Fixed
- **Download percentage visibility during active transfers**: Fixed progress parsing so `.part` destination lines are treated as primary media transfer context (not auxiliary files), restoring visible download percentages during yt-dlp/aria2 runs.
- **aria2 progress text stability**: Ignored aria2 noise lines without numeric progress (`[#...]`, `FILE: ...`, separator/summary lines) in active download rendering so they no longer overwrite the last shown percentage.
- **HLS fragment progress accuracy with aria2**: Added fragment-based fallback progress (`Total fragments` + `.part-FragN`) so long HLS downloads no longer stall around `<1%` when aria2 byte summary percentages are unstable.
- **Repeat-download row reuse in Active Downloads**: Starting a download for a URL that already has a terminal row (`Done`, `Cancelled`, or `Error`) now creates a new Active Downloads widget instead of reusing the old one.

### Security
-

## [0.0.8] - 02-16-2026

### Added
- **Version in Title Bar**: The application window title now includes the version number (e.g., "Media Downloader v0.0.8").
- **Advanced Filename Template Insertables**: Added an insertables dropdown next to `Filename Pattern` in Advanced Settings.
  - Users can click to insert common yt-dlp output tokens like `%(title)s`, `%(uploader)s`, `%(upload_date>%m)s`, `%(id)s`, and `%(ext)s`.
  - This mirrors the token-insertion workflow used by sorting subfolder patterns for faster, less error-prone template building.
- **Archive redownload override checkbox**: Added `Override duplicate download check` to the Start tab so users can bypass the duplicate-archive prompt for that download batch.
- **Generic Sorting Rules**: Overhauled the sorting rule system to be more flexible.
  - Users can now filter by various metadata fields: **Uploader**, **Title**, **Playlist Title**, **Tags**, **Description**, or **Duration**.
  - Added operators for filtering: **Is one of**, **Contains**, **Equals**, **Greater than**, and **Less than**.
  - Retained support for filtering by download type (All, Video, Audio, Gallery).
  - Existing uploader-based rules are automatically migrated to the new format.
- **Dynamic Subfolder Patterns**: Replaced the simple "Date-based Subfolders" checkbox with a powerful **Subfolder Pattern** field.
  - Users can define custom subfolder structures using tokens like `{upload_year}`, `{upload_month}`, `{uploader}`, `{title}`, etc.
  - Example: `{upload_year}/{uploader}` will sort files into `Target Folder\2025\Agadmator`.
  - Existing date-based rules are automatically migrated to the pattern `{upload_year} - {upload_month}`.
- **Playlist-Aware Sorting Rules**: Added playlist-specific sort targeting and playlist-name token insertion.
  - Added two new **Rule Applies To** options: **Video Playlist Downloads** and **Audio Playlist Downloads**.
  - Added a new subfolder insert token option: `%(playlist)s` to create dedicated folders per playlist name.
  - Sorting subfolder patterns now support both `{token}` and `%(token)s` styles, including `%(playlist)s`.
- **gallery-dl Support**: Added support for downloading image galleries using `gallery-dl`.
  - Added "Gallery" option to the Download Type dropdown in the Start tab.
  - Bundled `gallery-dl` binary.
  - Added `gallery-dl` update button in the Advanced Settings tab.
  - Added browser cookie support for `gallery-dl` in Advanced Settings.

### Changed
- **Faster playlist pre-expansion path**: Playlist expansion fallbacks no longer force `--yes-playlist` full extraction and now keep `--flat-playlist --lazy-playlist` to gather entry URLs/titles faster before worker enqueue.
- **Sorting token insert dropdown**: Replaced the sorting editor insert option `%(playlist)s` with `Album {album}` for subfolder pattern building.
- **Improved Gallery Validation**: Relaxed validation for gallery downloads to allow common gallery sites (Instagram, Twitter, etc.) even if simulation fails, as `gallery-dl` simulation can be unreliable due to auth requirements.
- **Gallery Download Parsing**: Enhanced file detection for `gallery-dl` downloads by parsing stdout for file paths and falling back to directory snapshots if needed.
- **Download Archive Always On**: Archive checks and writes are now always enabled (UI toggle is removed and config is enforced to `download_archive=True`).
- **Archive UI removed from Advanced tab**: Download archive now runs fully in the background with no archive toggle or archive controls shown to users.
- **SQLite-only archive persistence**: Archive records now persist only in `download_archive.db` with no `.txt` archive path usage.
- **Dependency management migrated**: Replaced `requirements.txt` with `pyproject.toml` (PEP 621 project metadata + dependencies).

### Deprecated
-

### Removed
-

### Fixed
- **Sorting date token simplification**: Removed sorting helper tokens `upload_year`, `upload_month`, and `upload_day`. Sorting date helpers now use release-date tokens only (`release_year`, `release_month`, `release_day`).
- **Sorting token cleanup**: Removed sorting support for `album_year` (UI insert option and token resolution).
- **Sorting legacy rule cleanup**: Removed legacy sorting-rule compatibility paths (`date_subfolders`, `audio_only`, legacy single-filter fields, and uploader-list fallback). Sorting now uses only `download_type`, `conditions`, and `subfolder_pattern`.
- **Progress bar early 100% + incorrect postprocessing status**: Active download parsing now ignores subtitle/auxiliary transfer percentages (for example `.vtt` subtitle fetches) until main media transfer starts, and postprocessing detection no longer treats generic "Extracting ..." lines as postprocessing (only true post-download steps such as `Extracting audio`).
- **yt-dlp native size/stage reporting**: Fixed native yt-dlp downloads that could jump to 100% on a small auxiliary file (such as a thumbnail) and then sit there for the real transfer. The worker now tracks `[download] Destination:` targets, ignores auxiliary-file percentages for the main media bar, recognizes fragment-style native progress lines, and emits clearer stage text for thumbnail/subtitle/video/audio transfers.
- **Download stage source-of-truth cleanup**: Removed UI-side guessing of video/audio phases from progress resets. `YtDlpWorker` now emits the main lifecycle stages directly (extracting media info, transfer target changes, segment downloads, post-processing), `MainWindow` no longer forces a generic `Downloading...` label on worker start, and resume now shows an indeterminate `Resuming download...` state until the worker reports the real stage.
- **Audio-stage handoff detection**: Hardened stream-stage labeling when yt-dlp switches from video to audio. The worker now classifies temp targets such as `.m4a.part` by the full path, caches the `requested_downloads` stream order from `info.json`, and advances the displayed stage when progress resets onto the next primary stream even if yt-dlp does not emit a clean new destination line.
- **Per-stream byte display preserved**: Multi-stream jobs continue to show the active stream's own downloaded/total byte counters instead of an aggregated overall total, so the audio phase can restart from its own low byte count while still switching the label from video to audio at the right handoff point.
- **Small overall job progress bar**: Added a slim secondary bar for multi-stream downloads that shows whole-job progress across the requested primary streams, while the main bar and byte counters remain scoped to the currently active video or audio stream.
- **Release date metadata targeting**: Switched default filename templates and Advanced template insert tokens from `upload_date` to `release_date` so date-based output naming uses release date metadata by default. Sorting date helper tokens now prefer `release_date` with fallback to `upload_date` for backward compatibility.
- **Embedded media date precedence**: Metadata writing now explicitly sets `meta_date` with strict fallback order `release_date` -> `release_year` -> `upload_date`, preventing corrupted `release_year` values from overriding valid date metadata.
- **GUI stutter during concurrent postprocessing**: Reduced main-thread repaint pressure by throttling duplicate progress updates and avoiding repeated progress-bar stylesheet resets during high-frequency yt-dlp/ffmpeg status output.
- **Playlist burst false `yt-dlp not available` failures**: Hardened `yt-dlp` availability checks to avoid transient failures under heavy concurrent playlist starts.
  - Added a lock-protected verification cache so many workers do not all run `yt-dlp --version` at once.
  - Increased verification timeout and reused the last-known-good check for brief timeout spikes.
  - Prevented temporary verification timeouts from immediately invalidating an otherwise working bundled `yt-dlp`.
- **Rate limit "None" handling**: Treat unlimited (`None`) as no throttle and prevent emitting invalid `--limit-rate None` in yt-dlp command args.
- **yt-dlp nightly option compatibility**: Removed unsupported `--no-input` usage from yt-dlp metadata/playlist pre-expansion commands to prevent immediate expansion/validation failures on newer yt-dlp builds.
- **Runtime concurrency above startup cap**: The Start tab now allows selecting up to 8 concurrent downloads for the current session while still persisting a maximum of 4 in config for the next app launch.
- **Temp subtitle file cleanup**: Added post-move cleanup for leftover subtitle sidecars (for example `.en.srt`) in `temp_downloads` when separate subtitle files are not enabled, preventing embedded-subtitle temp artifacts from being left behind.
- **HLS fragment progress no longer locks at 100% early**: Active download progress parsing now detects `frag X/Y` lines and prevents transient `100.0% ... (frag 0/N)` outputs from pinning the UI bar at 100% for the rest of the transfer.
- **Fixed Sorting Rule Album Detection**: Added fallback logic to use playlist title as the album name when the `album` metadata field is missing. This ensures `{album}` subfolder patterns work correctly for playlists (e.g., YouTube Music albums) that don't explicitly provide album metadata.
- **Enhanced Album Detection**: Added fallback to extract album metadata directly from downloaded files using `ffprobe` (checking both container and stream tags) if `yt-dlp` metadata is missing the album field. This improves sorting reliability for playlists where album info is embedded in the file but not in the JSON metadata.
- **Playlist Sorting Recognition**: Fixed an issue where downloads from expanded playlists were not recognized as playlist items by the sorting system. The application now internally tracks playlist context, ensuring that "Audio Playlist Downloads" and "Video Playlist Downloads" sorting rules are correctly applied without modifying metadata.
- **Failed-download list cleanup after successful retry**: URLs are now removed from the failed-download summary once a retry succeeds, so only currently failed downloads are shown.
- **Download archive repeat detection**: Duplicate downloads are now blocked before queueing by checking the archive directly in `DownloadManager`.
- **Archive key handling**: `ArchiveManager` now uses robust URL key generation (including YouTube `watch`, `shorts`, `live`, `embed`, and `youtu.be` forms), normalizes non-YouTube URLs, and avoids writing duplicate archive entries.
- **Archive duplicate UX**: When a URL is already archived, users are now prompted to either cancel or "Download Again" instead of being hard-rejected.
- **Playlist UI row collapsing**: Active Downloads now tracks widgets by unique item IDs instead of URL-only keys, so playlist entries always render as distinct rows even when URLs repeat.
- **Playlist expansion fallback regression**: Playlist expansion now parses and uses valid JSON output even when yt-dlp exits non-zero, retries YouTube Music `watch+list` links via canonical `youtube.com/playlist` URLs, and normalizes flat-playlist YouTube entries into per-video watch URLs.
- **Stuck playlist placeholder + sparse expansion fallback**: The "Preparing playlist download..." placeholder is now always replaced once expansion completes, and playlist expansion now includes a line-based `yt-dlp --print` fallback to recover per-item URLs when single-JSON extraction fails.
- **Playlist expansion auth parity**: Playlist pre-expansion now uses the same cookie and JavaScript runtime settings as normal downloads (`cookies_from_browser` and `--js-runtimes`), improving expansion reliability for protected YouTube/YouTube Music playlists.
- **Playlist pre-listing reliability**: Added entry-aware expansion and additional full/lazy `--print` fallbacks so playlist rows can be populated with per-item titles/URLs before downloads start on providers where flat JSON expansion fails.
- **Playlist worker fan-out fallback**: Added a final expansion fallback that runs yt-dlp with worker-equivalent args (`build_yt_dlp_args`) plus `--skip-download --print` so playlists that only work in full download context can still be pre-expanded into one worker per item.
- **Playlist all-mode enforcement**: When a playlist URL is detected and expansion still returns only one URL, the app now aborts queueing (with a clear error) instead of silently launching one playlist worker that downloads items sequentially under a single UI row.
- **Playlist status row cleanup**: After replacing the playlist placeholder with expanded item rows, the Active tab now removes any leftover status row like "Calculating playlist (... items)..." or "Preparing playlist download...".
- **Playlist preparation progress feedback**: Playlist pre-expansion now streams extraction progress to the placeholder row, updating the progress bar text with live status like `Extracting playlist item X/Y` (or a URL-based extracting message when total count is unavailable) so users can see activity during long playlist scans.

### Security
-

## [0.0.3] - 02-09-2026

### 02-10-2026
- Improved application update cleanup: The installer file is now automatically deleted after the update process completes, preventing clutter in the temporary downloads folder.
- Fixed logging initialization for installed builds by routing logs to a user-writable directory (AppData/LocalAppData) when the app directory is not writable.

### 02-09-2026
- Updated the app self-update flow to avoid browser dependency and apply updates directly from the UI.
- Unified update asset selection to prefer installer/MSI or portable executables and added safe executable swap handling for non-installer builds.
- Downloads now target the configured temporary directory when available, falling back to a system temp directory if unset.
- Build: Adjusted the PyInstaller spec to emit onedir output for NSIS packaging.

## [0.0.2] - 02-08-2026

### 02-08-2026
- Fixed a missing `time` import in `core/yt_dlp_worker.py` that caused MP3 playlist downloads to fail with `name "time" is not defined`.
- Normalized bundled/system binary paths to avoid mixed path separators in Windows error messages.
- Added detailed stderr/stdout to yt-dlp verification failures to make the root cause visible.
- Fixed duplicate redownload confirmation by centralizing the archive prompt in the main window.

### 02-08-2026
- **Fixed metadata embedding for videos**: Added `--embed-metadata` and `--embed-thumbnail` flags to video downloads (previously only applied to audio).
- **Capped max concurrent downloads at 4 on app startup**: While users can temporarily set higher concurrency (up to 8) in the UI, any value above 4 reverts to 4 when the app restarts. This prevents users from accidentally launching with overly aggressive concurrency.
- **Added "View Formats" feature**: Users can now select "View Formats" from the Download Type dropdown to inspect all available download formats for a URL without downloading. This runs `yt-dlp -F <URL>` and displays results in a dialog.
- **Dynamic Download button text**: The main Download button now changes its label based on the selected Download Type (e.g., "Download Video", "Download Audio", "View Formats") to provide clear user feedback.
- **Added comprehensive mouseover tooltips**: All UI elements now have helpful tooltips explaining their purpose, including buttons, dropdowns, checkboxes, and labels across all three tabs (Start, Active, Advanced).
- **Early unsupported-URL detection**: Added a quick background validation step before creating Active Downloads UI elements. If `yt-dlp` cannot quickly extract metadata for a URL, the app will report the URL as unsupported and will not create the download UI entry, preventing wasted UI artifacts and faster user feedback.
- **Extractor index & host heuristics**: At startup the app now attempts to build a local extractor index (using the `yt_dlp` python module when available) and uses it to rapidly determine whether a host is supported. This reduces latency for common hosts like YouTube by avoiding the slower metadata probes.

### 02-08-2026
- **Implemented Tiered URL Validation**: Refined the early supported-URL detection into a two-tier system to balance speed and accuracy.
- **Tier 1 (Fast-Track)**: The app now performs an immediate string/regex check for high-traffic domains (e.g., `youtube.com`, `youtu.be`, `music.youtube.com`). These are accepted instantly to provide zero-latency UI feedback.
- **Tier 2 (Metadata Probe)**: For less-common domains, the app initiates a background `yt-dlp --simulate` call. The "Active Download" UI entry is only generated if this probe confirms the URL is a valid target.
- **Rationale**: This hybrid approach eliminates the "verification lag" for the most frequent use cases (YouTube) while preventing the UI from being cluttered with invalid or unsupported URLs for niche sites.

### 02-08-2026
- **Configurable Output Filename Pattern**: Added a new GUI textbox in the Advanced Settings tab to allow users to define the output filename pattern using `yt-dlp` template variables.
- **Default**: `%(title)s [%(uploader)s][%(upload_date>%m-%d-%Y)s][%(id)s].%(ext)s`
- **Validation**: Basic validation ensures balanced parentheses and brackets before saving.
- **Reset**: Users can easily revert to the default pattern.
- **Integration**: The `DownloadManager` now pulls this pattern from the configuration instead of using a hardcoded string.

### 02-08-2026
- **Fixed ConfigParser Interpolation Error**: Disabled interpolation in `ConfigManager` to prevent `configparser` from attempting to interpret `yt-dlp` template variables (like `%(title)s`) as configuration references. This fixed a crash on startup.

### 02-08-2026
- **Grouped Advanced Settings into categories**: The Advanced tab now clusters options into labeled sections (e.g., Configuration, Authentication & Access, Output Template, Download Options, Media & Subtitles, Updates, Maintenance) so users can find settings faster.
- **Implemented Application Self-Update Mechanism**: Added logic to check for updates from GitHub Releases, download the update, and apply it.
- **Update Check**: Queries GitHub API for the latest release tag.
- **Download**: Downloads the update asset (exe/msi) to a temporary location.
- **Apply**: If it's an installer, launches it. If it's a binary replacement, uses a batch script to swap the executable and restart.
- **UI Integration**: Added a prompt in the main window when an update is available, with options to update now or view the release page.
- **Added chapter embedding toggle**: New Advanced Settings checkbox (default on) to pass `--embed-chapters` to yt-dlp when available.

### 02-08-2026
- **Added Global Download Speed Indicator**: Implemented a real-time download speed indicator at the bottom of the main application window.
- **Implementation**: Uses `psutil.Process().io_counters().read_bytes` to measure the total I/O read throughput of the application process.
- **UI Update**: Added a `QLabel` to the bottom of `LzyDownloaderApp` to display the total speed.
- **Aggregation**: Implemented a timer in `LzyDownloaderApp` to calculate the speed difference every second.
- **Rationale**: Provides a more accurate and simpler measure of actual data throughput compared to parsing `yt-dlp` stdout, and avoids per-file tracking complexity.
- **Ensured ffmpeg fallback for yt-dlp**: `yt-dlp` now receives an explicit `--ffmpeg-location` that prefers system `ffmpeg/ffprobe` when both are present, and otherwise falls back to the bundled binaries to ensure merging and post-processing work on clean systems.

### 02-08-2026
- **Added Theme Selection**: Added a dropdown in Advanced Settings to switch between "System", "Light", and "Dark" themes.
- **Implementation**: Updates `main.pyw` to apply the selected theme on startup.
- **Dark Mode**: Uses `pyqtdarktheme` if available, otherwise falls back to system style.
- **Persistence**: Saves the user's choice in `settings.json`.
- **Dynamic Switching**: Theme changes are applied immediately without requiring a restart.
- **Robustness**: Added checks for `setup_theme` vs `load_stylesheet` to support different versions of `pyqtdarktheme` or compatible libraries.
- **System Theme Detection**: Added Windows registry check to correctly detect system theme when "System" (auto) is selected, fixing an issue where it defaulted to dark mode.

### 02-08-2026
- **Fixed Global Download Speed Indicator**: Modified the download speed indicator to include I/O from child processes.
- **Implementation**: The speed calculation now iterates through the main process and all its children (`psutil.Process().children(recursive=True)`), summing their `io_counters` to get a total I/O throughput.
- **Rationale**: This provides a more accurate download speed measurement, as `yt-dlp` runs in separate subprocesses whose I/O was not previously being tracked.

### 02-08-2026
- **Fixed Global Download Speed Indicator (Again)**: The previous fix for the download speed indicator was flawed and caused it to display `-- MB/s`. The calculation logic has been rewritten to be more robust and now correctly sums the I/O from the main process and all its children.
- **Implementation**: The `_get_total_io_counters` function now simply sums the `read_bytes` from the main process and all its children, with proper error handling for terminated processes.
- **Rationale**: This simpler implementation is more resilient and provides an accurate total download speed.

### Fixed
- Fixed PyInstaller build hang on PyQt6.QtGui hook processing (PyInstaller 6.18.0 issue)
  - Solution: Created custom minimal PyQt6.QtGui hook to bypass problematic default hook
  - Added `hooks/` directory with simplified PyQt6.QtGui hook
  - Downgraded PyInstaller from 6.18.0 to 6.17.0 for better stability
  - Build now completes successfully and exe launches correctly
- Fixed critical app crash on startup caused by corrupted method merging in `main_window.py`
- Fixed unhandled exception in version fetch from daemon thread (disabled for now pending future refactor)
- Added robust error handling around signal emission in background threads
- Do not auto-create `temp_downloads` or set default output directory on first run; leave paths unset until user selects them

## [0.0.1] - 02-02-2026

### Added
- Initial release of LzyDownloader
- PyQt6-based GUI for downloading media via yt-dlp
- Support for 1000+ websites (YouTube, TikTok, Instagram, etc.)
- Playlist detection and expansion
- Concurrent download management with user-configurable limits (capped at 4 on startup)
- Advanced download options:
  - Audio/video quality selection
  - Format filtering by codec
  - SponsorBlock integration for automatic segment removal
  - Filename sanitization and customization
- Metadata and thumbnail embedding for videos and audio
- Browser cookie integration for age-restricted content
- Optional JavaScript runtime support (Deno/Node.js) for anti-bot challenges
- GitHub-based auto-update system:
  - Automatic release checking on app startup
  - Silent installer-based updates
