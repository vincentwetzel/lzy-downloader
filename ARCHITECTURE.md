# LzyDownloader C++ Architecture

## 1. Overview
This document outlines the architecture for the C++ port of LzyDownloader. The application is built with **Qt 6 (Widgets)** and provides a graphical interface for downloading media using **yt-dlp** and **gallery-dl**. The design prioritizes performance, stability, and seamless compatibility with the original Python version.

## 2. System Design

### 2.1 Single Instance Enforcement
The application ensures that only one GUI instance can run at a time. This is achieved in `main.cpp` using `QSystemSemaphore` and `QSharedMemory`. A `QSystemSemaphore` with a unique key guards startup, and a `QSharedMemory` segment marks the active instance. If another instance is already running and the new process was launched with a direct URL argument, the new process forwards that URL to the active instance through a lightweight `QLocalSocket` handoff channel and exits. Non-URL duplicate launches still exit gracefully. **Note:** Launching the application with `--headless`, `--server`, or `--background` appends a `_Server` suffix to these locks, allowing a headless API server to run concurrently with the standard GUI. User preferences still come from the shared main `settings.ini`; server/headless runtime state is isolated under `Server/`.

### 2.2 Core Components
- **LzyAppLib Static Library**: All application code (excluding `main.cpp`) from `src/core/`, `src/utils/`, and `src/ui/` is compiled into a single static library, `LzyAppLib`. This library encapsulates the core business logic, UI components, and utility functions, providing a modular foundation for both the main executable and the test suite.
- **UI Layer (`src/ui/`):** Handles user interaction, input, and visual feedback using Qt Widgets. This layer is now part of the `LzyAppLib` static library.
  - **No Native Menus**: The application strictly avoids native OS menu bars (`QMenuBar`), utilizing in-app UI elements (buttons, tabs, sidebars) for all navigation and actions. For example, the Supported Sites dialog is launched from a dedicated button on the Start Tab.
  - **Start Tab operational controls**: Playlist logic, Max Concurrent, Rate Limit, and override-duplicate controls live directly on the Start tab and save instantly to `ConfigManager`, allowing the backend to react immediately instead of waiting for a separate Apply action.
  - **AdvancedSettingsTab navigation**: The left-side category list is a compact `QListWidget` whose stylesheet is rebuilt from `QPalette` values whenever the palette changes so it remains consistent with both light and dark themes.
  - **Runtime format selection**: Advanced Settings can defer the entire video/audio format decision until enqueue time by setting `Quality` to `Select at Runtime`; `DownloadManager` fetches format metadata and `MainWindow` presents `FormatSelectionDialog`, which enqueues one item per selected format and marks those selected format IDs as concrete downloader overrides so they do not loop back into the picker.
  - **Non-interactive enqueue path**: Direct CLI URL launches and Local API requests set a `non_interactive` option so automation can proceed without modal prompts. These requests force completed-archive override, use download-all playlist handling, skip runtime section/format/subtitle pickers, and downgrade UI-only warnings to log messages.
  - **Playlist prompt bypasses**: Non-interactive requests always process playlists as "Download All", and single-item playlists are treated as standalone media so users are not prompted for a playlist decision.
  - **Missing binary setup**: Startup checks, enqueue-time checks, and Start-tab format checks open `MissingBinariesDialog` when required tools are absent. The dialog presents a compact checklist and delegates install/browse actions to `BinariesPage`, so users can resolve missing tools without navigating through Advanced Settings first.
  - **Windows debug console toggle**: When the app owns its console window, Advanced Settings exposes `General/show_debug_console` and `MainWindow` can allocate, show, or hide that console at runtime without changing the executable type.
  - **UI Builders**: `MainWindowUiBuilder` and `StartTabUiBuilder` classes are introduced to encapsulate the creation and layout of UI elements for `MainWindow` and `StartTab` respectively, improving modularity.
  - **Sorting Rule Dialog**: The dialog for creating/editing sorting rules uses a `QScrollArea` with `QVBoxLayout` instead of `QListWidget` for smooth pixel-level scrolling without item-snapping. The scroll area is capped at 150-400px height with 4px spacing between conditions. Condition widgets use a `CONDITION_VALUE_INPUT_HEIGHT` constant (100px) for consistent text entry sizing via `setFixedHeight()`. Dialog minimum size is 650x500px.
  - **Active Downloads toolbar**: The monitoring tab now favors compact icon-driven controls, including Resume All plus a unified Clear Inactive action for finished, stopped, and failed rows. If a download item is added with an ID already visible in the tab, the existing row is removed before the replacement widget is inserted so placeholder refreshes and restored queue items do not duplicate UI rows.
