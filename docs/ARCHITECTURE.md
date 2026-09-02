# LzyDownloader C++ Architecture

Use this page for ownership and data flow. [`FILE_MANIFEST.md`](FILE_MANIFEST.md)
is the path index, [`SPEC.md`](SPEC.md) is the behavior contract,
[`SETTINGS.md`](SETTINGS.md) is the schema, and [`API_SURFACE.md`](API_SURFACE.md)
is the callable-interface index. Do not duplicate those contracts here.

## System design

`LzyAppLib` contains `src/core/`, `src/ui/`, and `src/utils/`; `main.cpp` owns
startup and the executable. The UI submits options and renders signals. Core
managers own queue state, workers, archive access, and finalization. Utilities
provide process, logging, path, and platform helpers.

```text
StartTab / LocalApiServer / CLI
        -> MainWindow -> DownloadManager -> QueueManager -> ArchiveManager
                               |                 |
                       PlaylistExpansionWorker  |
                               |                 |
                   YtDlpWorker / GalleryWorker  |
                               |                 |
                     DownloadFinalizer -> FFmpeg/metadata -> destination
                               |
                 ActiveDownloadsTab / webhook / history
```

All network, filesystem, database, and child-process work is asynchronous or
off the GUI thread. Download and metadata workers own their child processes in
dedicated thread event loops; finalization and thumbnail file operations use
worker callbacks. Workers communicate through signals and queued calls, and
the UI is touched only on the GUI thread.

### Startup and modes

`main.cpp` enforces instances with `QSystemSemaphore` and `QSharedMemory`.
GUI, `--server`, `--headless`, and `--background` share preferences but use
separate runtime markers and `Server/` runtime state where applicable. A
second GUI launch forwards a direct URL through `QLocalSocket`.

Shutdown stops downloader/helper process trees, flushes resumable state, and
releases power inhibition. Non-interactive exit flushes terminal state before
`QCoreApplication::quit()`.

### Queue and media flow

`DownloadManager` registers a row immediately, resolves request-versus-persisted
playlist policy, then expands a playlist or starts a dedicated worker thread.
`DownloadQueueManager` owns ordering, local concurrency,
duplicate identity, retry/resume snapshots, and restored-item recovery;
`GlobalDownloadLimiter` coordinates worker-slot admission across separate
GUI/server processes. Non-interactive
validation, duplicate, binary, runtime, and terminal errors are emitted as
`nonInteractiveRequestFailed` for bridge/webhook consumers rather than shown in
modal dialogs. `ArchiveManager` owns normalized identity and SQLite history.
`DownloadFinalizer` verifies, sorts, embeds metadata, and moves/copies output.
`FileReplacement` protects an existing destination during intentional
replacement. Temp cleanup owns root resolution and guarded UUID-folder removal.

## Component ownership

| Component | Owns |
|---|---|
| `ConfigManager.*` | Validated Qt `QSettings`, defaults, reset, change signals |
| `ArchiveManager.*` | Schema-compatible SQLite history and media identity |
| `DownloadQueueManager.*` | Ordering, concurrency, duplicate checks, retry/resume |
| `GlobalDownloadLimiter.*` | Locked per-user process registry for cross-process worker-slot admission and stale-holder cleanup |
| `DownloadQueueManagerRecovery.cpp` | Restored stopped/failed replacement recovery |
| `DownloadQueueState.*` | Atomic `downloads_backup.json` save/load/restore |
| `DownloadQueueManagerCleanup.cpp`, `DownloadTempCleanup.*` | Async orphan reconciliation and guarded temp cleanup |
| `FileReplacement.*` | Verified destination replacement and rollback |
| `DownloadManager.*`, `DownloadManagerWorkers.cpp` | Scheduling, dedicated worker-thread lifecycle, terminal classification, power, video quality warnings |
| `DownloadManagerPlaylist.cpp`, `PlaylistExpansionWorker.*`, `PlaylistExpansionParser.*` | Read-only probing, item selection, placeholders, thumbnails, playlist metadata, fallback |
| `YtDlpArgsBuilder.*` | Settings/options to yt-dlp/aria2c arguments and replay-safe live classification |
| `DiagnosticTail.h`, `YtDlpWorker.*`, `YtDlpWorkerProcess.cpp`, `YtDlpWorkerProcessOutput.cpp`, `YtDlpWorkerInfoJson.cpp`, `YtDlpWorkerProcessHelpers.h` | Async yt-dlp process, bounded diagnostics, output/progress parsing, metadata loading, cookies, livestream wait, aria2c recovery |
| `YtDlpWorkerDiagnostics.cpp` | Fatal/incomplete-media, disk-full, and bounded recovery classification |
| `YtDlpWorkerTransfers.cpp` | Transfer-stage inference, including combined-source audio |
| `YtDlpLiveStatus.h` | Explicit premiere/upcoming diagnostic mapping |
| `GalleryDlWorker.*` | gallery-dl process and gallery output handling |
| `DownloadFinalizer.*` | Background verification, sorting, replacement, terminal cleanup, archive update |
| `MetadataEmbedder.*` | Worker-thread metadata/thumbnail rewrite, cancellation, and tracked `attached_pic` remux |
| `download_pipeline/FfmpegMuxer.*` | Async FFmpeg muxing and progress |
| `ProcessUtils.*`, `SmartBinaryResolver.*` | Process trees, environments, binary discovery, and ownership tracking |

