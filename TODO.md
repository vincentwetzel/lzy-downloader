# LzyDownloader C++ Port TODO

## In Progress
- [ ] Refactor and split large `.cpp` files above or approaching 500 lines (e.g., `DownloadItemWidget.cpp`, `MainWindowConnections.cpp`, and `YtDlpWorkerProcess.cpp`) to preserve optimal AI context limits.
- [ ] Split `ProcessUtils.cpp` after the external-binary resolver expansion; the file is currently above the 500-line guidance and should move version parsing/probing into a focused helper.
- [ ] Review `AppUpdater` release-flow UX and diagnostics now that platform-specific updater assets are supported for Windows, Linux, and macOS.
- [ ] Evaluate whether `YtDlpWorker` should expose a reusable capped diagnostic tail helper so warning/error retention stays consistent across workers.

## Planned / Future Enhancements
- [ ] Implement translations for supported interface languages (see `docs/LANGUAGES.md`).
- [ ] Integrate Qt Linguist (`.ts`/`.qm` compiler steps) into `CMakeLists.txt` build automation.

## Completed
- [x] Updated external-binary docs for longer environment-aware version probes and the install-success repair/standalone warning flow.
- [x] Refreshed bundled extractor domains for Nitter and Zoom coverage updates.
- [x] Documented generic aria2c referer propagation and FFmpeg single-file copy/remove fallback with transient cleanup retries.
- [x] Documented generic playlist/carousel item index handling, metadata-only probe cleanup, output-template metadata fallbacks, and browser-cookie retry behavior.
- [x] Documented cross-platform updater asset selection, bounded worker diagnostics, gallery stderr trimming, and Qt 6.2-safe cleanup/comparison updates.
- [x] Release packaging: Fixed Linux AppImage icon resolution by using a clean `build-release/AppDir` staging directory and a generated desktop file whose icon entry matches the resized release PNG.
- [x] CI/CD: Fixed the release workflow Qt setup to avoid unused/non-installable Qt module requests and use the available Qt 6.6 Windows MSVC archive, relying on Qt Base for Core/Widgets/Network/Sql.
- [x] CI/CD: Added tag-triggered GitHub Actions release automation that runs the unified release builder and uploads Windows installer plus Linux AppImage assets to GitHub Releases.
- [x] Performance: Optimized hot-path stdout line parsing in `YtDlpWorkerProgress.cpp` using regex-free string parsing and zero-allocation views.
- [x] Documented yt-dlp diagnostic classification updates for critical extractor failures, FFmpeg-specific guidance, impersonation recommendations, and SponsorBlock cut argument changes.
- [x] Documented v1.1.69 behavior for auto-detected binary path ownership, completed-with-warning downloads, retry-based cleanup, livestream wait metadata safety, and Discord bridge progress refreshes.
- [x] Refactored external binary resolution through `SmartBinaryResolver` so manual overrides win, the app-local `bin` folder is searched first, stale native-settings ghosts are purged, and multiple candidates can be selected by newest usable version.
- [x] Simplified External Binaries update handling so version probes, update availability checks, and package-manager/tool-native update commands share bounded process handling and clear status refreshes.
- [x] Aligned startup, runtime, and External Binaries page resolution so auto-detected best paths are persisted, version labels use compact tool-specific parsers, and update checks re-probe the active executable before comparing remote releases.
- [x] Complete codebase-wide performance optimizations, string hygiene, and SQLite database connection/lock safety.
- All completed phases and milestones are permanently archived in [CHANGELOG.md](CHANGELOG.md).
