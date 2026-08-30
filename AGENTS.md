# LzyDownloader C++ Agent Guide

Read `TODO.md` first. Load only the references needed for the task; do not
read the whole documentation set by default.

| Task | Reference |
|---|---|
| Behavior, pipeline, recovery, or UI | `docs/SPEC.md` |
| Ownership or file location | `docs/ARCHITECTURE.md`, `docs/FILE_MANIFEST.md` |
| Settings schema | `docs/SETTINGS.md` |
| Methods, signals, or Local API | `docs/API_SURFACE.md` |
| C++/Qt, security, or tests | `docs/CODING_STANDARDS.md` |
| Build or release | `UPDATE_AND_RELEASE.md` |

`README.md` is the user-facing overview; `CONTRIBUTING.md` is the contributor
workflow. Canonical ownership is: `SPEC` = behavior, `SETTINGS` = schema,
`API_SURFACE` = interfaces, `ARCHITECTURE` = responsibilities, and
`CODING_STANDARDS` = implementation rules. `docs/CHANGELOG_ARCHIVE.md` is
historical and must not be edited.

## Boundary

LzyDownloader is a Qt 6 Widgets/C++20 desktop downloader using yt-dlp,
gallery-dl, FFmpeg, and SQLite. Keep path/process handling portable, preserve
the `download_archive.db` schema, and use Qt-native `QSettings` INI semantics
(Python `configparser` compatibility is not required).

Windows, Linux, and macOS are first-class supported platforms. Every runtime,
packaging, browser-integration, and documentation change must preserve all
three platforms; OS-specific behavior must be isolated behind explicit
platform branches with a tested fallback or a documented release prerequisite.

## Documentation security

- Never commit personal or machine-specific absolute paths, usernames, home
  directories, workspace locations, or environment dumps to repository
  documentation, examples, logs, or release instructions.
- Use repository-relative paths, environment variables, placeholders such as
  `<repository-root>`, or commands that discover the current repository (for
  example `git rev-parse --show-toplevel`).
- Any path used in a security or release check must be derived at runtime and
  validated by file names/content, not hardcoded to a developer's machine.
- Treat paths copied from CI logs or local terminals as sensitive until
  sanitized. Review `git diff --check` and search changed documentation for
  drive-letter paths, Unix home-directory patterns, and user-profile paths
  before committing.

## Invariants

- Keep GUI work responsive and QWidget access on the GUI thread. Network,
  filesystem, database, and child-process work is asynchronous/off-thread.
- Preserve `temp -> stable verification -> destination`. Failed terminal
  cleanup removes only the owned UUID folder; stopped jobs retain partials.
  Orphan cleanup preserves roots, symlinks, non-UUID folders, and protected IDs.
- Keep extractor handling generic: no hostname-specific arguments, referers,
  downloader choices, workarounds, or livestream rules. Use metadata/settings
  and generic URL shape hints only.
- Use normalized media identity across queue, active, paused, retry, and
  archive checks. Archive override can replace restored stopped/failed entries,
  never genuine paused entries; retry/resume checks the current active snapshot.
- Playlist probes are asynchronous/read-only. Ordinary URLs may fall back from
  transient probe errors; explicit playlist URLs and missing yt-dlp fail.
  Probes do not create temp data, limit items, or perform archive writes.
- Treat disk-full, missing/empty/invalid media, invalid-input FFmpeg, and
  critical extractor errors as terminal before metadata embedding, even if a
  final path was printed. Preserve actionable diagnostics.
- Preserve metadata-driven live state and allow cookie/livestream-wait recovery
  only once each with explicit generic evidence. Parse native, fragment,
  template, aria2c, and indeterminate progress; recover sizes from formats or
  bounded owned `.part` polling.
- Show queue rows/thumbnails immediately. Keep compact rows/action controls
  visible with no horizontal scroll; render one detailed `ProgressLabelBar`.
  Preserve audio artist fallback, thumbnail `attached_pic` remuxing, bounded
  cuts, deferred queue saves, and terminal state before non-interactive quit.
- Local API is localhost-only, bearer-authenticated, bounded, and
  non-interactive; route cancellation through the manager. Validation,
  duplicate, binary, runtime, and terminal failures must remain observable to
  non-interactive callers without opening modal dialogs. Inhibit idle sleep
  (not display power-off) during GUI/server/headless/background downloads and
  release it on completion, cancellation, and shutdown.

## Implementation and handoff

- Explicit binary paths win; preserve resolver ownership tracking, package
  manager rules, paired Windows `.exe` paths, and standalone FFmpeg safety.
  Do not add runtime dependencies without approval; keep Qt/SQLite/image
  plugins/OpenSSL deployment self-contained.
- Add new source files or Qt modules to `CMakeLists.txt`. Use RAII and guarded
  QObject lifetimes, `Q_INVOKABLE` for queued `invokeMethod` targets, escaped
  rich text, complete HTTP/HTTPS links, and `QDebug` for non-trivial logic.
- Keep source files under 500 lines and Markdown under 100 KB. Add focused
  isolated-temp regression tests with headless Qt (`QT_QPA_PLATFORM=minimal`). Update affected docs and
  changelog/TODO entries as appropriate; synchronize `ARCHITECTURE` and
  `FILE_MANIFEST` when ownership or locations change.
- Keep documentation token-efficient: prefer one canonical statement,
  concise bullets/tables, and links to detailed references over duplicated
  prose. Remove stale or completed guidance without omitting behavioral
  contracts merely to shorten a document.
- Releases are CI-first: prepare synchronized CMake/vcpkg/changelog/release-note
  inputs, then provide the commit and tag push commands so GitHub Actions can
  build and publish the release. Do not run `python build_release.py` locally
  or push commits/tags unless the user explicitly requests a local validation
  or a different release workflow.

## High-value locations

| Area | Location |
|---|---|
| Queue/archive | `src/core/ArchiveManager.cpp`, `DownloadQueueManager*.cpp` |
| Finalization | `src/core/DownloadFinalizer.*`, `FileReplacement.*`, `DownloadTempCleanup.*` |
| Workers | `src/core/YtDlpWorker.*`, `YtDlpWorkerProcess.cpp`, `YtDlpWorkerProcessOutput.cpp`, `YtDlpWorkerInfoJson.cpp`, `YtDlpWorkerProcessHelpers.h`, `YtDlpWorkerDiagnostics.cpp`, `YtDlpWorkerTransfers.cpp`, `DiagnosticTail.h` |
| Arguments/live | `src/core/YtDlpArgsBuilder.*`, `YtDlpLiveStatus.h` |
| UI/binaries | `src/ui/MainWindowUiBuilder.*`, `MainWindowConnections.cpp`, `MainWindowDownloadConnections.cpp`, `ActiveDownloadsTab.*`, `DownloadItemWidget.cpp`, `DownloadItemWidgetProgress.cpp`, `DownloadItemWidgetIcons.h`, `src/ui/advanced_settings/BinariesPage.*` |
| Browser companion | `src/integration/BrowserNativeMessagingHost.cpp`, `src/integration/BrowserCookieFile.*`, and the separate browser-extension checkout |
| Tests/release | `tests/`, `tests/run_headless_tests.py`, `build_release.py`, `.github/workflows/release.yml` |
