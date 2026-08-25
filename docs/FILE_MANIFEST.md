# LzyDownloader File Manifest

This document is a quick file-to-responsibility index for the C++ port. It is intentionally narrow in scope:
- `docs/FILE_MANIFEST.md` answers "where does this live?"
- `docs/ARCHITECTURE.md` answers "how does it work?"
- `docs/SPEC.md` answers "what must it do?"

## Root Files
- `README.md`: Project overview, build, usage, configuration, and release checklist.
- `AGENTS.md`: Canonical operating instructions for AI contributors.
- `CHANGELOG.md`: User-facing release history and notable completed changes.
- `TODO.md`: Active maintenance items, planned work, and known gaps.
- `UPDATE_AND_RELEASE.md`: Release/operator workflow reference.
- `CMakeLists.txt`: Primary build graph and executable/library registration.
- `build_release.py`: Release build and packaging orchestration, including static/dynamic Qt detection and Linux AppImage linuxdeploy handling.
- `tools/`: Extractor-list maintenance scripts and their shared domain-parsing utility; generated extractor JSON remains at the root as bundled runtime data.
- `.github/workflows/release.yml`: Tag/manual Windows, Linux, Intel macOS, and Apple Silicon macOS build matrix, CI-only Linux vcpkg prerequisites, Windows NSIS/Qt setup, hosted-Qt macOS DMG setup, prerelease yt-dlp validation, matching-qmake Linux packaging, fallback release notes, and tag-only asset publication.
- `release-notes/<tag>.md` (when supplied): Version-matched release description consumed by the GitHub Release job; CI creates a minimal fallback in the runner when absent. This is not an application runtime resource.
- `main.cpp`: Application entry point and single-instance/bootstrap wiring.

## Documentation
- `docs/ARCHITECTURE.md`: System design, component responsibilities, and data flow.
- `docs/SPEC.md`: Functional requirements, behavior guarantees, and product rules.
- `docs/SETTINGS.md`: Settings schema, keys, groups, and validation rules.
- `docs/API_SURFACE.md`: Public class methods, signals, and cross-component behavioral contracts.
- `docs/CODING_STANDARDS.md`: Contribution and implementation standards.
- `docs/LANGUAGES.md`: Planned translation coverage.
- `docs/CHANGELOG_ARCHIVE.md`: Historical changelog entries preserved for reference.

## Source Layout
- `src/core/`: Download pipeline, queueing, archive handling, configuration, binary resolution, and supporting logic.
- `src/ui/`: Qt Widgets UI, tabs, dialogs, and presentation-layer builders.
- `src/ui/MainWindowUiBuilder.*`: Main-window tabs, footer status indicators, and exit-after-downloads control layout.
- `src/utils/`: Shared helpers for logging, discovery, parsing, and platform utilities.
- `tests/`: Test fixtures and Qt test coverage, kept in a top-level test directory.