- **Core Logic (`src/core/`):** Manages download queues, file operations, configuration, and external process execution.
- **Exit Cleanup:** `MainWindow::closeEvent` warns if downloads are queued or active, explains that state will be resumed on next launch, then calls `DownloadManager::shutdown()` to cleanly tear down active downloader/post-processing trees and flush terminal queue states to disk. It finally performs a catch-all sweep of its own `QProcess` children using `ProcessUtils::terminateProcessTree()`. **Note:** Headless or `--exit-after` automated shutdowns explicitly replicate this sequence before invoking `QCoreApplication::quit()`, as `quit()` natively bypasses window close events.
- **Utilities (`src/utils/`):** Provides helper functions for tasks like string manipulation and URL normalization.
- **Extractor Domain Loader:** `YtDlpJsonParser` loads the extractor-domain list from the app directory for clipboard auto-paste checks in `StartTab`.
- **Auto-paste Control:** `DownloadOptionsPage` saves `General/auto_paste_mode`, and `MainWindow` reacts to app focus/clipboard changes to route matching URLs to `StartTab` according to the selected mode, using a brief debounce plus duplicate queue checks instead of a long lockout. The same page also stores `DownloadOptions/ffmpeg_cut_encoder` and `DownloadOptions/ffmpeg_cut_custom_args`, which let yt-dlp's accurate SponsorBlock cut pass use hardware or custom FFmpeg output arguments. Hardware encoder choices are populated from asynchronous FFmpeg encoder and local GPU probes so irrelevant options stay hidden.
- **Temporary File Isolation:** yt-dlp downloads are isolated under a per-item UUID subfolder of the configured temporary directory. `YtDlpArgsBuilder` also injects the item ID as `%(lzy_id)s` so advanced output templates can opt into collision-proof names, and `DownloadFinalizer` removes the UUID temp folder once finalization and cleanup finish.
- **External Binaries:** Relies on user-installed binaries (`yt-dlp`, `ffmpeg`, `ffprobe`, `deno`, `gallery-dl`, `aria2c`) found in system paths or configured manually. **`deno` is used as the JavaScript runtime for yt-dlp's YouTube challenge solver (`--js-runtimes deno:...`).** The application includes `BinaryFinder` for startup discovery, `ProcessUtils` for runtime resolution, `BinariesPage` for status/version/install/update UX, and `MissingBinariesDialog` for guided setup when required tools are absent. Package-managed installations are detected and are not overwritten by the in-app updater.
- **Local API Server:** `LocalApiServer` is an optional, localhost-only `QTcpServer` on port `8765`. GUI mode starts it only when `General/enable_local_api` is enabled; `--server` and `--headless` start it automatically without changing that GUI preference. It generates or loads `api_token.txt` from the app-local data directory, using `Server/api_token.txt` for server/headless runtime isolation, requires `Authorization: Bearer <token>`, accepts `POST /enqueue` with `url` plus optional `type` (`video`, `audio`, or `gallery`), and exposes `GET /status` snapshots sourced from download manager signals. `MainWindowDiscord.cpp` also mirrors queue state to the local Discord bridge with compact sanitized status strings, preserved progress across partial queue refreshes, explicit playlist parent mapping, and terminal completion/cancellation states retained long enough for bridge clients to observe them.
- **App Update Lookup:** `AppUpdater` checks the current lowercase GitHub repo slug first and can fall back to legacy repo API URLs so releases remain discoverable across repository renames. Version tags are normalized and compared numerically, and installer selection prefers `LzyDownloader-Setup-*.exe` assets.
- **Error Dialog UX:** `DownloadManager` always forwards the active item's metadata with detected yt-dlp errors, and `MainWindow` renders rich-text popups that can include a clickable source URL for retry/open workflows.
- **Qt SDK Discovery:** `CMakeLists.txt` auto-adds Qt search prefixes from `Qt6_DIR`/`QT_DIR`/`QTDIR` environment variables plus common Windows installs such as `C:\Qt\6.*\msvc2022_64`, which keeps CLion/Ninja configure steps working even when the IDE does not inherit a Qt kit path.

