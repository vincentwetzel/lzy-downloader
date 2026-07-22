# Changelog

All notable changes to LzyDownloader will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Older historical changelogs (pre-v1.1.25) can be found in [docs/CHANGELOG_ARCHIVE.md](docs/CHANGELOG_ARCHIVE.md).

## [Unreleased]

## [1.2.6] - 2026-07-22

### Changed
- Added `docs/FILE_MANIFEST.md` as a dedicated quick index for file-to-responsibility lookup and redirected the main docs to use it as the primary path map.
- External binary discovery and update handling now use longer, environment-aware version probes, preserve explicit user overrides, and keep the active executable aligned with the last successful auto-detected path.
- The app updater now selects the correct release asset for Windows, Linux, and macOS instead of assuming a Windows-only installer flow.
- yt-dlp launch URLs now drop common tracking query parameters before execution, and livestream wait-state recovery can retry once without `--wait-for-video` or `--live-from-start` when the pre-wait probe reports a false-offline stream.

### Fixed
- Browser-cookie failures now retry once without browser cookies when yt-dlp reports public-download breakage caused by extractor cookie state.
- Active Downloads progress and diagnostics now retain clearer bounded tails so long-running jobs stay informative without growing memory usage unchecked.
- Playlist and carousel item targeting now respects generic one-based index hints such as `img_index`, `slide`, `item`, `index`, and `playlist_index` without adding site-specific overrides.
- Linux AppImage packaging now stages AppDir cleanly and generates a desktop file whose icon entry matches the resized release PNG so linuxdeploy can resolve the final bundle reliably.
- Final file cleanup is more resilient when Windows briefly holds locks after download completion, and FFmpeg single-file moves now fall back to copy/remove when a direct move is not possible.
- Audio playlist artwork generation is limited to explicit full-playlist or multi-item batches, so single tracks and partial selections no longer create unnecessary `folder.jpg` files.
- External-binary version notes and update diagnostics now stay compact and clearer during install-success repair flows and standalone-binary warnings.

## [1.2.5] - 2026-07-07

### Changed
- Added `docs/FILE_MANIFEST.md` as a dedicated quick index for file-to-responsibility lookup and redirected the main docs to use it as the primary path map.
- yt-dlp launch URLs now drop common tracking query parameters before execution, and livestream wait-state recovery can retry once without `--wait-for-video` or `--live-from-start` when the pre-wait probe reports a false-offline stream.

## [1.2.1] - 2026-07-06

### Fixed
- **Proactive browser-cookie retry**: yt-dlp workers now watch stderr for cookie-backed HTTP 400/Bad Request, JSON metadata, and live-status failures, proactively retry once without browser-cookie options, and add a clearer authentication tip when cookies were involved.
- **External binary probing**: Version checks now use the app process environment, allow longer startup timeouts, and recognize date-like version banners so External Binaries rows resolve more reliably.
- **Python package install warnings**: Successful tool installs that still emit Windows locking/invalid-distribution warnings now surface a follow-up repair or standalone-binary choice instead of showing a plain success dialog.
- **Audio playlist artwork scope**: `folder.jpg` generation now runs only for explicit full playlist or multi-item audio batches, so single-item tracks and partial playlist selections no longer get playlist artwork.
- **Windows FFmpeg discovery**: `ProcessUtils` now searches a few common manual Windows FFmpeg install folders before broader resolution so locally installed copies are easier to pick up.
- **aria2c referer propagation**: `YtDlpArgsBuilder` now passes the request origin as an aria2c referer header when an external downloader is used, which improves compatibility with hosts that require a referer for segmented transfers.
- **FFmpeg mux cleanup resilience**: `FfmpegMuxer` now retries transient output/source cleanup and falls back from rename to copy/remove when moving a single input file to the final destination.
- **Generic playlist/carousel item targeting**: URLs with hostname-independent item index hints such as `img_index`, `slide`, `item`, `index`, or `playlist_index` now probe the full expanded result set and select the intended entry, while real downloads pass the chosen one-based item to yt-dlp with `--playlist-items`.
- **Playlist entry filenames**: Output templates now add uploader and upload-date metadata fallbacks so playlist or carousel entries with only playlist-level owner/date fields still produce useful names.
- **Cookie fallback coverage**: Browser-cookie retries also cover cookie/API-access failures that surface as empty media responses or permission/decryption errors, retrying once without cookie arguments before reporting terminal diagnostics.
- **Linux AppImage packaging**: `build_release.py` now stages AppDir under `build-release`, cleans stale Linux packaging state, and generates a desktop file whose icon entry matches the resized release PNG so linuxdeploy can resolve the AppImage icon during WSL builds.
- **App updater portability**: Release asset selection now matches the current platform (`.exe`, `.AppImage`, or `.dmg`) and the downloaded updater is launched accordingly, so non-Windows update flows can proceed without assuming an installer EXE.
- **Worker diagnostics and bounds**: yt-dlp workers now retain a bounded tail of warnings/errors for terminal diagnostics, treat generic FFmpeg "Option not found" failures as post-processing errors, and tighten pre-wait thumbnail file handling and logging when metadata fetches fail.
- **Gallery-dl buffering**: Gallery workers now guard against deleted processes before reading buffers and trim retained stderr in batches to keep long gallery runs from growing memory linearly.
- **Sorting and archive cleanup**: Sorting and archive helpers now use simpler Qt-native comparisons for Qt 6.2 compatibility while preserving the existing metadata fallback and URL normalization rules.