## High-Value Entry Points
- `src/core/DownloadManager.*`: Queue orchestration, download lifecycle, finalization flow, and type-aware terminal quality warnings (including title/source context).
- `src/core/DownloadFinalizer.*`: Background verification, destination moves, and guarded terminal temporary-directory cleanup.
- `src/core/YtDlpWorker.*`: yt-dlp process handling, output parsing, progress classification including audio-aware combined-source labels, browser-cookie failure and metadata-backed degraded-format recovery, and bounded aria2c-to-native recovery.
- `src/core/YtDlpWorkerDiagnostics.cpp`: Shared fatal/incomplete-media and bounded aria2c missing-output recovery classification used to reject stale final paths before FFmpeg metadata embedding or finalization.
- `src/core/YtDlpLiveStatus.h`: Narrow mapping from explicit yt-dlp premiere/upcoming diagnostics to live queue metadata during probe fallback.
- `src/core/DownloadQueueState.*`: Queue persistence to `downloads_backup.json`.
- `src/core/DownloadQueueManagerCleanup.cpp`: Asynchronous startup reconciliation of orphaned UUID folders while queue IDs are protected.
- `src/core/DownloadTempCleanup.*`: Shared temporary-root resolution, guarded deletion, and startup orphan-folder reconciliation.
- `src/core/ConfigManager.*`: `settings.ini` persistence and validation.
- `src/core/ProcessUtils.*`: Process helpers, binary discovery, and termination utilities.
- `src/core/SmartBinaryResolver.*`: External binary path resolution and auto-detected path ownership.
- `src/core/PlaylistExpansionWorker.*`: Playlist validation and expansion.
- `src/core/PlaylistExpansionParser.*`: yt-dlp JSON expansion parsing and playlist item selection.
- `src/core/YtDlpArgsBuilder.*`: Metadata/option-driven command-line construction for yt-dlp and aria2c, including replay-safe livestream classification.
- `src/core/download_pipeline/FfmpegMuxer.*`: Asynchronous FFmpeg muxing, progress, and final output handling.
- `src/core/MetadataEmbedder.*`: Metadata/thumbnail embedding post-processing, including the existing abandoned-thumbnail remux that maps tracked sidecars as `attached_pic` artwork.
- `src/core/AppUpdater.*`: Application update lookup, platform/CPU-matched artifact handling, and pre-launch handoff notification; macOS DMGs are opened through Finder. `MainWindow` owns queue/process shutdown for that handoff. `LzyDownloader.nsi` owns silent-update relaunch after replacement.
- `src/core/LocalApiServer.*`: Localhost API server and auth handling.
- `src/ui/MainWindow.*`: Main application shell, tab wiring, and global UI actions.
- `src/ui/StartTab.*`: Queue entry point and download submission controls.
- `src/ui/ActiveDownloadsTab.*` / `src/ui/DownloadItemWidget.*`: Responsive active-download rows, queued thumbnail previews, progress display, and row actions.
- `src/ui/advanced_settings/`: Advanced Settings pages, including template and binary management.
- `src/ui/advanced_settings/BinariesPage.*`: External-binary discovery, install/update actions, version status, and staged Windows FFmpeg replacement.
- `src/ui/InitialBinarySetupDialog.*`: First-launch binary preference and optional-tool selection UI that drives guided provisioning.
- `src/ui/advanced_settings/DownloadOptionsPage.*`: Download-option controls, including the enabled-by-default playlist-index prefix.
- `src/ui/`: Reusable widgets, tabs, dialogs, and row controls; resource files
  are kept in `src/ui/assets/` and `src/ui/resources.qrc`.
- `tests/`: Headless Qt tests for argument construction, parsing, persistence, UI state, binary/API behavior, and end-to-end behavior.
- `tests/TestDownloadManager.cpp`: Manager-level regression coverage for transient playlist-probe fallback and explicit playlist failure classification.
- `tests/TestGalleryDlArgsBuilder.cpp`: Gallery-dl argument and rate-limit construction coverage.
- `tests/TestPlaylistExpansionParser.cpp`: Metadata-to-queue mapping, playlist selection, thumbnail, and live-status parsing coverage.
- `tests/TestYtDlpWorker.cpp`: Regression coverage for native/aria2 progress, audio-aware combined-source labels, diagnostics, transient downloader recovery, and cookie-backed degraded-format recovery exclusions.
- `extractors_yt-dlp.json` / `extractors_gallery-dl.json`: Bundled extractor-domain data used by URL validation, smart type selection, and Supported Sites UI.

## Working Rule of Thumb
- If you need to change behavior, start in `docs/SPEC.md`.
- If you need to find code, use this manifest first, then jump to the relevant `src/` entry point.
- If you add or move major files, update this manifest and `docs/ARCHITECTURE.md` together.
- Keep the manifest limited to real source paths. Historical or generated
  paths should be described in `docs/CHANGELOG_ARCHIVE.md`, not listed as
  current entry points.