### 2.4 Window/Tray Lifecycle
- The main window close action (`X`) performs an application exit after temp-file safety checks.
- The tray icon remains available for manual show/hide and quit commands, but close-to-tray behavior is not used.

### 2.3 Data Flow
1.  **Input:** User enters a URL in `StartTab`, passes one as a direct CLI argument, or submits it to the optional localhost API.
2.  **Immediate Queue Feedback:** Download item appears instantly in Active Downloads tab with "Checking for playlist..." status. Item is added to `m_downloadQueue` immediately and tracked in `m_pendingExpansions` map.
3.  **Validation/Expansion:** `PlaylistExpander` validates the URL and checks for playlists asynchronously. **PlaylistExpander now uses `YtDlpArgsBuilder` to construct the full yt-dlp command (including `--js-runtimes deno:...`, `--cookies-from-browser`, `--ffmpeg-location`, etc.) so playlist expansion matches the actual download configuration.**
4.  **Runtime Selection Gate:** If Advanced Settings quality is set to `Select at Runtime` for video/audio downloads, `DownloadManager` asynchronously fetches `yt-dlp` format metadata and `MainWindow` shows `FormatSelectionDialog`. Each selected format becomes its own queued item. When section downloads are enabled, `MainWindow` also presents `DownloadSectionsDialog` (which includes instructions on how to disable the prompt), captures both the raw section expression and a filename-safe section label, and re-enqueues the item with both values. Non-interactive requests bypass these dialogs and fall back to automatic/default downloader choices.
5.  **Queue Update:** For single videos, status updates from "Checking for playlist..." to "Queued". For playlists, placeholder is removed and individual track items are added.
6.  **Execution:** `DownloadManager` spawns a worker (`YtDlpWorker` or `GalleryDlWorker`) for each item via deferred `QMetaObject::invokeMethod` call to avoid GUI thread blocking.
7.  **Progress:** The worker parses native yt-dlp progress from `stderr` (`[download] XX.X% of YY.YMiB at ZZ.ZMiB/s ETA 0:00`) or aria2 progress (`[#XXXXX ...]`) and emits progress signals (`progressUpdated`, `speedChanged`, etc.). `YtDlpWorker` tracks each `[download] Destination:` target and aria2 `FILE:` target so auxiliary transfers like thumbnails, subtitles, and `.info.json` writes can surface as status updates without falsely pinning the main media bar at 100%, treats extraction/setup stages as indeterminate instead of leaving the UI on a stale queued `0%`, understands fragment-based native downloader lines, prefers requested `format_id` matches from temp filenames such as `.f251.webm.part`, then emitted total-size matches, then yt-dlp's announced format list plus aria2 command-line `itag`/`mime` hints when `requested_downloads` is missing, preserves that inferred order if a later `info.json` still omits `requested_downloads`, before relying on ambiguous container extensions or plain progress-reset ordering, and falls back to the `requested_downloads` metadata order when progress restarts onto the next primary stream without a fresh destination line. **YtDlpWorker includes diagnostic logging for process state changes, stderr/stdout data received, and progress parsing.**
8.  **UI Update:** `ActiveDownloadsTab` receives signals and updates the corresponding progress bars, labels, and displays a thumbnail preview on the left side of the download GUI element. The UI now treats worker/finalizer status text as the source of truth and no longer guesses video-vs-audio phases from progress resets. **Thumbnails are loaded from the `thumbnail_path` field in progress data, which is emitted by `YtDlpWorker` when yt-dlp converts the thumbnail during the download process.** For multi-stream downloads, the main progress bar remains scoped to the currently active stream while a slim secondary bar can show overall whole-job progress across the requested primary streams. For scheduled livestreams in a `[wait]` state, `YtDlpWorker` fetches pre-wait metadata via the YouTube oEmbed API (or a fallback background process) to instantly populate the title and thumbnail while a countdown is displayed. Progress bars are color-coded: colorless (queued), light blue (downloading), teal (animating indeterminately during post-processing), green (completed). Progress details (percentage, downloaded/total size, speed, ETA) are painted centered directly on the progress bar using the custom `ProgressLabelBar` widget.
9.  **Post-Processing:** Upon success, `DownloadManager` performs post-processing (e.g., embedding track numbers for audio playlists). Section clips in MP4-family containers now also run through an asynchronous ffprobe+ffmpeg normalization pass that first reads the clipped container duration and then rewrites with `-t <clip_duration>` plus `-fix_sub_duration`/`-c:s mov_text`, regenerating embedded subtitle stream durations against the clipped A/V timeline before the file is moved to the final output directory. Queue state persistence is deferred via `Qt::QueuedConnection`.
10. **Stopped Download Cleanup:** While a download is running, `DownloadManager` records every observed temporary file path reported by worker progress updates. If the user later clicks `Clear Temp Files` on a stopped or failed item, `DownloadQueueManager` uses that tracked path list plus literal stem matching to remove associated `.part`, `.aria2`, `.ytdl`, `.info.json`, fragment, thumbnail, subtitle, and partial media files without relying on fragile wildcard globs.