## [1.1.95] - 2026-06-25

### Changed
- **Version bump**: Updated project version to 1.1.95.


## [1.1.88] - 2026-06-16

### Fixed
- **Release workflow Qt install**: Removed non-installable and unused Qt module requests and switched Windows CI to the Qt 6.6 `win64_msvc2019_64` desktop archive so `aqtinstall` can resolve packages correctly.
- **CMake merge cleanup**: Resolved committed version conflict markers and aligned CMake/vcpkg release metadata for `1.1.88`.

## [1.1.75] - 2026-06-16

### Added
- **Unified Cross-Platform Build Pipeline**: Integrated the cross-platform `build_release.py` script to clean, configure, compile, and package the application natively for both Windows and Linux.
- **Linux AppImage Support**: Fully enabled official Linux support by packaging the application into single-file portable `.AppImage` containers.
- **GitHub Actions Release Automation**: Added automated multi-platform workflows to trigger on git release tags, building and attaching both installers to GitHub Releases in parallel.

## [1.1.73] - 2026-06-22

### Changed
- **Version Synchronization**: Unified and bumped build version metadata to `1.1.73` across CMake configure scripts, vcpkg manifests, and the changelog records.
- **Build Pipeline Alignment**: Resolved executable metadata mismatches to ensure stable automated installer packaging.

## [1.1.71] - 2026-06-20

### Changed
- **Metadata Alignment**: Updated versioning constraints and unified metadata records across CMake configure targets and vcpkg dependency manifests.
- **Build Pipeline Polish**: Internal optimization of build targets and clean output alignments.

## [1.1.70] - 2026-06-15

### Changed
- **External binary management refactor**: `yt-dlp` and `gallery-dl` updater logic now share `BaseBinaryUpdater`, while external tool lookup goes through `SmartBinaryResolver` so manual overrides win, the app-local `bin` folder is considered first, stale settings ghosts are cleared, and multiple discovered candidates can be selected by newest usable version.
- **Startup binary path ownership**: Startup now tracks whether saved binary paths were auto-detected or manually chosen, refreshing only auto-detected paths during discovery while preserving explicit user overrides.
- **Discord bridge freshness**: Local Discord bridge webhook payloads are sent immediately when a row's status or numeric progress changes, while secondary active-download updates remain throttled.

