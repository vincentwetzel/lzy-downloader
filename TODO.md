# LzyDownloader C++ Port TODO

## In Progress
- [x] Recover once from cookie-backed degraded progressive format selection when an uncapped or higher-capped bestvideo request resolves below 480p, including manifests that omit the better adaptive formats.
- [x] Add regression coverage for cookie-backed degraded-format detection, including adaptive-format, incomplete-manifest, explicit-cap, and no-cookie exclusions.
- [x] Make valid Download History source URLs clickable and open them in the default browser.
- [x] Keep the Active Downloads scroll content and rows constrained to the viewport so right-side Cancel/Retry and folder actions remain visible at compact widths.
- [x] Keep Active Downloads rows compact at narrow window widths so long titles do not hide row actions behind horizontal scrolling.
- [x] Display queued thumbnail URLs immediately, including preserving the thumbnail when a single-item playlist placeholder is updated.
- [x] Keep thumbnail metadata in asynchronous playlist probes so queued rows can display previews before download starts.
- [x] Remove per-download temporary directories on all terminal finalization failure paths while preserving stopped-download resume data.
- [x] Audit temporary-folder creation and cleanup paths; centralize root resolution, remove pre-start failure leftovers, and reconcile orphaned UUID folders after queue restoration.
- [ ] Verify direct-download fallback behavior with a slow playlist probe and an explicit playlist URL.
- [ ] Add an automated manager-level test for transient playlist-probe fallback and explicit playlist failure classification.
- [ ] Refactor and split large `.cpp` files above or approaching 500 lines (e.g., `DownloadItemWidget.cpp`, `MainWindowConnections.cpp`, and `YtDlpWorkerProcess.cpp`) to preserve optimal AI context limits.
- [ ] Split `ProcessUtils.cpp` after the external-binary resolver expansion; the file is currently above the 500-line guidance and should move version parsing/probing into a focused helper.
- [ ] Review `AppUpdater` release-flow UX and diagnostics now that platform-specific updater assets are supported for Windows, Linux, and macOS.
- [x] Make Windows FFmpeg/FFprobe updates resilient to transient executable locks by staging replacements and retrying bounded moves.
- [ ] Evaluate whether `YtDlpWorker` should expose a reusable capped diagnostic tail helper so warning/error retention stays consistent across workers.
- [x] Added bounded aria2 retry/backoff, transient aria2-to-native fallback, and stale metadata-sidecar cleanup while preserving resumable media partials.
- [x] Recover once with yt-dlp's native downloader when aria2c returns without the expected `.part` media output, while preserving partial downloads.
- [x] Preserve track-level artist metadata for audio playlists by using item-level yt-dlp artist/creator/channel/uploader fields and excluding playlist-owner fields.
- [x] Add first-launch external-tool provisioning with a system-first/app-managed-first choice, optional-tool selection, non-modal update guidance, and configurable automatic updates for app-managed tools.
- [x] Restrict low-quality resolution warnings to video downloads and preserve audio transfer labels when audio extraction uses a combined source.

- [x] Synchronize all active documentation with the current implementation on 2026-08-15, including first-launch binary provisioning, explicit Local API archive overrides, recovery diagnostics, and the platform data-directory layout. Historical changelog entries remain unchanged.
- [x] Allow Discord bridge enqueue requests to explicitly override completed archive entries without a GUI confirmation dialog.
- [x] Add a checked-by-default NSIS finish-page option to launch LzyDownloader after installation.
- [x] Install NSIS explicitly in Windows release CI and resolve `makensis` from `PATH` for package-manager installations.
- [x] Prevent incomplete yt-dlp media with a printed final path from entering FFmpeg metadata embedding; classify missing fragments, empty data blocks, and invalid media input as transfer failures with accurate diagnostics.
- [x] Preserve explicit yt-dlp premiere/upcoming metadata during playlist-probe fallback so active livestreams cannot be assigned aria2c.
- [x] Fix the existing abandoned-thumbnail remux path so tracked thumbnail sidecars are embedded as attached artwork before cleanup.

## Planned / Future Enhancements
- [ ] Implement translations for supported interface languages (see `docs/LANGUAGES.md`).
- [ ] Integrate Qt Linguist (`.ts`/`.qm` compiler steps) into `CMakeLists.txt` build automation.

