# Changelog

All notable changes to LzyDownloader will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Older historical changelogs (pre-v1.1.25) can be found in [docs/CHANGELOG_ARCHIVE.md](docs/CHANGELOG_ARCHIVE.md).

## [Unreleased]

## [1.2.45] - 2026-09-04

- **Audio post-processing:** Skip redundant FFmpeg metadata rewrites when
  yt-dlp has already embedded the artwork and its tracked thumbnail sidecar
  is no longer available, allowing valid audio files to proceed directly to
  finalization. Added regression coverage for missing-thumbnail audio jobs.

## [1.2.44] - 2026-09-02

- **Persistence responsiveness:** Queue-backup and Download History JSON writes
  now snapshot immutable state and run through coalescing background writers.
  Orderly shutdown waits for the writer and flushes the final queue snapshot;
  successful media is recorded in SQLite from a worker-thread-local connection.
- **Shutdown safety:** Prevent queued worker starts after shutdown begins, wait
  for owned worker threads, and make startup completion emission idempotent for
  clean headless and timed-test teardown.
- **Playlist fallback identity:** Preserve the original row/job ID when ordinary
  URLs fall back after delayed playlist probes, including non-interactive
  playlist selections and recovered placeholder options.
- **Responsive UI cleanup:** Move history thumbnail decoding and failed/cancelled
  row temporary-file checks off the GUI thread with guarded generation checks.
- **Worker responsiveness:** Run downloader processes, metadata embedding,
  finalization, thumbnail loading, and history-cache copies off the GUI thread;
  coalesce high-frequency row progress while retaining the newest state.
- **Playlist behavior:** Carry the Start tab's selected playlist policy into each
  request, honor persisted defaults for manager callers, and apply single-item
  handling to expanded playlists.
- **Resume and progress recovery:** Preserve playlist indices for resumed audio
  filenames and asynchronously recover transfer sizes from owned `.part` files.
- **Windows test deployment:** Add a direct-Qt `windeployqt` fallback, deploy
  `qminimal`, serialize Visual Studio test builds, and bound loopback fixture
  timeouts.
- **Configuration safety:** Serialize shared `QSettings` access and validate
  persisted playlist policies against stable internal values.
- **Release CI:** Centralize the full headless test gate and publish platform
  assets only after all required release jobs succeed.
- **Extractor metadata:** Refresh the bundled yt-dlp extractor domain list.

## [1.2.43] - 2026-08-30

- **UI polish:** Refined the Active Downloads empty state and required-tools
  setup dialog with clearer hierarchy, improved spacing, larger action targets,
  palette-aware contrast, and better narrow-window readability. Tool status
  summaries now use concise singular/plural wording while retaining actionable
  existing-binary paths and manual-update guidance.
- **Worker diagnostics:** Standardized multi-line diagnostic assembly with
  Qt-native string literals without changing the captured error or warning
  content.
- **Worker/UI source split:** Refactored the yt-dlp process, metadata, output,
  download-row progress, and main-window download wiring into focused source
  units; added a bounded diagnostic-tail helper and test-only fake yt-dlp.
- **Playlist probe regression coverage:** Added a real slow-probe smoke test
  covering the 45-second watchdog, ordinary-URL fallback, and explicit-playlist
  failure classification.
- **Cross-platform browser companion:** Package the native-messaging host on
  Linux and macOS, register exact Chrome/Chromium origins for the current user
  on all supported desktop platforms, and use a persistent AppImage wrapper so
  Linux registrations do not point into a temporary mount.
- **Cross-platform process handling:** Keep worker/helper process groups
  terminable on POSIX systems and normalize internal Qt paths independently of
  native display separators.

## [1.2.42] - 2026-08-29

- **Windows Debug image support:** Restore Qt JPEG and PNG plugins to the
  minimized vcpkg build so Download History and active-download thumbnails
  decode in local Debug builds. Disabled toolbar icons now remain visible.
- **Global worker limit:** Coordinate download-worker slots across concurrent
  GUI and Discord/server-mode processes so `max_threads` is enforced globally.
- **Chrome browser companion:** Added a bounded native-messaging host with
  client-scoped status/cancellation and request-scoped, host-validated browser
  cookies. Cookie credentials stay out of queue backups and public snapshots
  and are cleaned up after use.
- **Chrome browser companion validation:** Enforce cookie URL path scope in the
  native host and add extension-side and desktop-side coverage for cookie
  scope, secure transport, expiration, size limits, redaction, and cleanup.