### Fixed
- **Binary update diagnostics and integrity**: App and tool update checks now use the shared chronological version parser, surface GitHub rate-limit/not-found failures more clearly, show External Binaries update warnings, and verify downloaded standalone tools against SHA-256 data when upstream release metadata provides it.
- **Completed-with-warning downloads**: yt-dlp jobs that produce final media despite a non-zero exit now keep the media, combine post-processing and exit-code warnings, and show a clear "Completed with warnings" state.
- **Finalizer cleanup resilience**: Final file replacement and temporary sidecar cleanup now retry short-lived locked file removals before failing, reducing spurious cleanup errors on Windows.
- **Livestream wait metadata safety**: Pre-wait livestream metadata and thumbnail fetches now guard process lifetime, validate thumbnail JSON fields more strictly, and avoid leaving empty thumbnail files after failed writes.

## [1.1.65] - 2026-06-10

### Changed
- **Progress rendering polish**: Active download rows now animate main and overall progress changes, cache tinted standard icons, and stop progress animations explicitly when rows enter indeterminate, cleared, cancelled, or completed states.
- **Conditional Clear Temp action**: The row-level "Clear Temp" button on inactive/stopped/cancelled download rows is now shown only if there are actual temporary files or tracked cleanup candidates existing on disk.
- **Shared thumbnail networking**: Active downloads and download history now reuse an app-owned thumbnail network manager with safer redirect policy, a LzyDownloader user agent, and request timeouts instead of creating short-lived managers per thumbnail.
- **Sorting UI persistence polish**: Sorting rule loading/saving now batches table repaints, preserves selection after add/edit, stores condition keys consistently, and guards empty combo indices while editing rules.
- **Hot-path parser cleanup**: yt-dlp output handling now gates expensive regex parsing by line prefix, shares common progress metadata population, and trims noisy per-line debug logging while preserving detailed native and aria2 progress data.
- **Qt-native cleanup polish**: Queue state serialization, sorting metadata lookup, yt-dlp argument building, and progress size math now use more direct Qt/STL helpers to reduce duplicate code and avoidable allocations.
- **Start tab settings cleanup**: Start-tab operational controls now share one guarded config-binding path for combo settings, preserving instant saves while reducing duplicate signal wiring.