## Completed
- [x] Prepared the v1.2.27 release metadata, changelog entry, and tag-matched GitHub release notes.
- [x] Enabled zero-padded playlist filename prefixes by default while preserving an explicit user opt-out.
- [x] Prevented false browser-cookie retries from matching ordinary metadata prose such as `locked in a heated race`; cookie fallback now uses explicit cookie/database/sign-in diagnostics and the normal process-finished path.
- [x] Prevented ordinary title phrases such as `Starting in` or `Live in` from triggering scheduled-livestream error handling.
- [x] Prevented ordinary URLs containing a `/live/` path segment from being misclassified as livestreams before yt-dlp metadata is available.
- [x] Kept `build_release.py` output in the invoking terminal by removing its unnecessary Windows shell bootstrap.
- [x] Fixed SponsorBlock/accurate-cut A/V synchronization by normalizing the audio timeline and bounded FFmpeg cut resource usage so post-processing does not monopolize the system.
- [x] Prevented transient playlist-probe timeouts on ordinary media URLs from being marked cancelled; they now fall back to the regular single-item downloader.
- [x] Updated the bundled Nitter extractor domain to `nitter.nicfab.eu` and kept validation / playlist-expansion probes read-only by omitting browser cookies.
- [x] Synchronized active documentation with the playlist filename-prefix default, current extractor data, temporary-folder lifecycle, and aria2c recovery behavior.
- [x] Kept playlist metadata expansion read-only when archive override is enabled, constrained aria2c referer generation to complete URL origins, and refreshed the bundled Nitter extractor domain.
- [x] Trimmed common tracking query parameters from yt-dlp launch URLs and added bounded false-offline livestream retry handling when wait-state probes fail.
- [x] Added a dedicated file manifest at `docs/FILE_MANIFEST.md` and redirected the main docs to use it as the quick path-to-code index.
- [x] Documented explicit full-playlist-only `folder.jpg` generation for audio batches and refreshed the extractor domain lists.
- [x] Updated external-binary docs for longer environment-aware version probes and the install-success repair/standalone warning flow.
- [x] Refreshed bundled extractor domains for Nitter and Zoom coverage updates.
- [x] Documented generic aria2c referer propagation and FFmpeg single-file copy/remove fallback with transient cleanup retries.
- [x] Documented generic playlist/carousel item index handling, metadata-only probe cleanup, output-template metadata fallbacks, and browser-cookie retry behavior.
- [x] Documented cross-platform updater asset selection, bounded worker diagnostics, gallery stderr trimming, and Qt 6.2-safe cleanup/comparison updates.
- [x] Release packaging: Fixed Linux AppImage icon resolution by using a clean `build-release/AppDir` staging directory and a generated desktop file whose icon entry matches the resized release PNG.
- [x] CI/CD: Fixed the release workflow Qt setup to avoid unused/non-installable Qt module requests and use the available Qt 6.6 Windows MSVC archive, relying on Qt Base for Core/Widgets/Network/Sql.
- [x] CI/CD: Install the vcpkg Qt Base XCB development prerequisites on Ubuntu release runners so the forced XCB backend can configure successfully.
- [x] CI/CD: Install Ubuntu host parser tools required by vcpkg's Qt SQL dependency graph.
- [x] CI/CD: Install vcpkg archive utilities explicitly on Ubuntu release runners.
- [x] CI/CD: Use yt-dlp's prerelease/nightly channel during release validation.
- [x] Documentation: Reconcile every maintained documentation surface with the release workflow's exact Linux dependencies and prerelease yt-dlp validation.
- [x] CI/CD: Added tag-triggered GitHub Actions release automation that runs the unified release builder and uploads Windows installer plus Linux AppImage assets to GitHub Releases.
- [x] Performance: Optimized hot-path stdout line parsing in `YtDlpWorkerProgress.cpp` using regex-free string parsing and zero-allocation views.
- [x] Documented yt-dlp diagnostic classification updates for critical extractor failures, FFmpeg-specific guidance, impersonation recommendations, and SponsorBlock cut argument changes.
- [x] Documented v1.1.69 behavior for auto-detected binary path ownership, completed-with-warning downloads, retry-based cleanup, livestream wait metadata safety, and Discord bridge progress refreshes.
- [x] Refactored external binary resolution through `SmartBinaryResolver` so manual overrides win, the app-local `bin` folder is searched first, stale native-settings ghosts are purged, and multiple candidates can be selected by newest usable version.
- [x] Simplified External Binaries update handling so version probes, update availability checks, and package-manager/tool-native update commands share bounded process handling and clear status refreshes.
- [x] Aligned startup, runtime, and External Binaries page resolution so auto-detected best paths are persisted, version labels use compact tool-specific parsers, and update checks re-probe the active executable before comparing remote releases.
- [x] Complete codebase-wide performance optimizations, string hygiene, and SQLite database connection/lock safety.
- All completed phases and milestones are permanently archived in [CHANGELOG.md](CHANGELOG.md).