- **Cookie setup guidance:** The Start Download warning now explains the full
  browser-cookie setup path and links directly to Advanced Settings →
  Essentials.

## [1.2.41] - 2026-08-27

- **Release CI:** Fixed the cross-platform build failure caused by routing the
  gallery-dl startup version signal to the wrong Qt receiver type.
- **Extractor metadata:** Refreshed the bundled yt-dlp and gallery-dl extractor
  domain lists for the release build.

## [1.2.40] - 2026-08-27

- **Startup updates:** Defer binary update actions and the tool checklist until
  the application update decision is resolved, suppressing binary update UI
  while the application installer is being downloaded or launched.
- **Startup recovery:** Complete startup checks when yt-dlp is missing or its
  version probe fails, so the required-tools checklist can be shown.

## [1.2.39] - 2026-08-27

- **Tool setup:** Consolidated missing-tool and external-binary update prompts
  into one **Set Up Required Tools** checklist. It labels fresh installs versus
  existing-binary upgrades, keeps the recommended installer visible inline,
  and provides sequential one-click **Update All** handling without installing
  a duplicate app-managed copy over a detected external tool.
- **Tool setup presentation:** Refined the consolidated checklist with clearer
  status hierarchy, theme-aware contrast, compact sizing, and visible Browse
  recovery actions after updates complete.

## [1.2.38] - 2026-08-27

- **Linux release packaging:** Move unused Qt SQL drivers outside the
  linuxdeploy scan directory during AppImage creation. The previous filename
  workaround still allowed linuxdeploy to inspect those ELF files and fail on
  unavailable Mimer or Oracle client libraries.

## [1.2.37] - 2026-08-27

- **Linux release packaging:** Exclude Qt's unused Mimer, MySQL, and other
  non-SQLite drivers from the linuxdeploy scan. The Mimer driver references the
  unavailable proprietary `libmimerapi.so` library and previously blocked the
  otherwise successful Ubuntu build.
- **Release build speed:** Enable Qt SDK caching and a persistent Linux ccache
  across release runs, and use Ninja for the Linux build graph when available.

## [1.2.36] - 2026-08-27

- **Linux release CI:** Use aqtinstall's current `linux_gcc_64` host
  architecture for the Qt 6.10.2 SDK. The older `gcc_64` alias resolves to the
  unavailable `qt_base` package and prevents Ubuntu release jobs from starting.

## [1.2.35] - 2026-08-27

- **Release CI reliability:** Restore the Python 3.11 environment after Qt
  installation so extractor refresh uses the same interpreter that received
  `yt-dlp` and `gallery-dl`.
- **Linux release CI:** Let aqtinstall select the platform's default Qt base
  archives; the filtered `qtbase` name was not available in the Qt 6.10.2
  Linux metadata.

## [1.2.34] - 2026-08-27

- **Release build performance:** Windows and Linux release jobs now use the
  pinned prebuilt Qt 6.10.2 SDK, MSVC parallel compilation, and an
  application-only build target with explicit host parallelism. This removes
  the source Qt build, duplicate Debug dependency builds, and test executable
  compilation from packaging.

## [1.2.33] - 2026-08-27

- **Deno updates:** WinGet-managed Deno updates now use the official stable
  installer when WinGet's catalog has not published the newest release yet,
  while WinGet remains available for initial installation. The Windows
  PowerShell download uses the built-in Windows curl client with retries,
  avoiding legacy web parsing and HTTP-stack compatibility failures.
  Suppressed duplicate generic process-error text for normal command failures.

- **Startup tool updates:** The startup outdated-binary prompt now provides an
  **Update Now** action for every detected binary and reuses the External Tools
  update implementation directly; the settings page remains available for
  alternate installation methods.

- **Non-interactive launches:** `--background` now follows the same
  non-interactive path as server/headless and API requests, suppressing modal
  playlist, runtime, missing-binary, duplicate, and download-error dialogs
  while exposing failures through the bridge/webhook diagnostic signal.

- **Progress stability:** The single Active Downloads progress bar now prefers
  aggregate multi-stream progress and ignores delayed lower values, so it does
  not jump backward when yt-dlp switches between video and audio streams.

## [1.2.32] - 2026-08-27

- **Release workflow:** Clarified that GitHub Actions is the normal packaging
  path; local release builds and headless tests are optional diagnostics.