### Fixed
- **Completed livestream replays**: Playlist expansion and runtime format selection now preserve yt-dlp `live_status` metadata and treat `post_live` videos as normal archived videos, avoiding livestream recorder/wait arguments and aria2c for already-published replays. Generic `/live/` URL-shape hints also bypass aria2c when extractor metadata is unavailable.
- **yt-dlp process environment**: External tools no longer inherit ambient `HTTP_PROXY`, `HTTPS_PROXY`, or `ALL_PROXY` variables, and app-built yt-dlp commands explicitly request direct connections unless the app supplies its own proxy option.
- **Browser cookie fallback**: If yt-dlp cannot copy browser cookies because its temporary cookie database path is access-denied, or if browser-cookie extractor state incorrectly reports a finished live replay as ended/unavailable, the worker retries once without `--cookies-from-browser` so public media can still download and protected media fail with clearer diagnostics.
- **No site-specific overrides**: Contributor docs now ban hardcoded per-domain downloader behavior; fixes must use generic metadata, user settings, standards, or hostname-independent URL-shape checks. Existing per-domain referer/extractor-argument injection in yt-dlp and aria2c argument construction was removed.
- **Local API failed status**: Failed downloads no longer report `progress: 100` in Local API status snapshots.
- **Updater JSON validation**: App update checks now ignore malformed release assets, tolerate missing release-note bodies, and fail clearly if a newer release response has no valid assets array.
- **yt-dlp updater asset selection**: yt-dlp self-updates now look for the correct release asset on Windows, macOS, and Linux instead of assuming only `yt-dlp.exe`.
- **Queue backup restore validation**: Queue restore now skips non-object entries in `downloads_backup.json` instead of passing malformed backup elements into resume handling.
- **Finalizer thread affinity**: Sorting-rule resolution during finalization is marshaled back to the application thread before worker-thread file moves continue, avoiding direct cross-thread access to UI-owned sorting state.
- **yt-dlp cleanup robustness**: Failed or interrupted yt-dlp workers now share a single temporary-directory fallback path, clean orphaned wait thumbnails only when they exist, avoid duplicate `info.json` removal warnings, and parse buffered UTF-8 lines from stable byte pointers.
- **Async cleanup ownership**: Playlist expansion timeouts and aria2 partial-file cleanup timers are now owned by long-lived application objects, preventing callbacks from targeting deleted process objects.
- **Playlist entry URL resolution**: YouTube playlist expansion now builds watch URLs from real entry IDs only, avoiding invalid watch URLs when yt-dlp provides only a source URL fallback.
- **Section normalization failure cleanup**: Failed section-clip normalization now removes the temporary replacement file if the original cannot be replaced.
- **Sorting token replacement**: Sorting subfolder token expansion now replaces each token occurrence independently, so case-insensitive duplicate tokens and date helper tokens cannot accidentally rewrite unrelated literal text.
- **Sorting metadata filtering**: Sorting rules now consistently ignore empty, `null`, and `NA` metadata values across aliases, playlist-title fallbacks, and token expansion.
- **Startup worker teardown**: Startup checks now delete the worker when the thread finishes and mark extractor generation done if no extractor parser is available, preventing startup completion from hanging.
- **Discord bridge request cleanup**: Local Discord bridge posts now use explicit timeouts and let replies clean themselves up through Qt deferred deletion.
- **Discord bridge callback lifetime**: Discord webhook request callbacks are now anchored to the main window context so queued network completions cannot outlive the UI object that owns bridge state.
- **yt-dlp metadata and thumbnail cleanup**: yt-dlp workers now flush trailing stdout/stderr through the shared UTF-8 line parser, recover `info.json` by scanning the UUID temp directory if the expected path is stale, move wait-state thumbnails into managed cleanup scope when possible, share validated `info.json` parsing and file cleanup helpers, clean empty UUID temp folders through the same fallback path on finish/error, and surface file cleanup/write failures instead of silently ignoring them.

## [1.1.58] - 2026-06-06

### Changed
- **Header hygiene cleanup**: Core and UI headers now consistently use `#pragma once`, and several helper APIs were made const-correct or explicit to match the repository coding standards.
- **Start tab safety/i18n cleanup**: Start-tab URL handling, download actions, and command preview helpers now guard missing UI dependencies more defensively and wrap user-facing strings in Qt translation calls.
- **Dependency baseline pinning**: The vcpkg manifest now pins a builtin baseline so manifest-mode source builds resolve dependencies reproducibly.
- **Audio playlist artwork default**: `Metadata/generate_folder_jpg` now defaults to enabled, and audio playlist detection also honors playlist metadata beyond just positive playlist indices.
- **Process-output memory bounds**: Long-running yt-dlp, gallery-dl, and FFmpeg output buffering now keeps bounded tails or buffered complete lines to avoid unbounded memory growth during livestrimes and large galleries.

### Fixed
- **Archive connection teardown scope**: Archive database cleanup now closes/removes the current thread's Qt SQL connection by its thread-local name, avoiding cross-thread connection removal while still releasing SQLite handles on shutdown and tests.
- **Logging cleanup safety**: Log-file open failure cleanup now routes the `QFile` through Qt deferred deletion instead of deleting a `QObject` directly.
- **Output template validation responsiveness**: Video/audio template validation now runs `yt-dlp` asynchronously with a watchdog and guarded callbacks, keeping Advanced Settings responsive while validation is in progress.
- **Download history persistence**: `download_history.json` is now loaded with explicit JSON validation and saved atomically with `QSaveFile`.
- **gallery-dl failure reporting**: The gallery worker now flushes remaining stdout/stderr at process exit, preserves a bounded stderr tail, reports crash details, and treats platform-neutral `gallery-dl` resolution consistently through `ProcessUtils`.
- **JSON parser diagnostics**: Aria2 RPC and yt-dlp metadata extraction now report JSON parse errors explicitly instead of silently treating malformed output as an empty response.
- **FFmpeg mux fallback**: Single-input muxing now falls back to copy/remove if a direct rename fails, improving cross-volume and filesystem-boundary moves.

