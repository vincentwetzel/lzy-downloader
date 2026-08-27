# LzyDownloader File Manifest

Use this page to locate code. Use [`SPEC.md`](SPEC.md) for behavior,
[`ARCHITECTURE.md`](ARCHITECTURE.md) for ownership/data flow, and
[`API_SURFACE.md`](API_SURFACE.md) for public interfaces. `AGENTS.md` routes
tasks to the smallest useful reference.

## Root and operational files

| Path | Responsibility |
|---|---|
| `main.cpp` | Startup, modes, and single-instance wiring |
| `CMakeLists.txt` | Build graph, targets, Qt modules, deployment hooks |
| `CMakePresets.json` | Supported local configure/build presets |
| `build_release.py` | Native release build, packaging, and version checks |
| `LzyDownloader.nsi` | Windows installer and silent relaunch |
| `cmake/deploy_openssl_runtime.cmake` | Windows Qt/OpenSSL/plugin deployment |
| `tools/` | Extractor refresh, shared parsing, and checksum helpers |
| `tools/configure_debug.ps1` | Debug configure recovery in `build-debug` |
| `.github/workflows/release.yml` | Release CI matrix and prerequisites |
| `tests/run_headless_tests.py` | Build-before-CTest runner and `--suspects` cache |
| `extractors_yt-dlp.json`, `extractors_gallery-dl.json` | Bundled extractor data |

Public/project guidance is in `README.md`, `CONTRIBUTING.md`, `SECURITY.md`,
`AGENTS.md`, `CHANGELOG.md`, `TODO.md`, and `UPDATE_AND_RELEASE.md`.

## Documentation

| Path | Responsibility |
|---|---|
| `docs/SPEC.md` | Stable functional/runtime/UI requirements |
| `docs/ARCHITECTURE.md` | System flow and component ownership |
| `docs/SETTINGS.md` | `QSettings` keys, defaults, and validation |
| `docs/API_SURFACE.md` | Public methods, signals, and API boundaries |
| `docs/CODING_STANDARDS.md` | C++/Qt, security, and test conventions |
| `docs/LANGUAGES.md` | Translation status and targets |
| `docs/CHANGELOG_ARCHIVE.md` | Historical entries; do not rewrite |

## Source layout

- `src/core/`: queue, archive, configuration, workers, finalization, binary
  resolution, API, and platform behavior.
- `src/core/download_pipeline/`: FFmpeg pipeline helpers.
- `src/ui/`: Qt Widgets, tabs, dialogs, and presentation builders.
- `src/utils/`: logging, discovery, parsing, and platform helpers.
- `tests/`: Qt tests, fixtures, and test-only helpers.

## High-value source entry points

| Area | Paths |
|---|---|
| Queue/archive/persistence | `src/core/DownloadManager.*`, `src/core/DownloadQueueManager*.cpp`, `src/core/DownloadQueueState.*`, `src/core/ArchiveManager.*` |
| Temp/finalization | `src/core/DownloadFinalizer.*`, `src/core/FileReplacement.*`, `src/core/DownloadTempCleanup.*`, `src/core/DownloadQueueManagerCleanup.cpp` |
| yt-dlp/gallery pipeline | `src/core/YtDlpWorker.*`, `src/core/YtDlpWorkerDiagnostics.cpp`, `src/core/YtDlpWorkerTransfers.cpp`, `src/core/GalleryDlWorker.*` |
| Probe/arguments/live state | `src/core/PlaylistExpansionWorker.*`, `src/core/PlaylistExpansionParser.*`, `src/core/YtDlpArgsBuilder.*`, `src/core/YtDlpLiveStatus.h` |
| Metadata/FFmpeg | `src/core/MetadataEmbedder.*`, `src/core/download_pipeline/FfmpegMuxer.*` |
| Tools/processes | `src/core/ProcessUtils.*`, `src/core/SmartBinaryResolver.*`, `src/core/BaseBinaryUpdater.*`, `src/core/StartupWorker.*` |
| API/update/power/logging | `src/core/LocalApiServer.*`, `src/core/AppUpdater.*`, `src/core/PowerInhibitor.*`, `src/utils/LogManager.*` |
| Main UI | `src/ui/MainWindow.*`, `src/ui/MainWindowUiBuilder.*`, `src/ui/StartTab.*`, `src/ui/ActiveDownloadsTab.*`, `src/ui/DownloadItemWidget.*` |
| Settings UI | `src/ui/advanced_settings/*`, `src/ui/InitialBinarySetupDialog.*` |

## Regression-test index

Tests are registered with `lzy_add_test(...)` and should use isolated temp
paths/headless Qt. Main focused files are:

| Concern | Test |
|---|---|
| Manager/probe/gallery/playlist | `TestDownloadManager.cpp`, `TestGalleryDlArgsBuilder.cpp`, `TestPlaylistExpansionParser.cpp` |
| Queue/archive/temp/replacement | `TestDownloadQueueManager.cpp`, `TestDownloadQueueState.cpp`, `TestDownloadTempCleanup.cpp`, `TestFileReplacement.cpp` |
| Worker/tools/power | `TestYtDlpWorker.cpp`, `TestProcessUtils.cpp`, `TestPowerInhibitor.cpp` |
| UI | `TestUIWidgets.cpp` |

When adding or moving a major file, update this manifest and
`docs/ARCHITECTURE.md` together. Keep generated/build paths out of this index.