- **Cross-platform binary installs:** External Tools now marks only app-local
  installers as **(Recommended)**, consistently targets the platform app-data
  `bin` folder for direct yt-dlp installs, and installs Deno there on Windows,
  macOS, and Linux without modifying the user's `PATH`. Package-manager
  choices remain explicit system-managed alternatives. Windows retains direct
  app-managed FFmpeg/FFprobe and gallery-dl installers.

- **Documentation efficiency:** Agent guidance now routes tasks to the smallest
  relevant reference; repeated API, architecture, and coding-policy prose was
  removed while the detailed behavior and settings references remain intact.

- **Headless build integration:** The Windows headless test runner now forwards
  the correct vcpkg setting to Visual Studio builds, removing the misleading
  manifest-disabled diagnostic without changing direct-Qt builds.

- **Regression tests:** Added coverage for retained auto-detected binary paths,
  Windows WinGet package discovery, the single-bar Active Downloads row, and
  the naturally sized External Binaries scroll document.

- **Debug configure recovery:** The VS Code debug configure task now detects
  incomplete CMake compiler metadata and automatically retries with `--fresh`
  in the standard `build-debug` directory.

- **External Tools layout:** The External Binaries group is now the scroll
  document directly, removing the intermediate layout whose expanded geometry
  fed back into the document height and created blank space after the final
  row. The document retains vertical scrolling, matches the viewport width,
  and derives its natural height from the width-constrained layout rather than
  from previously expanded row geometry.

- **FFmpeg updates:** Completed Windows standalone FFmpeg/FFprobe installations
  now remain selected after restart even with system-first preference enabled,
  and the paired FFprobe override is persisted with its `.exe` suffix. WinGet
  package paths now route through `winget upgrade`; standalone external copies
  no longer silently create a second app-local installation.

- **Active Downloads UI:** Removed the secondary aggregate progress bar so each
  download row presents one focused progress bar for its current transfer or
  processing stage.
- **Discord progress:** The webhook now carries aggregate multi-stream
  progress separately, allowing the bridge to avoid percentage resets during
  video/audio stream handoff without restoring the desktop bar.

- **External-tool dialog layout:** Long package-manager and PowerShell command
  previews now wrap at arbitrary characters inside a bounded, horizontally
  scroll-free preview area instead of expanding the install dialog off-screen.

- External-tool checks now time out cleanly, retain the exact installed/latest
  yt-dlp versions, and show a persistent prompt when a manually managed tool
  needs updating.

- **Windows build reliability:** The checked-in CMake presets now select the
  Qt MinGW compiler and Ninja explicitly, while CMake disables the optional
  compiler-predefines probe that can fail under Windows/libuv launches.
- **Qt deployment:** Windows post-build deployment now copies Qt plugins
  through a guarded CMake helper, preventing concurrent configure/build jobs
  from colliding while keeping runtime plugin deployment intact.
- **Qt dependencies:** The vcpkg manifest now opts into the exact Qt modules
  used by the application and tests instead of enabling Qt's full default
  feature set.

## [1.2.30] - 2026-08-25

- **Discoverability:** Reworked the public README around clear video-downloader,
  audio-downloader, playlist-downloader, gallery-downloader, yt-dlp GUI, and
  platform search terms; added prominent release/source links, project badges,
  use cases, and an FAQ. Added contributor, security, issue-template, and
  pull-request guidance so visitors have a clear path from discovery to safe
  adoption or contribution.
- **Project metadata:** Added descriptive CMake and vcpkg package metadata for
  the Qt desktop downloader and linked it to the canonical GitHub repository.
- **Licensing:** Project-authored source and assets are now released under
  GPL-3.0-or-later; external dependencies retain their own licenses.
- **Release distribution:** Release jobs now publish platform-specific SHA-256
  manifests beside packaged installers, AppImages, and DMGs, and the README
  links the companion Discord bridge for easier discovery.

- **Release tooling:** Added an explicit native-only `--target auto|windows|linux|macos` option to `build_release.py`, allowing macOS VM builds to select the macOS packaging path while rejecting unsupported cross-OS builds.
- **Release tooling:** Fixed version detection for the repository's multiline CMake `project()` declaration so the canonical release builder can package the declared application version.
- **Release tooling:** Added release-version monotonicity and tag/version consistency checks to prevent accidentally rebuilding an existing release number.

- **macOS CI:** Use Qt's published universal `clang_64` desktop archive on both native macOS runners, restoring Apple-Silicon release builds that failed when requesting the unavailable `clang_arm64` package.