## [1.1.56] - 2026-06-05

### Changed
- **Playlist expansion naming cleanup**: Replaced the broad `PlaylistExpander` implementation with `PlaylistExpansionWorker` plus `PlaylistExpansionParser`, keeping async yt-dlp probing separate from queue-item JSON mapping.
- **Stale source audit cleanup**: Removed unbuilt, unreferenced legacy helpers (`YtDlpJsonParser`, `YtDlpJsonExtractor`, `Aria2Daemon`, `FfmpegPostProcessor`, and `StringUtils`) so the source tree better matches the active architecture.
- **Build source list cleanup**: Removed obsolete source entries from `LzyAppLib` and consolidated post-build copying of the two extractor domain lists into one CMake step.
- **Headless test throughput**: `run_headless_tests.py` now runs CTest in parallel using the host CPU count while keeping `QT_QPA_PLATFORM=offscreen`.
- **Translation readiness pass**: Advanced Settings pages and binary-management dialogs now wrap user-facing text in Qt translation calls, keeping the UI ready for the supported-language work tracked in `docs/LANGUAGES.md`.
- **Active download controls wording**: Download rows now use clearer text labels for `Cancel`, `Stop & Save`, `Retry`, `Resume`, and `Clear Temp`, making destructive cancellation distinct from livestream finalization.
- **Core performance polish**: Queue, worker, finalizer, updater, Local API, and settings paths now use more Qt-native literals, cached/static regular expressions, prepared/reused query objects, and direct map inserts/lookups to reduce avoidable allocations in hot paths.
- **yt-dlp worker parsing cleanup**: stdout and stderr now share the same buffered line parser, reducing duplicate process-output logic while preserving progress and error parsing behavior.
- **Extractor refresh performance**: Shared extractor-domain parsing regexes are precompiled for faster yt-dlp/gallery-dl list generation.

### Fixed
- **Archive database teardown**: Archive database shutdown now closes matching Qt SQL connections before removing them, avoiding lingering SQLite locks during shutdown and tests.
- **Finalizer thread safety**: Finalization resolves settings before worker-thread file operations, guards QObject callbacks, and reports cleanup failures instead of silently ignoring failed temp-file removals.
- **Local API request validation**: The localhost API now rejects empty or malformed request lines with JSON errors, matches `Content-Length`, `Expect`, Host, and extension Origin headers more precisely, and builds HTTP responses without repeated string formatting.
- **Single-instance crash recovery**: Startup now detaches stale `QSharedMemory` segments before creating the single-instance lock and releases the startup semaphore through a scope guard.
- **yt-dlp metadata extraction errors**: JSON metadata extraction now reports a clear missing-binary error when `yt-dlp` cannot be resolved, and process stderr is decoded as UTF-8.
- **URL validation timeout ownership**: URL validation and JSON extraction timeouts are owned by their worker objects so callbacks are suppressed safely if the process object changes lifetime.
- **yt-dlp temp metadata cleanup**: Completed downloads now remove `info.json` after loading metadata and continue cleaning empty UUID temp folders using the configured or derived temp path.
- **Livestream downloader safety**: Livestream jobs now bypass aria2c, preserve the `is_live` flag from URL hints and `info.json`, and clamp wait-for-video intervals so invalid settings cannot create unsafe retry loops.
- **Windows process-tree cleanup**: Cancellation and graceful livestream interrupts now avoid orphaning child `ffmpeg` processes by giving `taskkill` a short bounded chance to terminate the tree before the parent process is killed.

## [1.1.52] - 2026-06-03