## 3. Directory Structure

```
LzyDownloader/
├── CMakeLists.txt              # Build System Configuration
├── main.cpp                    # Application Entry Point
├── src/
│   ├── core/                   # Core Business Logic
│   │   ├── ConfigManager.h/cpp   # Settings persistence (INI)
│   │   ├── ArchiveManager.h/cpp  # Download history (SQLite)
│   │   ├── DownloadQueueState.h/cpp # Manages persistence of download queue state
│   │   ├── DownloadManager.h/cpp # Queue & Lifecycle Management
│   │   ├── LocalApiServer.h/cpp  # localhost API bridge for local integrations
│   │   ├── YtDlpWorker.h/cpp     # yt-dlp process startup wrapper
│   │   ├── YtDlpWorkerProcess.cpp # completion, errors, output buffering, info.json parsing
│   │   ├── YtDlpWorkerOutput.cpp  # output-line orchestration and wait-state metadata
│   │   ├── YtDlpWorkerProgress.cpp # yt-dlp/aria2 progress parsing
│   │   ├── YtDlpWorkerTransfers.cpp # transfer target and stream-stage inference
│   │   ├── PlaylistExpander.h/cpp # Playlist Expansion (uses YtDlpArgsBuilder)
│   │   ├── YtDlpArgsBuilder.h/cpp # yt-dlp CLI argument construction
│   │   ├── ProcessUtils.h/cpp    # Runtime binary resolution + process env helpers
│   │   ├── GalleryDlWorker.h/cpp # gallery-dl Process Wrapper
│   │   ├── download_pipeline/
│   │   │   ├── Aria2RpcClient.h/cpp # aria2 RPC client / daemon controller
│   │   │   ├── Aria2DownloadWorker.h/cpp # aria2-backed media pipeline worker
│   │   │   ├── YtDlpDownloadInfoExtractor.h/cpp # yt-dlp --dump-json async extractor for aria2/runtime selection
│   │   │   └── FfmpegMuxer.h/cpp # ffmpeg muxing wrapper for multi-part downloads
│   │   ├── SortingManager.h/cpp  # File Sorting Logic
│   │   ├── AppUpdater.h/cpp      # Application Update Logic
│   │   └── ...
│   ├── ui/                     # User Interface (Qt Widgets)
│   │   ├── advanced_settings/
│   │   │   ├── MetadataPage.h/cpp # Metadata, thumbnails, and formatting settings
│   │   │   ├── BinariesPage.h/cpp # External binary status + install actions
│   │   ├── MainWindowUiBuilder.h/cpp # Builds UI for MainWindow
│   │   ├── StartTabUiBuilder.h/cpp # Builds UI for StartTab
│   │   ├── FormatSelectionDialog.h/cpp # Runtime format picker
│   │   ├── RuntimeSelectionDialog.h/cpp # Runtime subtitle picker
│   │   ├── DownloadItemWidget.h/cpp # Individual download item widget (displays thumbnail on left side of progress bar)
│   │   ├── SupportedSitesDialog.h/cpp # Searchable dialog listing supported domains
│   │   └── ...
│   └── utils/                  # Helper Modules
│       ├── BinaryFinder.h/cpp    # Locates external binaries
│       ├── StringUtils.h/cpp   # String/URL utilities
│       ├── ExtractorJsonParser.h/cpp # Extractor-domain cache loader
│       ├── LogManager.h/cpp    # Structured logging, one file per run with timestamp
│       └── ...
└── resources/                  # qrc resources (icons, extractor seed data, etc.)
```

## 4. Key Modules