- **Download progress recovery:** Native yt-dlp transfers now recover missing stream sizes from `formats` metadata and use bounded temporary-file polling when output goes quiet, preventing long downloads from appearing frozen.

- **Local API recovery:** Explicit non-interactive re-downloads now replace matching restored stopped/failed jobs instead of being rejected as paused; genuinely paused jobs remain protected, and rejected non-interactive duplicates emit terminal bridge diagnostics.
- **Duplicate prevention:** Queue, retry, active, paused, and archive checks now share normalized media identity comparison instead of relying on raw URL-string equality. The Discord bridge also collapses equivalent recovery entries using extractor-independent URL normalization.
- **Failure handling:** Disk-full diagnostics (`No space left on device`, errno/ENOSPC 28, and FFmpeg `-28`) now force terminal failure before metadata embedding or finalization.
- **Destination safety:** Existing completed files are preserved until a verified replacement has been moved or copied successfully; failed replacements restore the original destination.
- **Retry safety:** Retry/resume now checks the current active-item snapshot with normalized media identity, preventing equivalent active jobs from being re-enqueued.
- **Regression coverage:** Added focused queue-manager tests for equivalent-URL deduplication and terminal restored-item recovery, plus file-replacement tests that verify the previous destination survives missing or unsuccessful replacement output.
- **Power management:** Added cross-platform system idle-sleep inhibition for active downloads and post-processing, including the headless/server lifecycle used by the Discord bot. The display remains eligible for normal power-off, and the inhibitor is released on completion, cancellation, and shutdown.
- **Build:** Added the platform-specific power-management implementation and the Linux Qt D-Bus component.
- **Tests:** Fixed the new queue-state and temporary-cleanup tests by including the QtTest macros they use, allowing those targets to compile past source parsing and into AutoMOC.
- **Tests:** Windows test deployment now includes Qt's `qminimal.dll` and the
  required Qt runtime DLLs; the headless summary recognizes CTest exit-code
  failures as failed tests.
- **Tests:** Fixed YouTube short-link archive normalization and updated livestream MPEG-TS coverage for the remux-based argument path.
- **Tests:** Upgraded the headless runner with timestamped streaming output, build-before-test fail-fast behavior, end-of-run summaries, and a build-local suspects cache usable with `--suspects`.
- **Build:** Moved Qt test sources, fixtures, workflow templates, and the end-to-end server fixture from `src/tests/` to the top-level `tests/` directory.
- **Tests:** Registered download-manager playlist fallback, gallery-dl argument, and playlist-expansion parser coverage through the shared top-level test harness with an offscreen Qt environment.
- **Tests:** Added queue-backup persistence coverage for resume statuses, field round-tripping, invalid-entry filtering, and empty-queue cleanup, plus direct temporary-root ownership/fallback tests and negative aria2c recovery-boundary coverage.
- **Build:** Moved extractor maintenance scripts into `tools/`; Linux release tooling now caches linuxdeploy under `build-release/tooling/` and writes AppImages directly under `build-release/`.
- **Discord bridge:** Caller-supplied non-interactive job IDs are registered before enqueue validation, so validation, runtime-extraction, and missing-binary failures can emit terminal diagnostics for bridge cleanup.
- **Local API:** Added authenticated `POST /cancel` for tracked jobs; cancellation follows the normal manager process-tree and queue-state path and remains visible as `Cancelled` to webhook/status consumers.

- Moved the download counters and current-speed indicator into the footer's first row, with the exit-after-downloads switch remaining rightmost.
- Low-quality video warnings now show the downloaded title and provide a clickable HTTP/HTTPS source link when available.
- Fixed application updates with unfinished downloads by flushing resumable queue state and stopping downloader/helper process trees before launching the installer.
- Silent application updates now relaunch the freshly installed `LzyDownloader.exe` automatically.
- **CI/CD:** Linux AppImage packaging now selects qmake from the vcpkg Qt installation used to build the executable, prevents the linuxdeploy Qt-module mismatch, and supplies a fallback release body when tag-matched notes are absent.
- **CI/CD:** Linux AppImage packaging detects vcpkg's statically linked Qt build and skips linuxdeploy-plugin-qt, preventing `.a`/`.prl` SQL driver files from causing the Ubuntu `Invalid magic bytes in file header` crash; dynamic Qt builds still receive Qt/SQL deployment.
- **CI/CD:** The release workflow now supports manual build-only dispatches; GitHub Release assets remain tag-only.
- **Build:** Release and debug now use the standard MinGW/Ninja preset layout;
  debug uses local Qt only for host tools and no longer needs a separate local-Qt
  build directory.