### Added
- **AI contributor coding standards**: Added `docs/CODING_STANDARDS.md` as the canonical C++/Qt quality, security, threading, file-safety, testing, and UI guidance for future automated code changes.
- **Download History tab**: Added a persistent Download History tab backed by `download_history.json`, showing completed downloads with title, URL, timestamp, size, duration, cached thumbnail, and quick Open File/Open Folder actions.
- **Expanded headless test coverage**: Added CMake-registered tests for configuration defaults/reset cleanup, Local API auth/enqueue behavior, ProcessUtils cache behavior, and URL validation, plus headless CTest helper/workflow files for non-interactive Windows test runs.
- **Partial playlist queueing**: Playlist prompts now include a "Download Part..." flow that lets users select expanded playlist items by range text (for example `1-5, 8, 11-13`) or individual checkboxes before queueing only those entries.

### Changed
- **Core reliability hardening**: Core download, updater, queue, process, and parsing paths now consistently use localized user-facing strings, `QStringLiteral`, static regular expressions for hot parsers, validated JSON parsing, safer path construction, and atomic `QSaveFile` writes for critical state and downloaded tools.
- **Completed download metadata capture**: yt-dlp completion metadata now forwards duration values from `info.json`, and queued rows can show cached thumbnail paths immediately when restored or updated.
- **Qt standards cleanup**: Downloader argument builders, worker parsers, queue/archive helpers, updater paths, and runtime selection dialogs now consistently use Qt-native string/byte literals, argument-list process launches, and safer path helpers to reduce avoidable allocations and fragile platform-specific string handling.
- **Network and updater bounds**: App, yt-dlp, and gallery-dl update checks/downloads now apply explicit redirect policies, timeouts, response-size limits, payload-size limits, and safer JSON validation before saving or installing update artifacts.
- **Background runtime isolation**: Queue state and Local API token handling now treat `--background` the same as server/headless mode by using the isolated `Server/` app-data subfolder.
- **Playlist prompt handling**: `playlist_logic=Ask` now supports queueing all items, queueing only selected items, queueing just the first item, or cancelling after playlist expansion.

### Fixed
- **Sleep mode delay on idle queues**: The 1-download short/long sleep modes now calculate precise elapsed times since the last finished download instead of blindly sleeping when the queue is already idle, and changing the concurrency setting away from sleep modes now immediately cancels any pending sleep delays.
- **Local API hardening**: Generated API tokens are written atomically with owner-only permissions, oversized requests are rejected, and Host/Origin validation blocks unauthorized browser-origin access to the localhost API.
- **Finalizer responsiveness**: File move/copy and related finalization work is pushed off the GUI thread while preserving progress/status updates, keeping the UI responsive during large completed-download moves.
- **Finalizer callback safety**: Gallery and media finalization now guards queued self-references before emitting progress or copying directories, avoiding stale-object access during shutdown or cancellation.
- **Livestream wait-state feedback**: Scheduled livestream and upcoming-premiere waits now emit immediate indeterminate status, show next-check countdowns, and delay terminal failure while waiting for a user response.
- **Sorting path readability**: Sorting token sanitization now replaces illegal path characters with hyphens and collapses repeated spaces instead of silently merging metadata words together.

## [1.1.46] - 2026-05-29

### Changed
- **Extractor domain refresh**: Updated the bundled yt-dlp extractor domain list so Nitter support points at `nt.vern.cc`.

### Fixed
- **Playlist expansion temp-folder hygiene**: Playlist pre-expansion now reuses `YtDlpArgsBuilder` without creating a stranded per-download UUID temp folder for the placeholder item.
- **yt-dlp temp cleanup fallback**: yt-dlp cancellation, skipped-download cleanup, and wait-thumbnail relocation now derive the temp directory from the completed-downloads folder when `temporary_downloads_directory` is not explicitly set, preventing orphaned UUID folders and wait thumbnails in first-run or partially configured environments.

## [1.1.45] - 2026-05-28

### Added
- **Livestream Finish Now action**: Active livestream downloads now expose a `Finish Now` control that sends yt-dlp a graceful interrupt so the current stream can stop recording and continue through normal finalization.