### 4.1 ConfigManager (`src/core/ConfigManager.h`)
- **Responsibilities:**
  - Loads and saves application settings to `settings.ini` using `QSettings`.
  - Provides default configuration values using an internal `m_defaultSettings` map.
  - Uses the application's Qt-native INI schema. GUI and server/headless launches share the same app-local `settings.ini` so user preferences have one source of truth. Obsolete `Server/settings.ini` files are not used; if the main settings file is missing, one may be copied back once as a migration source.
  - Emits `settingChanged` signal when a setting is modified.
  - Automatically prunes legacy and dead keys from `settings.ini` on startup, ensuring the configuration file remains clean and canonical.
  - Clamps persisted `General/max_threads` back to `4` during startup so resumed sessions do not reopen with an overly aggressive concurrency level.

### 4.2 ArchiveManager (`src/core/ArchiveManager.h`)
- **Responsibilities:**
  - Manages the `download_archive.db` SQLite database using `QtSql`.
  - Provides methods to check for existing downloads (`isInArchive`) and add new ones (`addToArchive`).
  - **Implements URL normalization logic identical to the Python version.**

### 4.3 DownloadManager (`src/core/DownloadManager.h`)
- **Responsibilities:**
  - Manages the download queue and enforces concurrency limits (`max_threads`).
  - **Provides immediate UI feedback by emitting download items before playlist expansion completes.** Items are added to `m_downloadQueue` immediately and tracked in `m_pendingExpansions` map.
  - Intercepts runtime video/audio format-selection settings, fetches format metadata asynchronously, and re-enqueues one download per chosen format ID.
  - Handles the file lifecycle (Temp -> Final).
  - Uses `yt-dlp --print after_move:filepath` as the authoritative final output path source and moves files using Qt-native rename/copy fallback for Unicode-safe, cross-volume behavior.
  - Coordinates `YtDlpWorker` and `GalleryDlWorker` instances.
  - Tracks per-download cleanup candidates from worker progress (`current_file`, thumbnail paths, and related temp outputs) so stopped items can later clear all known temporary files.
  - Delegates queue state saving/loading to `DownloadQueueState`.
  - Handles playlist expansion via `PlaylistExpander`.
  - Preserves playlist context (`is_playlist`, `playlist_title`, playlist index, and original playlist URL) across single-item playlist handling, full playlist expansion, resume, sorting, and finalization.
  - Preflights YouTube SponsorBlock segment availability before starting video/livestream jobs so no-segment videos can skip expensive accurate-cut encoder arguments while unavailable preflights still fall back to the safer cut path.
  - Reinforces audio playlist album metadata before sorting and embedding when "Force Playlist as Single Album" is enabled, keeping album and album artist tags stable even when extractor metadata omits playlist context.
  - Performs post-processing (renaming, metadata embedding).
  - **Defers queue state persistence (`saveQueueState`) and download initiation (`startNextDownload`) via `QMetaObject::invokeMethod` with `Qt::QueuedConnection` to prevent GUI thread blocking.** Queue-finished detection also guards against false positives by waiting for pending playlist expansions and actively paused items to drain before emitting `queueFinished`.

### 4.3b LocalApiServer (`src/core/LocalApiServer.h`)
- **Responsibilities:**
  - Provides an optional localhost-only HTTP API for trusted local integrations.
  - Generates and persists an API token in `api_token.txt` under the app-local data directory, or under the `Server/` subfolder when launched with `--server` or `--headless`.
  - Requires Bearer-token authentication for all endpoints.
  - Emits enqueue requests through `MainWindow`, which applies non-interactive download defaults.
  - Accepts an optional enqueue `type` value and passes it through to the normal queue path, defaulting to video when omitted.
  - Tracks queue additions, progress, completion, and removals to serve `GET /status`.

### 4.3c MissingBinariesDialog (`src/ui/MissingBinariesDialog.h`)
- **Responsibilities:**
  - Presents a guided checklist whenever required external tools are missing during startup, enqueue, or Start-tab format checks.
  - Reuses `BinariesPage` install and browse actions so the setup flow stays consistent with Advanced Settings.
  - Refreshes `ProcessUtils` binary resolution in place and lets the original interactive action continue only after required tools are available.