- **CI/CD:** Tag releases now build separate Intel and Apple Silicon macOS app bundles, deploy Qt with `macdeployqt`, and publish architecture-labelled DMGs alongside the Windows and Linux assets.
- **Updater:** macOS update selection now requires a CPU-matching DMG and opens the downloaded disk image through Finder instead of treating it as an executable.
- **Portability:** Start-tab command previews and format probing now resolve generic binary names rather than Windows `.exe` names.
- **Documentation:** Reconciled the active documentation set with the cross-platform release workflow, static/dynamic Qt packaging rules, and platform-neutral executable resolution; historical changelog entries remain unchanged.

## [1.2.29] - 2026-08-24

- **CI/CD:** Windows release jobs now install NSIS before running the unified release builder, and local packaging accepts `makensis` from `PATH`.
- **CI/CD:** Linux release jobs now install the Qt/XCB development packages required when vcpkg builds Qt Base, preventing Ubuntu configuration failures caused by runtime-only X11/XCB libraries.
- **CI/CD:** Linux release jobs also install the `bison` and `flex` host tools required by vcpkg's PostgreSQL dependency during Qt SQL configuration.
- **CI/CD:** Linux release jobs explicitly install vcpkg's required archive utilities (`curl`, `tar`, `unzip`, and `zip`) for clean runners.
- **CI/CD:** Release automation now installs yt-dlp from the prerelease/nightly channel for current extractor/runtime validation.
- **CI/CD:** Headless test failures now use console-safe diagnostics on Windows instead of crashing while printing Unicode status symbols.
- **Documentation:** Reconciled the active documentation set with the release workflow's exact Linux prerequisites, prerelease yt-dlp validation, and packaging responsibilities.

- Fixed aria2c downloads that return without their expected temporary media file: the worker now retries once with yt-dlp's native downloader while retaining media partials and removing stale metadata sidecars.
- Fixed low-quality video warnings appearing for audio-only downloads whose metadata still contained the source video's height.
- Kept aria2c progress labels audio-oriented when audio extraction downloads a combined `video/*` source before extracting the audio.

## [1.2.27] - 2026-08-19

- Browser-cookie downloads now retry once without cookies when an uncapped or higher-capped bestvideo request resolves to a combined stream below 480p, covering cookie manifests that omit the adaptive formats needed to prove the downgrade. Direct format choices, caps at the selected resolution, and active livestreams are not changed, and no site-specific extractor client is hardcoded.
- Added regression coverage for adaptive-format detection, incomplete cookie manifests, explicit resolution caps, and downloads without cookie arguments.

## [1.2.26] - 2026-08-18

- Fixed abandoned-thumbnail finalization so the existing FFmpeg metadata rewrite consumes the tracked JPG as an attached-picture stream instead of deleting it without embedding.

## [1.2.24] - 2026-08-16

- Windows installer now offers to launch LzyDownloader after installation completes.
- Download History source URLs are now rendered as clickable hyperlinks that open in the default browser.
- Incomplete yt-dlp media is no longer treated as completed merely because a final path was printed. Missing fragments, empty data blocks, and invalid media input now fail before metadata embedding, with a diagnostic that distinguishes the transfer failure from a missing FFmpeg installation.
- Active documentation now agrees on recovery diagnostics, explicit upcoming-stream fallback metadata, Qt-native data-file locations, and the release-note file required by tag-based publishing.


## [1.2.23] - 2026-08-15

- Synchronized release metadata and Windows executable version information to `1.2.23` so CMake, vcpkg, the application binary, and installer packaging remain aligned.
- Refreshed the GitHub release notes workflow to publish the version-matched release description automatically.

## [1.2.22] - 2026-08-15

- Active Downloads rows now force their scroll content to shrink with the viewport, keeping right-side actions visible when the window is narrow.
- Playlist audio filenames now receive zero-padded playlist index prefixes by default (for example, `01 - Title.opus`); users can still disable the prefix in Download Options.
- Refreshed the bundled yt-dlp extractor domain data; the current Nitter entry is `nitter.nicfab.eu`.
- Audio playlist metadata now preserves an explicit track artist and falls back only to item-level artist/creator/channel/uploader fields, preventing playlist owners from being written as the track artist.
- Synchronized the active README, specification, architecture, settings, API, manifest, and release documentation with current runtime behavior.
- Windows FFmpeg/FFprobe updates now stage extracted binaries and retry locked-file replacement for up to one minute, avoiding immediate failure when a media process is still releasing the old executable.
- First interactive launch now provides a guided external-tool setup with system-first or app-managed-first selection only when system tools are detected, default optional-tool choices, and progress-backed provisioning. App-managed tool updates can run on a launch/daily/weekly cadence, while externally selected and package-managed tools retain explicit update confirmation.
- Discord bridge enqueue requests now carry an explicit archive-override confirmation, allowing intentional re-downloads to proceed without waiting for the GUI duplicate-download dialog.
- Documentation was reconciled across the active user, API, architecture, settings, specification, manifest, coding, and release guides; historical changelog entries remain unchanged.