### Changed
- **External Tools reliability**: Install/update dialogs now run with the app-managed process environment, can be cancelled safely, quote command paths with spaces, clear binary-resolution caches before config changes propagate, and use package-manager-aware update commands for WinGet/Scoop/Chocolatey/Homebrew/pip installs. Standalone Deno updates now use `deno upgrade`.
- **Output template validation**: Video and audio filename templates now share a single yt-dlp validation path with explicit start/finish timeouts, type-specific templates inherit the current shared default when blank, and gallery template reset/save messaging is clearer.
- **Extractor refresh automation**: The yt-dlp and gallery-dl extractor update scripts now finish without waiting for a final Enter keypress, share one domain-parsing helper, and keep the app on the two explicit extractor list files used by clipboard checks and the Supported Sites dialog.
- **Test target registration**: CMake now registers Qt tests through a shared `lzy_add_test(...)` helper and includes the archive, sorting, and UI widget test executables alongside the existing yt-dlp and end-to-end test coverage.
- **Headless/background launch docs**: Documentation now treats `--background` consistently with server/headless automation for single-instance locks, API startup, and isolated runtime state.
- **Single-download sleep mode**: Sleep-mode scheduling now starts the first eligible item immediately and waits between subsequent single-download starts.
- **Local API enqueue IDs**: `POST /enqueue` now accepts an optional `id` field so trusted local integrations can provide stable job IDs instead of always receiving an app-generated UUID.

### Fixed
- **Qt HTTPS on clean Windows installs**: Restored explicit OpenSSL runtime deployment from vcpkg/Qt install directories so `qopensslbackend` can initialize instead of falling back to Qt's `cert-only` backend and failing update checks with `TLS initialization failed`.
- **Cleared download state cleanup**: Clearing stopped or failed rows now notifies `DownloadManager`, cancels any still-running worker, removes paused queue state, saves the queue asynchronously, and resumes scheduling.
- **Temporary directory cleanup**: Finished gallery downloads, skipped yt-dlp jobs, and cancelled queued or paused jobs now clean up their per-download UUID temp directories instead of leaving empty or stale folders behind.
- **SponsorBlock A/V desync**: Fixed a regression where audio/video desynchronization still occurred during SponsorBlock segment removal because synchronization arguments were only being applied to the `ModifyChapters` post-processor.
- **Cookie-check UI recovery**: Browser cookie validation now buffers stderr for the final error dialog, handles `yt-dlp` launch failures, suppresses stale timeout/cancel callbacks, and cleans up the validation process tree when the settings page is destroyed.
- **yt-dlp error extraction**: URL validation, JSON metadata extraction, and worker output handling now capture embedded `ERROR:` text more reliably and classify no-format failures with clearer guidance.
- **Archive database teardown**: Closing the archive connection no longer accidentally creates a Qt SQL connection while checking whether one already exists.
- **Queue persistence churn**: Queue mutations no longer each schedule redundant state saves; queue count updates and manager-level reactions handle persistence more cleanly during rapid queue operations.
- **End-to-end test server startup**: The local test HTTP server now chooses `python.exe` on Windows and `python3` elsewhere, probes `127.0.0.1`, resets the socket between retries, and uses argument-list process termination.

## [1.1.35] - 2026-05-09

### Added
- **Discord queue positions**: Local Discord bridge webhook payloads now include `queue_position` for queued jobs and refresh positions when users move queued downloads up or down.

### Fixed
- **Sorting rule literal fallbacks**: Fixed an issue where `yt-dlp` populating missing metadata fields with literal `"NA"` or `"null"` strings (e.g., for `album`) would bypass fallback logic and result in `"Unknown"` folder names. The sorting manager now correctly rejects these literal strings and falls back to `playlist_title` or other metadata aliases.
- **SponsorBlock/Section A/V desync**: Fixed audio/video desynchronization in MP4 files during SponsorBlock and section cuts. The application now injects FFmpeg arguments (`-ignore_editlist 1`, `-avoid_negative_ts make_zero`, `-fflags +genpts`) into yt-dlp's cut pass to explicitly lock audio to the new video keyframes and rewrite clean timestamps.
- **HLS fragment progress parsing**: Fixed an issue where yt-dlp's native downloader caused the UI progress bar to violently snap back to 0% thousands of times when downloading fragmented HLS streams (like Twitch VODs). The progress parser now intercepts the `(frag X/Y)` output to calculate and display the true overall progress percentage and replaces the fragment byte size with a clear segment count.

