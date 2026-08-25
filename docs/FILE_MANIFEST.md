# LzyDownloader File Manifest

This document is a quick file-to-responsibility index for the C++ port. It is intentionally narrow in scope:
- `docs/FILE_MANIFEST.md` answers "where does this live?"
- `docs/ARCHITECTURE.md` answers "how does it work?"
- `docs/SPEC.md` answers "what must it do?"

## Root Files
- `README.md`: Project overview, build, usage, configuration, and release checklist.
- `LICENSE`: GPL-3.0-or-later notice for project-authored source and assets, with third-party licensing guidance.
- `CONTRIBUTING.md`: Contributor setup, testing, issue quality, and pull-request guidance.
- `SECURITY.md`: Private vulnerability-reporting guidance and handling rules for sensitive diagnostics.
- `AGENTS.md`: Canonical operating instructions for AI contributors.
- `CHANGELOG.md`: User-facing release history and notable completed changes.
- `TODO.md`: Active maintenance items, planned work, and known gaps.
- `UPDATE_AND_RELEASE.md`: Release/operator workflow reference.
- `CMakeLists.txt`: Primary build graph and executable/library registration.
- `build_release.py`: Native-only release build and packaging orchestration, including the explicit `--target` selector, static/dynamic Qt detection, and Linux AppImage linuxdeploy handling.
- `tools/`: Extractor-list maintenance scripts and their shared domain-parsing utility; generated extractor JSON remains at the root as bundled runtime data.
- `tools/generate_release_checksums.py`: Standard-library release helper that writes platform-specific SHA-256 manifests for packaged assets.
- `docs/assets/screenshots/lzydownloader-interface.png`: Approved public product screenshot showing the native download interface and sorting rules.
- `docs/assets/social-preview.png`: Approved wide branded project preview for README sharing and GitHub social-preview upload.
- `.github/workflows/release.yml`: Tag/manual Windows, Linux, Intel macOS, and Apple Silicon macOS build matrix, CI-only Linux vcpkg prerequisites, Windows NSIS/Qt setup, universal-Qt macOS DMG setup, prerelease yt-dlp validation, matching-qmake Linux packaging, fallback release notes, and tag-only asset publication.
- `.github/ISSUE_TEMPLATE/`: Structured bug-report and feature-request forms that keep public support requests actionable.
- `.github/pull_request_template.md`: Pull-request checklist for tests, documentation, and sensitive-data handling.
- `release-notes/<tag>.md` (when supplied): Version-matched release description consumed by the GitHub Release job; CI creates a minimal fallback in the runner when absent. This is not an application runtime resource.
- `main.cpp`: Application entry point and single-instance/bootstrap wiring.

## Documentation
- `docs/ARCHITECTURE.md`: System design, component responsibilities, and data flow.
- `docs/SPEC.md`: Functional requirements, behavior guarantees, and product rules.
- `docs/SETTINGS.md`: Settings schema, keys, groups, and validation rules.
- `docs/API_SURFACE.md`: Public class methods, signals, and cross-component behavioral contracts.
- `tests/run_headless_tests.py`: Timestamped build/CTest orchestration, fail-fast compilation, summaries, and the previous-failure suspects cache.
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
- `src/core/PowerInhibitor.*`: Platform-native system idle-sleep inhibition for active GUI and headless/server downloads.
- `src/core/DownloadFinalizer.*`: Background verification, destination moves, and guarded terminal temporary-directory cleanup.
- `src/core/FileReplacement.*`: Recoverable replacement of an existing destination after verified download output is ready.
- `src/core/ArchiveManager.*`: SQLite archive persistence and normalized media identity comparison shared by duplicate checks.
- `src/core/DownloadQueueManager.*`: Queue, active, paused, retry, and archive duplicate-state checks using shared media identity.
- `src/core/DownloadQueueManagerRecovery.cpp`: Explicit re-download recovery for matching restored stopped/failed queue entries.
- `src/core/YtDlpWorker.*`: yt-dlp process handling, output parsing, progress classification including audio-aware combined-source labels, metadata-size and bounded temporary-file progress recovery, browser-cookie failure and metadata-backed degraded-format recovery, and bounded aria2c-to-native recovery.
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
- `src/core/LocalApiServer.*`: Localhost API server/auth handling, enqueue routing, tracked-job cancellation, and status snapshots.
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
- `tests/TestYtDlpWorker.cpp`: Regression coverage for native/aria2 progress, metadata-size fallback, audio-aware combined-source labels, diagnostics, transient downloader recovery, and cookie-backed degraded-format recovery exclusions.
- `tests/TestDownloadQueueState.cpp`: Queue-backup serialization, resume-status mapping, malformed-entry filtering, and empty-queue cleanup coverage.
- `tests/TestDownloadQueueManager.cpp`: Equivalent-URL deduplication across active/retry state and explicit stopped/failed re-download recovery coverage.
- `tests/TestFileReplacement.cpp`: Existing-destination preservation when replacement succeeds or the verified source is missing.
- `tests/TestDownloadTempCleanup.cpp`: Temporary-root fallback, owned-directory cleanup, and orphan-sweep preservation coverage.
- `tests/TestPowerInhibitor.cpp`: Idempotent acquire/release coverage for the platform sleep-inhibition helper; unsupported desktop services remain a valid best-effort outcome.
- `extractors_yt-dlp.json` / `extractors_gallery-dl.json`: Bundled extractor-domain data used by URL validation, smart type selection, and Supported Sites UI.

## Working Rule of Thumb
- If you need to change behavior, start in `docs/SPEC.md`.
- If you need to find code, use this manifest first, then jump to the relevant `src/` entry point.
- If you add or move major files, update this manifest and `docs/ARCHITECTURE.md` together.
- Keep the manifest limited to real source paths. Historical or generated
  paths should be described in `docs/CHANGELOG_ARCHIVE.md`, not listed as
  current entry points.