### 4.4 YtDlpWorker (`src/core/YtDlpWorker.h`)
- **Responsibilities:**
  - Executes `yt-dlp` commands using `QProcess`.
  - Keeps startup and binary-resolution logic in `YtDlpWorker.cpp`.
  - Splits process completion and buffered metadata reading into `YtDlpWorkerProcess.cpp`.
  - Splits output-line orchestration and wait-state title/thumbnail fetching into `YtDlpWorkerOutput.cpp`.
  - Splits native yt-dlp and aria2 progress parsing into `YtDlpWorkerProgress.cpp`.
  - Splits transfer target classification and stream-stage inference into `YtDlpWorkerTransfers.cpp`.

### 4.4b DownloadQueueState (`src/core/DownloadQueueState.h`)
- **Responsibilities:**
  - Manages the persistence of the download queue, active, and paused items.
  - Serializes state including `tempFilePath`, `originalDownloadedFilePath`, and cleanup-related per-item options so features like cross-session resuming and manual temp file cleanup continue to work after an app restart.
  - Saves the current state to a JSON backup file (`downloads_backup.json`) in the application's configuration directory, using the `Server/` subfolder for server/headless runtime state.
  - Loads the state from the backup file on startup.
  - Emits `resumeDownloadsRequested` to `DownloadManager` to prompt the user about resuming previous downloads.
  - Resolves `yt-dlp` through configured overrides, system discovery, or bundled fallback paths.
  - Forces UTF-8 process I/O environment (`PYTHONUTF8`, `PYTHONIOENCODING`) to preserve Unicode output text.
  - Emits explicit failure results when `QProcess` cannot start, preventing stuck in-progress UI states.
  - **Parses native yt-dlp progress from `stderr`** (`[download] XX.X% of YY.YMiB at ZZ.ZMiB/s ETA 0:00`) and aria2c progress (`[#XXXXX ...]`) for progress percentage, speed, ETA, and metadata. **Progress lines are matched directly against the `[download]` prefix (no `download:` URL scheme stripping), fragment/native variations are accepted, and `[download] Destination:` tracking prevents thumbnail/subtitle/info-json transfers from replacing the main media progress numbers.**
  - Captures `stderr` for error reporting and progress. Captures `stdout` for the final file path (`--print after_move:filepath`).
  - **Caches full metadata from `info.json`** in `m_fullMetadata` when `readInfoJsonWithRetry()` successfully parses the file, ensuring all fields (uploader, channel, tags, etc.) are available for sorting rule evaluation when the download completes.
  - Includes diagnostic logging for process state changes, stderr/stdout data received, and progress parsing.
  - Reads `info.json` via a retry mechanism (up to 5 attempts with 500ms delays) to extract video title and metadata.

### 4.5 YtDlpUpdater (`src/core/YtDlpUpdater.h`)
- **Responsibilities:**
  - Checks GitHub for the latest `yt-dlp` nightly build.
  - Downloads and replaces the `yt-dlp.exe` binary.
  - Fetches and emits the current `yt-dlp` version.

### 4.5b PlaylistExpander (`src/core/PlaylistExpander.h`)
- **Responsibilities:**
  - Expands playlist URLs into individual video entries using `yt-dlp --flat-playlist --dump-single-json`.
  - **Uses `YtDlpArgsBuilder` to construct the full yt-dlp command**, ensuring playlist expansion includes the same configuration as actual downloads: `--js-runtimes deno:...`, `--cookies-from-browser`, `--ffmpeg-location`, `--windows-filenames`, etc.
  - Removes download-specific args (format selection, output template, embedding options) and adds `--no-download` to avoid fetching actual media during expansion.
  - Emits `playlistDetected` signal when a multi-item playlist is found and the user's playlist logic is set to "Ask".

### 4.6 SortingManager (`src/core/SortingManager.h`)
- **Responsibilities:**
  - Evaluates user-defined sorting rules against video metadata.
  - Replaces dynamic tokens (e.g., `{uploader}`) in destination paths.
  - Accepts both stored internal rule keys (`video_playlist`, `audio_playlist`, `any`, etc.) and older human-readable labels, while resolving aliases such as playlist title vs. album and uploader vs. channel.