## [1.1.31] - 2026-05-05

### Changed
- **Extractor domain refresh**: Updated the bundled yt-dlp extractor domain list so Nitter support points at the currently useful `nitter.dcs0.hu` instance instead of the retired `canada.unofficialbird.com` domain.

### Fixed
- **Active Downloads duplicate row replacement**: Re-adding a download item with an existing internal ID now clears the old row before inserting the replacement, preventing duplicate widgets when queue placeholders or restored items are refreshed.
- **Discord webhook payload stability**: Webhook status strings are now flattened and capped before POSTing to the local Discord bridge, keeping payloads compact even when yt-dlp emits multi-line status text.
- **Discord webhook terminal-state reporting**: Completion and cancellation events now preserve the final tracked state long enough for downstream integrations to observe terminal progress/status instead of dropping the item immediately after the final webhook.
- **Discord webhook progress preservation**: Queue refreshes that do not include a progress field now keep the previous progress value, avoiding accidental resets to `0%` in local bridge clients.
- **Discord playlist parent mapping**: Playlist child webhook payloads now use the explicit `playlist_placeholder_id` mapping so expanded children stay associated with the original enqueue request without overloading unrelated option IDs.

## [1.1.28] - 2026-05-04

### Fixed
- **Headless exit-after state flush**: Fixed an issue where running in `--server --exit-after` mode would exit via `QCoreApplication::quit()` before the final completed queue state was flushed to `downloads_backup.json`. The headless shutdown sequence now explicitly invokes `DownloadManager::shutdown()` before quitting to ensure external integrations (like the Python Discord bridge) correctly see the queue as empty/completed on the next startup.
- **Wait-state thumbnail cleanup**: Upcoming livestream wait thumbnails are now cleaned up on cancellation or failure, moved into the per-download UUID temp folder on success, and replaced by the real yt-dlp thumbnail path when one becomes available. This prevents orphaned `_wait_thumbnail.jpg` files from lingering in the temp directory.

## [1.1.25] - 2026-05-04

### Added
- **Discord Webhook Integration**: Added real-time HTTP POST payload emissions to a local webhook (`http://127.0.0.1:8766/webhook`) during download progress, completion, and cancellation, keeping the Python Discord bot perfectly synchronized with the C++ application state.
- **Expanded Qt test coverage**: Added archive URL-normalization, sorting-token/sanitization, UI widget progress-state, and local end-to-end download test coverage with isolated test fixtures.

### Changed
- **DownloadManager source split**: Split `DownloadManager.cpp` into focused enqueue, playlist, control, execution, and worker/finalization translation units so the core manager files stay under the 500-line context limit.

### Fixed
- **App and Discord icons**: Fixed the CMake resource wiring so Qt runtime assets (`:/app-icon` and `:/ui/assets/discord.png`) and the Windows executable icon resource are compiled into `LzyDownloader.exe`.
- **Discord Webhook Reliability**: Fixed an issue where Discord webhook POST requests would silently fail or leak memory because `QNetworkAccessManager` was being instantiated without a dedicated event loop. Webhook emissions are now strictly routed through the main GUI thread.
- **Discord Webhook Throttling**: Fixed payload bombardment by ensuring webhook POSTs bypass the 1.5-second throttle only when the download status string actually changes.
- **Discord Webhook Playlist Tracking**: Added `parent_id` mapping to webhook payloads so remote clients (like the Discord bot) can successfully associate dynamically generated playlist child jobs with their original parent `/enqueue` request.

Older historical changelogs (pre-v1.1.25) can be found in docs/CHANGELOG_ARCHIVE.md.