### UI and integrations

| Component | Owns |
|---|---|
| `MainWindow.*`, `MainWindowConnections.cpp`, `MainWindowDownloadConnections.cpp`, `MainWindowUiBuilder.*` | Shell, tabs, footer, global actions, startup/external-tool update handoff, webhook wiring |
| `StartTab.*`, `start_tab/*` | URL submission, clipboard, playlist/runtime selection |
| `ActiveDownloadsTab.*`, `DownloadItemWidget.cpp`, `DownloadItemWidgetProgress.cpp`, `DownloadItemWidgetIcons.h` | Download rows, thumbnails, compact layout, one focused progress bar, actions, and newest-row visibility |
| `advanced_settings/*`, `MissingBinariesDialog.*` | Settings pages, templates, and consolidated binary setup/provisioning |
| `LocalApiServer.*` | Authenticated localhost enqueue/status/cancel, aggregate progress, and tracked-job signals |
| `integration/BrowserNativeMessagingHost.cpp`, `integration/BrowserNativeHostRegistration.*`, `integration/BrowserCookieFile.*` | Bounded cross-platform Chrome native-messaging bridge and registration, plus request-scoped cookie-file ownership; starts the validated headless server and relays allowlisted Local API operations |
| `AppUpdater.*`, `LzyDownloader.nsi` | Release lookup/handoff and Windows silent-install relaunch |
| `PowerInhibitor.*` | Platform idle-sleep inhibition |
| `LogManager.*` | Per-run logs and five-file startup retention |

### Tests and release support

`tests/` links `LzyAppLib`, registers tests with `lzy_add_test(...)`, and runs
with Qt's `minimal` platform plugin. `tests/run_headless_tests.py` owns
build-before-CTest execution,
timestamped output, summaries, and the failed-test cache. `CMakeLists.txt`
owns the build graph; `CMakePresets.json` owns supported local configure
presets;
`cmake/deploy_openssl_runtime.cmake` owns guarded Windows Qt/OpenSSL runtime
deployment;
`build_release.py` owns native build/version checks, while
`tools/release_packaging.py` owns Linux AppImage packaging; and
`triplets/*.cmake` owns optional release-only settings for local vcpkg builds;
`.github/workflows/tests.yml` owns the reusable full-suite CI job, while
`.github/workflows/release.yml` owns release prerequisites, the platform matrix,
artifact collection, and publication gating.

## Concurrency and deployment

Use worker threads, asynchronous `QProcess`/network APIs, or queued callbacks
for long operations. Coalesce high-frequency progress before QWidget updates,
but keep backend/API progress delivery independent. Mutexes use RAII and are
not held while emitting signals.
External processes have bounded watchdogs, UTF-8 line buffering, bounded
diagnostics, and process-tree cleanup. Qt SQL connections stay within their
creating thread. `GlobalDownloadLimiter` uses a short-lived lock and removes
holders whose process is no longer alive; manager shutdown releases the
process's reservations. GUI and server/headless/background downloads inhibit
idle sleep while active, without preventing normal display power-off.

Windows keeps required Qt plugins, SQLite, OpenSSL, Qt runtime, and MinGW
compiler runtime DLLs beside the executable. The deployment helper is also
used for test executables so headless runs do not depend on the developer shell.
Linux packaging selects qmake from the Qt SDK used for the release build,
deploys its dynamic Qt/SQLite runtime, and includes the browser host in the
AppImage. macOS packaging creates architecture-labelled bundles/DMGs and
places the browser host inside the app bundle before creating the DMG.