## [1.2.20] - 2026-08-09

### Fixed
- Temporary download cleanup now uses one shared root resolver, removes empty folders when yt-dlp/gallery-dl cannot start, and asynchronously removes orphaned UUID folders after queue restoration while preserving resumable stopped/failed downloads.
- Transient aria2c failures now use bounded retry/backoff settings and one delayed fallback to yt-dlp's native downloader for timeout, slow-transfer, network, and temporary-server failures. Automatic recovery removes stale `.info.json` sidecars while preserving media partials for resume, and reports the recovery status in the download row.
- Explicit yt-dlp premiere/upcoming diagnostics now survive playlist-probe fallback, keeping active livestreams on yt-dlp's native downloader instead of aria2c and reducing failures from rotating live manifests.

## [1.2.18] - 2026-08-05

### Fixed
- Terminal download finalization now removes the guarded per-download UUID temporary directory on failure exits while preserving stopped-download partial files for resume.
- Browser-cookie retry detection no longer treats ordinary metadata text such as "locked in a heated race" as a cookie failure. The worker now waits for process completion and retries only on explicit cookie/database/sign-in diagnostics, preventing a false retry state from leaving a download hanging.
- Active Downloads rows now compact correctly in narrow or half-screen windows: long titles wrap within the viewport and no longer push row actions into a horizontal scroll area.
- URLs containing a `/live/` path segment are no longer forced into livestream mode; yt-dlp metadata or explicit options now determine livestream behavior.
- Ordinary title text containing phrases such as `Starting in` or `Live in` no longer triggers the scheduled-livestream cookie/wait dialog.

### Changed
- Queued download rows now start loading available remote thumbnails immediately, including thumbnails preserved when a single-item playlist placeholder is updated.
- Playlist metadata probing now retains per-entry thumbnail data instead of using flat entries that often omit it.

## [1.2.15] - 2026-07-28

### Fixed
- Ordinary media URLs now recover from timed-out or transient playlist probes by falling back to the normal single-item yt-dlp worker.
- SponsorBlock and accurate section cuts now normalize audio timestamps during re-encoding, preventing A/V drift.

### Changed
- FFmpeg cut/filter work is capped at two worker threads and runs below normal priority on Windows to keep the desktop responsive.
- Release-build output remains in the invoking terminal on Windows.
- Playlist metadata expansion remains read-only when archive override is enabled.
- Validation probes omit browser cookies, and aria2c referers are emitted only for complete URL origins.

## [1.2.9] - 2026-07-23

### Changed
- Release builds now remain in the invoking terminal on Windows instead of opening a separate command window.
- Playlist metadata expansion now ignores the archive override so probing remains read-only and can inspect already archived entries.
- Validation and playlist-expansion probes now omit browser cookies so lightweight URL checks stay non-interactive and do not block on browser-profile locks.
- aria2c referer arguments are emitted only for URLs with both a scheme and host, avoiding malformed downloader arguments for relative or incomplete URLs.
- Tracking-parameter cleanup now uses Qt's default URL serialization after removing transient query items.
- Refreshed the bundled Nitter extractor domain to `nitter.arcticfoxes.net`.

## [1.2.11] - 2026-07-23

### Changed
- Synchronized release metadata to version `1.2.11` across the build configuration, generated version header, and vcpkg manifest.
- Refreshed the documentation for playlist validation, extractor handling, and download-argument safety so the architecture and specification stay aligned with current behavior.
- Kept metadata-only playlist expansion read-only while maintaining generic item-index hints and URL cleanup rules for real downloads.

### Fixed
- Versioned release assets now stay aligned with the application binary metadata, installer packaging, and update checks.

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
- **Browser-cookie retry**: yt-dlp workers retry once without browser-cookie options when explicit cookie-backed extraction/authentication diagnostics are reported, and add a clearer authentication tip when that fallback was needed.
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