### 4.7 LogManager (`src/utils/LogManager.h`)
- **Responsibilities:**
  - Installs a custom Qt message handler to capture debug output.
  - Writes logs to `%LOCALAPPDATA%\LzyDownloader\LzyDownloader_YYYY-MM-dd_HH-mm-ss.log` (or equivalent platform-specific config directory). Server/headless logs are written under the `Server/` subfolder while preferences remain in the shared main `settings.ini`.
  - **One log file per run:** Each application startup creates a new log file with a timestamp in the filename.
  - Implements **automatic cleanup on startup:** keeps only the 5 most recent log files, deleting older ones to prevent unbounded disk growth.
  - Designed for **NSIS release deployment**: logs are stored in user data directories, not the installation directory, ensuring they persist across application updates.

### 4.8 YtDlpArgsBuilder (`src/core/YtDlpArgsBuilder.h`)
- **Responsibilities:**
  - Constructs the full `yt-dlp` command-line arguments from `ConfigManager` settings and per-download options.
  - Handles format selection (codec mapping, quality constraints, direct runtime `format` overrides, and separate runtime video/audio format merges), output templates, subtitle configuration, metadata/thumbnail embedding, JS runtime (`deno`), cookie extraction, and rate limiting.
  - Normalizes legacy codec labels from saved settings (for example `H.264` -> `H.264 (AVC)`) and translates codec preferences into yt-dlp regex selectors that match common aliases such as `avc1`, `hev1`, `hvc1`, and `mp4a`.
  - Respects the Advanced Settings `restrict_filenames` toggle instead of hardcoding `--no-restrict-filenames`.
  - Injects `--parse-metadata` arguments to unify album/album_artist tags for audio playlists when the "Force Playlist as Single Album" setting is enabled.
  - Preserves the requested output container for `--download-sections` jobs instead of forcing an intermediate MKV remux; section video jobs only add `--force-keyframes-at-cuts` for cleaner clip boundaries.
  - Adds optional `ModifyChapters+ffmpeg_o` postprocessor arguments when a hardware/custom FFmpeg cut encoder is configured and SponsorBlock segments are confirmed or could not be preflighted, speeding up yt-dlp's accurate re-encode path without paying that cost for videos with no removable segments.
  - Injects the internal download ID as `%(lzy_id)s` and scopes yt-dlp output to a per-download temporary subfolder so concurrent jobs with identical site filenames do not corrupt each other.
  - Injects a filename-safe section suffix into the output template when `download_sections_label` is present so clipped files identify the chosen time range or chapter in their saved filename.

## 5. Concurrency Model
- **GUI Thread:** The main thread handles all UI updates and user interactions. **No blocking operations are allowed on this thread.**
- **Worker Threads:** `QProcess` runs external binaries asynchronously. Qt signals and slots are used for communication.

## 6. Deployment
- **Build System:** CMake.
- **Qt Configure Resilience:** The build should succeed in IDE-driven configure runs by auto-detecting common Windows Qt SDK locations before `find_package(Qt6 ...)` executes.
- **Installer:** NSIS will be used to create a Windows installer (`LzyDownloader-Setup.exe`).
- **Release Helper Script:** `build_release.ps1` refreshes extractor JSONs, performs a clean Release configure/build, and runs `makensis` so local release packaging follows the same repeatable steps every time.
- **Executable Name:** The final executable will be named `LzyDownloader.exe` to ensure a seamless update from the Python version.
- **Bundling:** All dependencies (Qt runtime DLLs, binaries) will be included in the installation directory.
- **Release Output Hygiene:** After `windeployqt`, `CMakeLists.txt` re-copies the resolved Qt runtime DLLs from the configured Qt installation. Keep the deployed compression/runtime DLLs that ship with Qt, including `zlib1.dll`, because `Qt6Network.dll` depends on them on Windows.
- **OpenSSL Runtime Deployment:** Windows builds must copy `libcrypto-3-x64.dll` and `libssl-3-x64.dll` next to `LzyDownloader.exe` so Qt's TLS backend can initialize HTTPS for update checks and other network requests.
- **Qt Image Format Plugins:** Windows deployments must include the required `plugins/imageformats` codecs for active-download artwork and converted thumbnails, including `qjpeg`, `qpng`, `qwebp`, and `qico` (plus debug variants when available).
- **User Data Preservation:** The NSIS installer MUST NOT overwrite user data files (`settings.ini`, `download_archive.db`, `downloads_backup.json`, or timestamped log files). These are stored in platform-specific user data directories (for example `%LOCALAPPDATA%\LzyDownloader\` on Windows), separate from the installation directory, ensuring they persist across application updates.
