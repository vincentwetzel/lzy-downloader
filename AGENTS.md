# AGENTS.md - AI Contributor Guide for LzyDownloader (C++ Port)

This document is the **canonical instruction set for all AI agents** working on the C++ port of the LzyDownloader project. It defines the project's purpose, architecture, constraints, and rules for safe and effective contributions.

All agents MUST follow this document as the source of truth.

**Fast Start:** Keep the UI responsive (no blocking I/O on the GUI thread; use `QThread` or `QtConcurrent`). Preserve the download lifecycle (temp → verify → move to completed dir). Prefer discovered or user-configured external binaries. Update `CHANGELOG.md` for significant changes. For releases, sync `CMakeLists.txt`, `vcpkg.json`, `LzyDownloader.nsi` examples/metadata, and `CHANGELOG.md` before building. Update Section 3 + Quick-Reference list when files/roles change. For file locations, jump to the Quick-Reference list in Section 3.

---

## 1. Project Overview

**LzyDownloader (C++)** is a desktop application built with **Qt 6 (Widgets)** that allows users to download media (primarily video and audio) from online platforms using **yt-dlp** and **gallery-dl**.

**This project is a C++ port of the original Python application.** The primary goal is to create a **drop-in replacement** that is faster, more efficient, and maintains full compatibility with the user's existing settings and download history.

Key goals:
- Provide a **stable, user-friendly GUI** using native C++ and Qt.
- Support **concurrent downloads**, playlists, and advanced configuration.
- Be **self-contained** when compiled (no external dependency installs).
- Prioritize **UI responsiveness**, robustness, and clear error handling.
- **Clean Break from Python**: Backwards compatibility with the Python version's `settings.ini` is NO LONGER REQUIRED. It is expected and acceptable that the C++ app uses a pure Qt-native configuration format, even if it requires users to regenerate their settings.

The target platform is primarily **Windows**, but path handling and logic should remain cross-platform where possible.

---

## 2. Core Functionality (What Must Not Break)

Agents MUST preserve and respect the following behaviors from the original Python application:

- Media downloading via `yt-dlp` (for videos/audio) and `gallery-dl` (for image galleries).
- Concurrent download management with user-defined limits (capped at a maximum of 4 concurrent threads upon application startup, though users can manually increase it up to 8 during a session).
- Playlist expansion and processing (for `yt-dlp` downloads).
- **Configuration**: The app uses a Qt-native `QSettings` INI implementation. It does not need to conform to Python `configparser` quirks.
- **Archive Portability**: The C++ app MUST use the same `download_archive.db` (SQLite) to respect the user's download history.
- File lifecycle: download into temp dir → verify file stability → move to completed downloads directory.
- Metadata embedding (title, artist, etc.) and thumbnail embedding.
- Responsive GUI at all times (no blocking I/O on the main thread).
- In-app updating of the `yt-dlp` and `gallery-dl` executables.
- The final executable name MUST be `LzyDownloader.exe` to ensure the update process from the Python version works seamlessly.
- **Download Progress Display**: The UI progress bar MUST update correctly for **both** yt-dlp's native downloader **and** aria2c as an external downloader. The worker must parse and emit progress from:
  - Native yt-dlp progress lines (`[download] XX.X% of ...`) including intercepting `(frag X/Y)` tags for HLS fragmented streams to override erratic progress outputs.
  - aria2c progress lines (`[#XXXXX N.NMiB/N.NMiB(XX.X%)...]`)
  - The `--progress-template` output format
  - Livestream indeterminate progress (`[download] XX.XMiB at YY.YMiB/s (HH:MM:SS)`)
- **Progress Bar Color Coding**: Download progress bars MUST use color-coding to provide visual feedback on download state:
  - **Colorless/Default** (no custom stylesheet): When download is queued, initializing, or in indeterminate state (progress < 0)
  - **Light Blue** (`#3b82f6`): While actively downloading (0% < progress < 100%)
  - **Teal** (`#008080`): During post-processing phase (indeterminate scrolling animation with status containing "Processing", "Merging", "Finalizing", etc.)
  - **Green** (`#22c55e`): When download is fully completed (progress at 100% and post-processing finished)
  - The percentage, file sizes, speed, and ETA MUST be painted centered on the progress bar using the `ProgressLabelBar` custom widget.
- **Detailed Progress Display**: The UI MUST display rich, detailed progress information to users during downloads, comparable to command-line yt-dlp output:
  - **Status Label**: Shows current download stage (e.g., "Extracting media information...", "Downloading 2 segment(s)...", "Merging segments with ffmpeg...", "Verifying download completeness...", "Applying sorting rules...", "Moving to final destination...", "Next check in 05:00")
  - **Centered Progress Text**: Painted directly on the progress bar, includes percentage, downloaded/total size, speed, and ETA (e.g., "45%  15.3 MiB/45.7 MiB  2.4 MiB/s  ETA 0:12")
  - All progress data (speed, ETA, sizes) MUST be emitted by workers and parsed from both native yt-dlp and aria2c output
- **Immediate Queue UI Feedback**: Downloads MUST appear in the Active Downloads tab immediately when queued, without waiting for playlist expansion or validation:
  - Gallery downloads appear instantly with "Queued" status
  - Video/audio downloads appear instantly with "Checking for playlist..." status during playlist expansion
  - Single videos update to "Queued" once expansion completes
  - Playlists remove the placeholder and add individual items for each track
  - Queue state persistence (`saveQueueState`) MUST be deferred via `Qt::QueuedConnection` to avoid blocking the GUI thread
- **Headless State Persistence**: The application MUST guarantee that final terminal states (such as a fully cleared queue upon completion) are successfully serialized to `downloads_backup.json` before `QCoreApplication::quit()` is called, especially during `--server --exit-after` execution flows that bypass `closeEvent()`.

---

## 3. Architecture Overview (C++ Port)

**POLICY:** Agents MUST update this section and the "Quick-Reference" list below whenever new files are added or responsibilities shift.

The project follows a **modular, separation-of-concerns design** using C++ and Qt.

### Entry Point
- `main.cpp` - Initializes the `QApplication`, creates and shows the `MainWindow`, and enforces single application instance using `QSystemSemaphore` and `QSharedMemory` (branches the lock with a `_Server` suffix for headless runs). Installs a custom message handler for logging.
- `CMakeLists.txt` - Project definition, **version source of truth** (`project(VERSION x.y.z)`), dependencies (Qt6), and build instructions. Version is auto-generated into `version.h` via `configure_file`. **Supports both standalone Qt installs and vcpkg-toolchain builds, copies Qt runtime plugins from vcpkg when a manifest/toolchain build is active, deploys OpenSSL runtime DLLs needed by Qt HTTPS, and adds MSVC `/FS` so parallel debug builds do not collide on the shared PDB.**
- `vcpkg.json` - vcpkg manifest for source builds. Declares the baseline package dependencies (currently Qt base with SQLite support) used by the CMake presets and release workflow. **Release rule:** keep `version-string` synchronized with `CMakeLists.txt`.
- `ports/pcre2/` - vcpkg overlay port for the transitive Qt PCRE2 dependency. Mirrors the upstream vcpkg `pcre2` port and marks `PCRE2_STATIC_RUNTIME` as intentionally maybe-unused so newer PCRE2 CMake configures stay warning-free.
- `LzyDownloader.rc` - Windows resource file for embedding the application icon and **version info** (file/product version from `version.h`) into the executable.
- `LzyDownloader.nsi` / `build_release.ps1` - Windows installer packaging. **Release rule:** the installer version must come from `CMakeLists.txt` via the release script or `makensis /DAPP_VERSION=...`; keep any NSIS examples/metadata free of stale version numbers, and do not "fix" a release by only renaming the setup `.exe`.
- `src/core/version.h.in` - CMake template that generates `version.h` with `APP_VERSION_MAJOR`, `APP_VERSION_MINOR`, `APP_VERSION_PATCH`, `APP_VERSION_STRING`, and `APP_VERSION_RC` macros.
- **Auto version bump**: `.git/hooks/pre-push` automatically increments the patch version on every `git push`. Skip with `SKIP_VERSION_BUMP=1 git push`.

### UI Layer (`src/ui/`)
- `MainWindow.h/.cpp`, `MainWindowConnections.cpp`, `MainWindowDiscord.cpp`, `MainWindowDownloads.cpp`, `MainWindowUi.cpp`, and `MainWindowHelpers.h` - Application shell and signal orchestrator split by responsibility. `MainWindow.cpp` owns construction/destruction, `MainWindowConnections.cpp` owns server/updater/startup/download signal wiring, `MainWindowDiscord.cpp` owns Discord webhook sync, `MainWindowDownloads.cpp` owns enqueue/runtime-selection/error-popup slots, `MainWindowUi.cpp` owns UI/tray/theme/clipboard/exit behavior, and `MainWindowHelpers.h` owns shared CLI/non-interactive helpers. Now includes labels for displaying download statistics (queued, active, completed). **Handles initial setup prompt for download directories if not configured, ensuring both completed and temporary directories are set at launch.** **Connects to `AdvancedSettingsTab::setYtDlpVersion` to display the current `yt-dlp` version.** **Now includes a `QClipboard` listener and an updated `handleClipboardAutoPaste` function to support multiple auto-paste modes with a short 500 ms debounce plus duplicate queue checks.** **Handles runtime subtitle selection and displays the runtime format-selection dialog requested by `DownloadManager`.** **Owns the playlist-choice prompt for `playlist_logic=Ask`, forwarding the user's decision back to `DownloadManager` so playlist placeholders are either updated in place for single videos or removed before individual playlist entries are added to the Active Downloads tab.** **When the section-download dialog is accepted, it now stores both the raw `--download-sections` value and a human-readable section label so clipped downloads can include the selected range/chapter in the output filename.** **yt-dlp error popups now render rich text and include a clickable source URL when one is available.** **Before enqueueing, it verifies the required binaries for the selected download type, opens `MissingBinariesDialog` when required tools are absent, auto-falls back to the native downloader if `aria2c` was enabled but is no longer installed, warns when a stable `yt-dlp` build is detected, and confirms exit when queued or active downloads will be resumed on the next launch.** **Direct CLI URL launches and Local API enqueues mark requests as non-interactive, forcing completed-archive override, playlist download-all behavior, runtime-prompt bypasses, and log-only handling for UI warnings.** **On Windows, it also owns the runtime debug-console visibility toggle backed by `General/show_debug_console`.** **Discord webhook payloads sanitize long/multi-line status text, preserve progress across partial queue refreshes, use explicit playlist placeholder IDs for parent mapping, include live `queue_position` values for queued jobs, and keep terminal completion/cancellation state available long enough for local bridge clients to observe it.**
- `StartTab.h/.cpp` - Input and configuration orchestrator. Delegates URL handling, download actions, and command preview updates to specialized helper classes. **Now also wires the Start tab's operational controls (playlist logic, max concurrent, rate limit, and override duplicate detection) to save instantly into `ConfigManager`, and launches `SupportedSitesDialog` from the dedicated toolbar button.**
- `StartTabUiBuilder.h/.cpp` - Builds the UI layout for `StartTab`, including URL input, download buttons, quick-access folder buttons, and the dedicated `Supported Sites` button.
- `start_tab/StartTabDownloadActions.h/.cpp` - Handles download button clicks, format checking, and download type changes.
- `start_tab/StartTabUrlHandler.h/.cpp` - Manages URL text input, clipboard monitoring, and auto-switching download types based on supported extractors.
- `start_tab/StartTabCommandPreviewUpdater.h/.cpp` - Updates the command preview text box when settings or download types change.
- `ActiveDownloadsTab.h/.cpp` - Monitoring; renders active/completed downloads, progress bars, etc. Ensures a thumbnail preview is played/displayed on the left side of each download GUI element. Includes toolbar buttons to quickly open temporary and completed download folders. **Also exposes a lightweight removal slot so playlist-expansion placeholders can disappear cleanly before per-entry playlist rows are added.** **The toolbar now uses compact icon actions, adds `Resume All` for stopped/failed rows, and replaces `Clear Completed` with a unified `Clear Inactive` action.** **Adding a row with an existing internal download ID first clears the previous row so refreshed placeholders/restored queue items cannot render duplicate widgets.** **For live downloads, it relays per-item `Finish Now` requests from `DownloadItemWidget` to `DownloadManager` so yt-dlp can be interrupted gracefully and finalized.**
- `AdvancedSettingsTab.h/.cpp` - Global settings shell. Uses a compact `QListWidget` menu plus `QStackedWidget`, with broad noob-friendly sections: Essentials (folders/theme/API/auth cookies), Formats (video/audio/livestream defaults), Download Flow (downloader engine, clipboard/queue helpers, chapters/sections/SponsorBlock, filename/display options), Files & Tags (output templates, metadata/artwork, subtitles), and External Tools (binary management). Legacy navigation names such as "External Binaries" and "Download Options" are aliased to the new sections. Most settings auto-save on change. The "Output Templates" controls have dedicated Save buttons with validation. Subtitle language selection uses a friendly picker. Provides immediate feedback if a selected browser's cookie database is locked, preventing misconfiguration. The cookie access check has a 30-second timeout. **The "Auto-paste URL when app is focused" toggle has been replaced with a `QComboBox` offering multiple auto-paste modes (disabled, on focus, on new URL, on focus & enqueue, on new URL & enqueue). All auto-paste modes include duplicate URL prevention with a short clipboard debounce and queue checking.** **Download Flow exposes FFmpeg cut encoder settings so accurate SponsorBlock cuts can use NVENC, Quick Sync, AMF, VideoToolbox, or custom FFmpeg output arguments; hardware choices are filtered asynchronously using FFmpeg encoder support plus local GPU detection.** **On Windows, the footer includes a `Show Debug Console` toggle when the app owns its console window.**
    - The left-side category list now derives its colors from the active `QPalette` and re-applies the stylesheet when a palette change occurs so it stays compact and theme-accurate.
- `advanced_settings/MetadataPage.h/.cpp` - Advanced Settings page for metadata, thumbnail embedding, cropping, and format conversion.
- `advanced_settings/BinariesPage.h/.cpp` - Advanced Settings page for external dependency management. **Shows per-binary status, live version strings, and brief inline descriptions for discovered/configured executables, keeps manual "Browse" overrides (use "Clear Path" to reset to auto-detection), and offers package-manager/manual-download "Install" actions for `yt-dlp`, `ffmpeg`, `ffprobe`, `gallery-dl`, `aria2c`, and `deno`.** **yt-dlp and gallery-dl now also expose inline `Update` buttons from this page, while package-managed installs are redirected back to the system package manager instead of being overwritten in place.** **Successful installs refresh detection in the running app and can save a direct custom path (for example `curl`-downloaded standalones); users are only asked to restart if a package-manager install remains invisible to the current process.** **yt-dlp install suggestions now prefer nightly-capable commands where available (for example Scoop `yt-dlp-nightly`, Homebrew `--HEAD`, or direct nightly downloads) and clearly label when a package manager only provides stable builds.**
- `FormatSelectionDialog.h/.cpp` - Runtime format picker; displays `yt-dlp --dump-json` format data in a table, allows multi-selection/custom IDs, and each selected format is enqueued as a separate download. Automatically filters out video formats when the 'Audio Only' download type is selected. **This dialog is the sole place where runtime video/audio format decisions are made once Advanced Settings quality is set to `Select at Runtime`.**
- `MissingBinariesDialog.h/.cpp` - Welcome-style setup dialog for missing required binaries. It shows a checklist of absent or invalid tools, reuses `BinariesPage` install/browse actions, refreshes detection without requiring users to hunt through Advanced Settings, and blocks the current interactive action until the required tools are resolved or the user closes the dialog.
- `RuntimeSelectionDialog.h/.cpp` - Runtime subtitle picker; currently used for subtitle-at-runtime selection sourced from extractor metadata.
- `DownloadSectionsDialog.h/.cpp` - Runtime section picker; builds one or more chapter/time-range clip definitions for `yt-dlp --download-sections` and also produces a filename-safe section label for clipped outputs. **Includes helper text instructing users how to turn off the section prompt in Advanced Settings.**
- `SortingTab.h/.cpp` - UI for managing file sorting rules. **Now uses a `QTableWidget` to display rules in a grid with columns for Priority, Type, Condition, Target Path, and Subfolder.**
- `SortingRuleDialog.h/.cpp` - Dialog for creating and editing sorting rules. **Uses QScrollArea with QVBoxLayout instead of QListWidget for smooth pixel-level scrolling (no item-snapping). Conditions are added directly to a vertical layout inside the scroll area. The dialog has a minimum size of 650x500. Multi-line text inputs use a fixed height of 100px controlled by the `CONDITION_VALUE_INPUT_HEIGHT` constant for consistent sizing. Also includes "Greater Than" and "Less Than" operators, dynamically enabled/disabled based on the selected field. "Is One Of" condition values are now sorted alphabetically when the dialog is accepted.**
- `SupportedSitesDialog.h/.cpp` - Searchable UI dialog that combines and displays the domains from `extractors_yt-dlp.json` and `extractors_gallery-dl.json` to inform users what media types are supported for specific domains.
- `resources.qrc` - Qt Resource file for embedding assets like images.

### Core Logic (`src/core/`)
- `ConfigManager.h/.cpp` - State persistence; reads/writes the shared app-local `settings.ini` using `QSettings` for both GUI and server/headless mode. Automatically sets `temporary_downloads_directory` when `completed_downloads_directory` is updated. Emits `settingChanged` signal when any setting is modified. Uses an internal map (`m_defaultSettings`) to manage default values. Ensures `output_template` is always a filename template. Automatically prunes dead/legacy keys from the configuration file on startup. **The canonical default video codec label is now `H.264 (AVC)`.** **On startup it now clamps persisted `General/max_threads` back to `4`, while still allowing users to raise concurrency during the current session.** Obsolete `Server/settings.ini` is not used, except as a one-time migration source when the main settings file is missing.
- `ArchiveManager.h/.cpp` - History persistence; reads/writes `download_archive.db` using `QtSql`. **Must be compatible with Python's schema.**
- `DownloadManager.h/.cpp` - "Brain"; owns construction, shutdown, config reactions, and the public download-management API. The implementation is split across focused `DownloadManager*.cpp` translation units to keep each file below the 500-line context limit while preserving one `DownloadManager` QObject.
- `DownloadManagerEnqueue.cpp` - Enqueue gates for duplicate/archive override, section metadata lookup, runtime format lookup, gallery immediate queueing, and video/audio playlist-expansion placeholder creation.
- `DownloadManagerPlaylist.cpp` - Playlist detection, `playlist_logic=Ask` selection handling, placeholder replacement/removal, and per-entry enqueueing.
- `DownloadManagerControls.cpp` - Stop, pause, resume, retry, restart-with-options, livestream finish-now, and queue reordering logic, including active worker/process-tree cleanup.
- `DownloadManagerExecution.cpp` - Queue scheduling, sleep-mode concurrency, worker startup, and SponsorBlock segment preflight before starting yt-dlp jobs.
- `DownloadManagerWorkers.cpp` - Worker progress bookkeeping, temp cleanup candidate tracking, yt-dlp/gallery completion, metadata embedding handoff, finalizer completion, cleared-row queue cleanup, aggregate speed, and queue-finished signaling.
- `LocalApiServer.h/.cpp` - "Bridge"; Optional localhost-only `QTcpServer` integration endpoint on port `8765`. GUI mode starts it from `General/enable_local_api`; `--server`/`--headless` starts it automatically without mutating that GUI preference. Generates/loads `api_token.txt` (under `Server/` for server/headless runtime isolation), requires Bearer-token auth, accepts `POST /enqueue` with `url` plus optional download `type` (`video`, `audio`, or `gallery`), exposes `GET /status`, and receives download manager signals to keep status snapshots current.
- `SortingManager.h/.cpp` - "Helper"; Applies sorting rules to determine the final download directory. **Now normalizes field/token lookups and uses alias-aware metadata resolution (for example album ↔ playlist title, uploader/channel-style fields, and case/punctuation differences) so sorting remains consistent across fresh, playlist, audio, and resumed downloads.** **Also accepts both legacy human-readable rule scopes and the newer internal keys such as `video_playlist`, `audio_playlist`, and `any`.**
- `PlaylistExpander.h/.cpp` - "Expander"; Uses `yt-dlp --flat-playlist --dump-single-json` to expand playlist URLs into individual video entries. **Now uses `YtDlpArgsBuilder` to construct the full command (including `--js-runtimes deno:...`, `--cookies-from-browser`, `--ffmpeg-location`, etc.) so playlist expansion matches the actual download configuration, and extracts playlist titles from multiple yt-dlp fields (`playlist_title`, `playlist`, `album`, `title`) so audio playlist children retain album context.**
- `DownloadQueueManager.h/.cpp` - "Queue Master"; Manages the download queue, paused items, and pending playlist expansions. Handles saving/loading queue state and duplicate detection across all queue states. **Owns the placeholder-removal path used during playlist expansion so "Checking for playlist..." rows can be removed without being converted into stopped downloads.**
- `DownloadFinalizer.h/.cpp` - "Mover"; Handles file stability verification, applying sorting rules, and moving/copying files from the temporary directory to their final destinations. Emits events back to the manager upon success or failure. **Playlist index prefixing is configurable via `DownloadOptions/prefix_playlist_indices`, defaults on for legacy audio behavior, avoids double-numbering, and removes per-download UUID temp folders after successful yt-dlp finalization.**
- `DownloadItem.h` - "Data Model"; Lightweight struct representing a single download item's state, options, and metadata.
- `download_pipeline/Aria2RpcClient.h/.cpp` - "RPC Client"; Owns the background `aria2c` RPC daemon connection used for concurrent, segmented downloads.
- `Aria2DownloadWorker.h/.cpp` - "Worker"; Orchestrates the pipeline (Extract -> Download -> Post-process) for media downloads.
- `download_pipeline/YtDlpDownloadInfoExtractor.h/.cpp` - "Extractor"; Runs `yt-dlp --dump-json` asynchronously to fetch download metadata and aria2 target URLs without blocking the UI.
- `download_pipeline/FfmpegMuxer.h/.cpp` - "Muxer"; Asynchronously runs `ffmpeg` to merge audio, video, subtitles, and artwork safely.
- `MetadataEmbedder.h/.cpp` - "Post-processor"; Embeds track numbers and optional extra metadata for audio playlist items, embeds abandoned thumbnails (e.g., from livestream remuxes), and normalizes finished section clips through an ffprobe+ffmpeg pass for MP4-family containers before finalization. The app probes the clipped container duration with `ffprobe`, then rewrites with `-fflags +genpts -ignore_editlist 1 -fix_sub_duration -c:s mov_text -t <clip_duration> -shortest -movflags +faststart` so embedded subtitle streams are forced onto the clipped timeline.
- `GalleryDlWorker.h/.cpp` - "Muscle"; Wraps `QProcess` to run `gallery-dl`, parses stdout for progress, and emits signals. **Resolves `gallery-dl` through `ProcessUtils`, supporting user-configured/system binaries.**
- `GalleryDlArgsBuilder.h/.cpp` - "Helper"; Constructs command-line arguments for `gallery-dl` based on settings from `ConfigManager`. **Passes the full template directly to `-f` since gallery-dl natively supports path templates with `/` separators.**
- `YtDlpArgsBuilder.h/.cpp` - "Helper"; Constructs command-line arguments for `yt-dlp` based on settings from `ConfigManager`. **Now relies solely on `ConfigManager` for the `temporary_downloads_directory` and `output_template`.** **All app-built yt-dlp commands include `--ignore-config` so user-level yt-dlp config files cannot override GUI/headless downloads.** **Download sections and SponsorBlock preserve the user's requested output container; section downloads always inject `--force-keyframes-at-cuts`, while SponsorBlock video/livestream downloads only inject that flag plus `ModifyChapters` FFmpeg args when `DownloadManager` confirms segments or the preflight is unavailable. Built-in cut modes include edit-list and timestamp normalization (`-ignore_editlist 1`, `-avoid_negative_ts make_zero`, `-fflags +genpts`) to prevent MP4 A/V desync; custom mode only uses the user's custom output args.** **If a section label is provided, it appends a filename-safe suffix such as `[section 15-00_to_end]` before `%(ext)s` so clipped files identify which part of the source they represent.** **It now injects the app's internal download ID as `%(lzy_id)s` and scopes yt-dlp output to a UUID temp subfolder so concurrent jobs with identical filenames cannot corrupt each other.** **It now also normalizes legacy codec labels like `H.264`/`H.265`, translates video/audio codec preferences into yt-dlp selectors that match real stream aliases such as `avc1`, `hev1`, and `mp4a`, honors direct runtime `format` overrides from `FormatSelectionDialog`, merges separate runtime video/audio format IDs for video downloads, respects the `restrict_filenames` Advanced Setting when building yt-dlp arguments, and can inject app-known playlist titles via `--parse-metadata` to unify album/album_artist tags for audio playlists even when per-track yt-dlp metadata loses playlist context.**
- `YtDlpWorker.h/.cpp` - yt-dlp process wrapper entry/startup logic. **Resolves `yt-dlp` through `ProcessUtils`, supports user-configured/system binaries, starts the worker process with native progress enabled, and can send a graceful interrupt for live-download finish-now finalization.**
- `YtDlpWorkerProcess.cpp` - yt-dlp process completion, process errors, stdout/stderr buffering, and `info.json` retry parsing.
- `YtDlpWorkerOutput.cpp` - yt-dlp output-line orchestration, error classification, lifecycle/status line handling, wait-state metadata/thumbnail fetching, destination detection, and handoff to progress parsers.
- `YtDlpWorkerProgress.cpp` - native yt-dlp and aria2 progress parsing plus size formatting and aggregate primary-stream progress calculation.
- `YtDlpWorkerTransfers.cpp` - transfer target classification, primary stream inference, aria2 command-line hints, current status emission, and console-line normalization.
- `AppUpdater.h/.cpp` - Self-maintenance; checks GitHub Releases for application updates and now falls back across multiple repository API URLs so repo renames do not break update checks.
- `StartupWorker.h/.cpp` - Orchestrates initial startup checks, including `yt-dlp` and `gallery-dl` version fetching and extractor list generation.

### Utilities (`src/utils/`)
- `StringUtils.h/.cpp` - Helper functions for string manipulation, URL normalization, etc. **The `cleanDisplayTitle` function has been removed as the video title is now extracted directly from `yt-dlp`'s `info.json` output.**
- `LogManager.h/.cpp` - Installs a custom message handler for structured logging, including log rotation. Routes headless/server logs into the same `Server/` app-data subfolder used by headless API and queue runtime state.
- `BrowserUtils.h/.cpp` - Helper functions for browser-related tasks, such as finding installed browsers. The `checkCookieAccess` function has been removed.
- `ExtractorJsonParser.h/.cpp` - Loads the `extractors_yt-dlp.json` and `extractors_gallery-dl.json` databases from the app directory for clipboard URL auto-paste.

### Tests (`src/tests/`)
- `BaseTest.h/.cpp` - Shared Qt test fixture that provisions isolated temporary settings/archive paths so tests do not touch user data.
- `TestYtDlpArgsBuilder.h/.cpp`, `TestYtDlpWorker.cpp`, `TestArchiveManager.h/.cpp`, `TestSortingManager.h/.cpp`, `TestUIWidgets.h/.cpp`, and `TestEndToEnd.h/.cpp` - Qt test coverage for command construction, progress parsing, archive URL normalization, sorting token/sanitization behavior, progress widget state, and a local end-to-end yt-dlp download.
- `test_server.py` and `test_video.webm` - Local HTTP fixture assets used by the end-to-end test.

### Quick-Reference: Where is X?

- **Settings/Config**: `src/core/ConfigManager.h/.cpp` (handles shared app-local `settings.ini` I/O for GUI and server/headless mode, emits `settingChanged` signal, ensures `output_template` is a filename template, `temporary_downloads_directory` is correctly set, automatically prunes legacy keys on startup, uses `H.264 (AVC)` as the canonical default video codec label, and clamps persisted startup concurrency back to `4`. Does not route preferences to `Server/`; obsolete `Server/settings.ini` may only be copied back once if the main file is missing).
- **Build Dependencies / vcpkg Manifest**: `vcpkg.json` (declares manifest-mode source-build dependencies), `CMakePresets.json` (points Windows preset builds at the vcpkg toolchain/triplet and workspace overlay ports), `CMakeLists.txt` plus `cmake/deploy_openssl_runtime.cmake` (deploy Qt plugins and OpenSSL runtime DLLs for Windows HTTPS support), and `ports/pcre2/` (local vcpkg overlay for the transitive PCRE2 recipe warning fix).
- **Qt Tests**: `src/tests/BaseTest.h/.cpp` (isolated fixture), `src/tests/TestYtDlpArgsBuilder.h/.cpp`, `src/tests/TestYtDlpWorker.cpp`, `src/tests/TestArchiveManager.h/.cpp`, `src/tests/TestSortingManager.h/.cpp`, `src/tests/TestUIWidgets.h/.cpp`, and `src/tests/TestEndToEnd.h/.cpp`. End-to-end fixtures live in `src/tests/test_server.py` and `src/tests/test_video.webm`; register new test executables in `CMakeLists.txt`.
- **Download Archive**: `src/core/ArchiveManager.h/.cpp` (handles `download_archive.db` I/O).
- **URL Validation / Enqueue Gates**: `src/core/DownloadManager.h/.cpp` and `src/core/DownloadManagerEnqueue.cpp`.
- **Download Queue**: `src/core/DownloadQueueManager.h/.cpp`. **Manages the download queue, paused items, and pending playlist expansions. Handles manual temp file cleanup when stopped/failed items are cleared by using tracked cleanup candidate paths plus literal stem matching (including format-ID stripping) to remove partial media, fragments, metadata, thumbnails, subtitles, and downloader state files without wildcard bugs. Provides immediate UI feedback by emitting download items before playlist expansion completes. Single videos show "Checking for playlist..." status which updates to "Queued" once expansion finishes, while true playlists now remove that placeholder row and enqueue one GUI item per expanded entry. Gallery downloads appear instantly. Queue state persistence and next-download evaluations are deferred via `Qt::QueuedConnection` to avoid blocking the GUI thread during synchronous cascades (e.g. mass-stopping). Includes `getDuplicateStatus()` method that checks all states (queued, active, paused, completed) and emits `duplicateDownloadDetected` signal with user-friendly warning when duplicates are detected.**
- **Playlist choice prompt (`Ask`)**: `src/ui/MainWindowConnections.cpp` (dialog orchestration) and `src/core/DownloadManagerPlaylist.cpp` (applies the selection to placeholder replacement and per-entry enqueueing).
- **Playlist Expansion**: `src/core/PlaylistExpander.h/.cpp` (uses `YtDlpArgsBuilder` for full command construction including deno, cookies, ffmpeg path).
- **Process Execution (`aria2c`)**: `src/core/download_pipeline/Aria2RpcClient.h/.cpp` and `src/core/download_pipeline/Aria2DownloadWorker.h/.cpp`.
- **Process Execution (`yt-dlp`/`ffmpeg`)**: `src/core/download_pipeline/YtDlpDownloadInfoExtractor.h/.cpp` and `src/core/download_pipeline/FfmpegMuxer.h/.cpp` (asynchronous metadata and muxing). **Shared process-tree shutdown and graceful-interrupt logic lives in `src/core/ProcessUtils.h/.cpp`, and app exit is coordinated from `src/core/DownloadManager.h/.cpp`, `src/core/DownloadManagerControls.cpp`, and `src/ui/MainWindowUi.cpp` (which catch-kill remaining utility processes). Process termination uses detached OS commands to prevent GUI thread blocking during mass-cancellations.**
- **Process Execution (`gallery-dl`)**: `src/core/GalleryDlWorker.h/.cpp` (wraps `gallery-dl` `QProcess`, resolves executable via `ProcessUtils` supporting system PATH and custom paths).
- **Output Templates**: `src/ui/advanced_settings/OutputTemplatesPage.h/.cpp` (UI for yt-dlp and gallery-dl filename templates). **gallery-dl template dropdown includes comprehensive token list.**
- **gallery-dl Output Template Handling**: `src/core/GalleryDlArgsBuilder.h/.cpp` (splits templates containing `/` into `output.directory` and filename parts for proper gallery-dl directory structure).
- **Smart URL Download Type Switching**: `src/ui/StartTab.h/.cpp` (auto-detects extractor support for pasted URLs and switches download type: yt-dlp-only → Video, gallery-dl-only → Gallery, both/neither → no change).
- **Start Tab operational controls / Supported Sites button**: `src/ui/StartTab.h/.cpp` and `src/ui/StartTabUiBuilder.h/.cpp` (instant-save wiring for playlist logic, max concurrent, rate limit, and duplicate override, plus the dedicated Supported Sites dialog launcher).
- **External Binary Status / Install Actions**: `src/ui/advanced_settings/BinariesPage.h/.cpp` (shown under Advanced Settings -> External Tools; handles "Browse" for custom paths, "Clear Path" to reset, and "Install" for package managers) and `src/ui/MissingBinariesDialog.h/.cpp` (guided setup dialog shown at startup, enqueue time, and Start-tab format checks when required binaries are missing).
- **yt-dlp Progress Parsing / Binary Resolution**: `src/core/YtDlpWorker.h/.cpp` and split implementation files `YtDlpWorkerProcess.cpp`, `YtDlpWorkerOutput.cpp`, `YtDlpWorkerProgress.cpp`, and `YtDlpWorkerTransfers.cpp`. **Parses native yt-dlp stderr output for `[download] XX.X% of ...` lines directly (no `download:` prefix stripping), including fragment-based native downloader lines (intercepting `(frag X/Y)` tags to calculate overall percentage) and aria2c progress lines (`[#XXXXX ...]`). Tracks `[download] Destination:` targets and aria2 `FILE:` targets so auxiliary transfers such as thumbnails, subtitles, and `.info.json` writes no longer drive the main media progress bar, emits destination-aware stages like thumbnail/subtitle/video/audio download status, treats extraction/setup work as indeterminate instead of leaving the UI stuck on a stale queued `0%`, prefers `requested_downloads` `format_id` matches from temp filenames like `.f251.webm.part`, then active-stream total-size matches, then yt-dlp announced format lists plus aria2 command-line `itag`/`mime` hints when `requested_downloads` is missing, preserves that inferred order if a later `info.json` still omits `requested_downloads`, before falling back to ambiguous extensions such as `.webm` or generic progress-reset ordering, and falls back to the `requested_downloads` metadata order when progress restarts onto the next primary stream without a fresh destination line. Handles `[wait]` states by parsing countdown timers and fetching pre-wait metadata (using YouTube oEmbed API for instant YouTube fetches, or a background `yt-dlp` process for others) to display title and thumbnail during long waits. Caches full metadata from `info.json` in `m_fullMetadata` for sorting rule evaluation at download completion. Section downloads no longer add app-side FFmpeg merger args that override yt-dlp's native cut handling.**
- **Progress Bars**: `src/ui/ActiveDownloadsTab.h/.cpp` (updates UI based on worker signals). **`src/ui/DownloadItemWidget.h/.cpp` now treats worker/finalizer status text as the source of truth, no longer guesses video/audio phases from progress resets, and exposes a live-only `Finish Now` button that asks yt-dlp to stop recording gracefully before finalization.**
- **Active Downloads bulk actions**: `src/ui/ActiveDownloadsTab.h/.cpp` (toolbar actions for Stop All, Resume All, Clear Inactive, and quick folder access).
- **Queue State / App Restarts**: `src/core/DownloadQueueState.h/.cpp` (serializes active, paused, and stopped queue items including their `tempFilePath`, `originalDownloadedFilePath`, `metadata`, and cleanup-related per-item options to `downloads_backup.json` under the app-local data root, using the `Server/` subfolder in headless/server mode, so partial downloads, sorting metadata, and temp-file cleanup survive application restarts without splitting user preferences).
- **File Moving**: `src/core/DownloadFinalizer.h/.cpp` (moves files from temp to final output, uses `SortingManager` to determine the final directory, applies configurable playlist index prefixes, and removes per-download UUID temp folders).
- **Section clip container normalization**: `src/core/MetadataEmbedder.h/.cpp` (runs an asynchronous ffprobe+ffmpeg normalization pass on finished MP4-family section clips before finalization, using a probed `-t <clip_duration>` limit plus `-fix_sub_duration`/`-c:s mov_text` to constrain embedded subtitle streams to the clip length).
- **Sorting Rules**: `src/ui/SortingTab.h/.cpp` (UI) and `src/core/SortingManager.h/.cpp` (logic).
- **Cookies from Browser (Video/Audio)**: `src/ui/AdvancedSettingsTab.h/.cpp` (handles cookie access checks directly using `QProcess`).
- **Cookies from Browser (Galleries)**: `src/ui/AdvancedSettingsTab.h/.cpp` (handles cookie access checks directly using `QProcess`).
- **Override duplicate download check**: `src/ui/start_tab/StartTabDownloadActions.h/.cpp` (for saving config) and `src/ui/start_tab/StartTabCommandPreviewUpdater.h/.cpp` (for command preview).
- **Enable SponsorBlock / FFmpeg cut encoder**: `src/ui/start_tab/StartTabDownloadActions.h/.cpp` (for saving config), `src/ui/start_tab/StartTabCommandPreviewUpdater.h/.cpp` (for command preview), `src/ui/advanced_settings/DownloadOptionsPage.h/.cpp` (cut encoder UI), `src/core/DownloadManagerExecution.cpp` (SponsorBlock segment preflight), and `src/core/YtDlpArgsBuilder.h/.cpp` (injects `--force-keyframes-at-cuts` plus `ModifyChapters` and `SponsorBlock` FFmpeg input/output args only when needed or when preflight is unavailable, including timestamp normalization for built-in encoder modes).
- **Runtime video/audio format selection**: `src/core/DownloadManagerEnqueue.cpp` (triggering/fetch), `src/ui/FormatSelectionDialog.h/.cpp` (selection UI), and `src/ui/MainWindowConnections.cpp` (dialog orchestration).
- **yt-dlp error popups**: `src/core/DownloadManagerWorkers.cpp` (always forwards item metadata for popup actions) and `src/ui/MainWindowDownloads.cpp` (renders rich-text dialogs with clickable source URLs when available).
- **Runtime subtitle selection**: `src/ui/MainWindowDownloads.cpp` and `src/ui/RuntimeSelectionDialog.h/.cpp`.
- **Non-interactive CLI/API downloads**: `src/ui/MainWindowConnections.cpp`, `src/ui/MainWindowDownloads.cpp`, and `src/ui/MainWindowHelpers.h` (direct URL and Local API request defaults, prompt suppression), `src/core/DownloadManagerEnqueue.cpp` (archive override and runtime/section prompt bypass), and `src/core/DownloadManagerPlaylist.cpp` (playlist download-all).
- **Local API server**: `src/core/LocalApiServer.h/.cpp` (localhost listener, token auth, enqueue/status endpoints with optional `type` selection; routes `api_token.txt` to the `Server/` subfolder when headless/server), `src/ui/advanced_settings/ConfigurationPage.h/.cpp` (`General/enable_local_api` GUI toggle), and `src/ui/MainWindowConnections.cpp`/`src/ui/MainWindowDownloads.cpp` (server lifecycle starts automatically for `--server`/`--headless` without mutating that GUI preference, plus non-interactive enqueue routing).
- **Section download filename labeling**: `src/ui/DownloadSectionsDialog.h/.cpp` (human-readable label generation), `src/ui/MainWindowDownloads.cpp` (stores section label in queued options), and `src/core/YtDlpArgsBuilder.h/.cpp` (injects the label into the output filename template).
- **Discord Webhook Updates**: `src/ui/MainWindowDiscord.cpp` (captures download signals and fires asynchronous HTTP POST JSON payloads to `127.0.0.1:8766/webhook` to sync with the Python bot, utilizing a main-thread `QNetworkAccessManager` to prevent event loop silent failures. Includes a 1.5s throttle to prevent bombardment, sanitizes long/multi-line status text, preserves progress when queue refresh payloads omit it, passes `parent_id` from explicit playlist placeholder IDs for expanded playlist items, tracks `queue_position` for queued jobs including manual up/down reordering, and keeps completion/cancellation state long enough for the bridge to observe terminal updates).
- **Video/audio codec preference routing**: `src/ui/advanced_settings/VideoSettingsPage.h/.cpp` (codec UI and legacy-label normalization), `src/ui/MainWindowConnections.cpp` (marks runtime-selected format IDs as concrete overrides), and `src/core/YtDlpArgsBuilder.h/.cpp` (maps Advanced Settings codec choices and runtime format overrides to yt-dlp selectors).
- **Auto-paste URL behavior**: `src/ui/AdvancedSettingsTab.h/.cpp` (setting via `QComboBox`) and `src/ui/MainWindowUi.cpp` (clipboard monitoring and logic). **Includes a short 500 ms debounce and queue-duplicate checking to prevent multiple enqueues of the same URL while still allowing quick successive copies.**
- **Duplicate download prevention**: `src/core/DownloadManager.h/.cpp` (`getDuplicateStatus()` checks queued, active, paused, and completed states; emits `duplicateDownloadDetected` signal), `src/ui/StartTab.h/.cpp` (displays warning popup with reason and URL).
- **Advanced Settings navigation styling / grouping**: `src/ui/AdvancedSettingsTab.h/.cpp` (palette-driven `QListWidget` styling for the left menu, refreshed on palette changes; broad grouped sections keep the tab compact and beginner-friendly while preserving legacy navigation aliases).
- **Embed video chapters**: `src/ui/AdvancedSettingsTab.h/.cpp`.
- **Embed metadata**: `src/ui/advanced_settings/MetadataPage.h/.cpp`.
- **Embed thumbnail**: `src/ui/advanced_settings/MetadataPage.h/.cpp`.
- **Use high-quality thumbnail converter**: `src/ui/advanced_settings/MetadataPage.h/.cpp`.
- **Convert thumbnails to**: `src/ui/advanced_settings/MetadataPage.h/.cpp`.
- **Force Playlist as Single Album**: `src/ui/advanced_settings/MetadataPage.h/.cpp` (UI), `src/core/PlaylistExpander.h/.cpp` (playlist title carry-through), `src/core/YtDlpArgsBuilder.h/.cpp` (yt-dlp metadata args), `src/core/DownloadManagerWorkers.cpp` (metadata reinforcement before finalization), and `src/core/MetadataEmbedder.h/.cpp` (FFmpeg metadata rewrite).
- **Crop audio thumbnails to square**: `src/ui/advanced_settings/MetadataPage.h/.cpp`.
- **Generate folder.jpg for audio playlists**: `src/ui/advanced_settings/MetadataPage.h/.cpp`.
- **Subtitle language**: `src/ui/AdvancedSettingsTab.h/.cpp`.
- **Embed subtitles in video**: `src/ui/AdvancedSettingsTab.h/.cpp`.
- **Write subtitles (separate file)**: `src/ui/AdvancedSettingsTab.h/.cpp`.
- **Include automatically-generated subtitles**: `src/ui/AdvancedSettingsTab.h/.cpp`.
- **Subtitle file format**: `src/ui/AdvancedSettingsTab.h/.cpp`.
- **yt-dlp version display**: `src/ui/advanced_settings/BinariesPage.h/.cpp` (display), `src/core/StartupWorker.h/.cpp` (initial fetch).
- **Update yt-dlp**: `src/ui/advanced_settings/BinariesPage.h/.cpp`.
- **gallery-dl version display**: `src/ui/advanced_settings/BinariesPage.h/.cpp` (display), `src/core/StartupWorker.h/.cpp` (initial fetch).
- **Update gallery-dl**: `src/ui/advanced_settings/BinariesPage.h/.cpp`.
- **App Updates**: `src/core/AppUpdater.h/.cpp` (GitHub release lookup with fallback repo URLs).
- **Binary auto-detection and fallback resolution**: `src/utils/BinaryFinder.h/.cpp` and `src/core/ProcessUtils.h/.cpp`. **ProcessUtils now caches resolved paths after the first lookup; `resolveBinary()` performs a fresh scan, `findBinary()` uses the cache. Cache is cleared when binaries are installed/overridden via BinariesPage. It also handles clean version string extraction for external binaries.**
- **External Downloader (aria2c)**: `src/ui/AdvancedSettingsTab.h/.cpp`.
- **Restrict filenames**: `src/ui/AdvancedSettingsTab.h/.cpp`.
- **Show Debug Console**: `src/ui/AdvancedSettingsTab.h/.cpp` (Windows toggle UI) and `src/ui/MainWindowConnections.cpp` (runtime console allocation/show-hide logic).
- **UI Assets**: `src/ui/resources.qrc` (for embedded images and other resources).
- **Executable Icon**: `LzyDownloader.rc` (Windows resource file for the application icon).
- **Extractor Domain Lists**: `extractors_yt-dlp.json` and `extractors_gallery-dl.json` (copied beside `LzyDownloader.exe` for URL auto-paste).
- **Supported Sites Dialog**: `src/ui/SupportedSitesDialog.h/.cpp` (reads the extractor domain lists and provides a searchable table of supported domains and their media types. Launched via a dedicated button on the Start tab).
- **Extractor Loader**: `src/utils/ExtractorJsonParser.h/.cpp` (loads extractor domains from the app directory for clipboard checks).
- **Download Statistics Display**: `src/ui/MainWindowUi.cpp` (labels for queued, active, completed counts).
- **Initial Directory Setup**: `src/ui/MainWindowConnections.cpp` (prompts user for download directories on first launch).
- **Logging**: `src/utils/LogManager.h/.cpp` (installs a custom message handler for structured logging. **Creates one log file per run with timestamp in filename: `LzyDownloader_YYYY-MM-dd_HH-mm-ss.log`. Keeps up to 5 most recent log files, deleting older ones automatically. Headless/server logs are written under the app-local `Server/` subfolder so Discord-bot runs are separated from GUI logs**).
- **Download Item Widget**: `src/ui/DownloadItemWidget.h/.cpp` (displays thumbnail preview on the left side of the progress bar, loaded from `thumbnail_path` in progress data; updates title from progress data; shows the live-only `Finish Now` action; **uses custom `ProgressLabelBar` subclass that paints percentage, sizes, speed, and ETA centered directly on the progress bar**).
---

## 4.9 UI Builders (`src/ui/MainWindowUiBuilder.h/.cpp`, `src/ui/StartTabUiBuilder.h/.cpp`)
- **Responsibilities:** Encapsulate the creation and layout of UI elements for `MainWindow` and `StartTab` respectively. They provide methods to build the UI and return pointers to the created widgets, allowing the main classes to manage logic and connections without being cluttered with UI construction details.

## 4. Dependencies

The application is transitioning to an **unbundled external-binary model**.

Current expectations:
- Prefer user-installed or manually configured executables for `yt-dlp`, `ffmpeg`, `ffprobe`, `gallery-dl`, `aria2c`, and `deno`.
- Keep Qt runtime/plugin deployment self-contained, including `qsqlite.dll`.

Agents MUST NOT:
- Introduce new external runtime dependencies without explicit instruction.
- Break existing fallback support while bundled binaries still exist in the repository.
- Require blocking runtime downloads on the GUI thread.

---

## 5. Development Stack

- **Language**: C++20
- **Framework**: Qt 6 (Widgets, Core, Network, Sql)
- **Build System**: CMake
- **Database**: SQLite (via `QtSql` module)

---

## 6. Agent Rules (Read Carefully)

### You MUST:
- Keep the UI responsive (use `QThread`, `QtConcurrent`, or `QProcess`'s async API).
- **Update `CMakeLists.txt` for any new dependencies.** If you add code that requires a new Qt module (e.g., `QtXml`), library, or source file (`.cpp`, `.h`, `.ui`), you MUST update `CMakeLists.txt` accordingly.
    - **Example:** Adding a new class `MyClass` requires adding `src/core/MyClass.cpp` to the `add_executable` command in `CMakeLists.txt`.
    - **Example:** Using a new Qt module like `QtXml` requires adding `Xml` to the `find_package(Qt6 ... COMPONENTS ...)` list.
- **Preserve existing build paths and settings.** Do not modify existing `INCLUDEPATH` or `LIBS` entries in the build configuration unless it is the explicit goal of the task. The project relies on specific paths for its dependencies.
- **Assume a Windows (MSVC) primary toolchain.** While cross-platform compatibility is a goal, ensure all changes build correctly on Windows first. Avoid introducing Unix-specific flags or syntax.
- **Preserve the schema** of `download_archive.db`. (Note: `settings.ini` format is now pure Qt, backwards compatibility with Python is not required).
- Maintain clear, user-facing error messages.
- Respect the existing file lifecycle (temp -> final) and directory structure.
- Add logging (`QDebug`) for non-trivial changes.
- **Update AGENTS.md** (Architecture & Quick-Reference) if you add files or change core logic locations.
- **Ensure all UI elements have tooltips** (`setToolTip`). This includes all interactive controls (buttons, checkboxes, dropdowns, line edits) AND their accompanying descriptive labels.
- **File Size Limits (Context Preservation)**: Ensure that no single file (source code, headers, or documentation) exceeds **500 lines** in length (and `.md` files remain under 100KB) to preserve agent context usage. Refactor large C++ classes or split extensive markdown documents into smaller, logically separated files when approaching this limit.
- **Ensure Theme Compatibility**: All UI elements MUST be designed to work correctly in both light and dark themes. Avoid hardcoded colors; use the application's `QPalette` to ensure elements adapt to the current theme.
- **Update `SPEC.md`, `ARCHITECTURE.MD`, and `TODO.md`** to reflect any changes to functional requirements, system design, or pending tasks.
- **Discard Invalid Settings**: If any setting loaded from `settings.ini` does not match the current application's expected format, it MUST be discarded and replaced with the default value.
- **Update Documentation on Functional Changes**: When you make changes to how the app works (e.g., progress parsing, download pipeline, UI behavior, configuration, external binary handling), you MUST update the relevant MD documentation files (`AGENTS.md`, `SPEC.md`, `ARCHITECTURE.md`, `TODO.md`, `CHANGELOG.md`) to reflect the new behavior. This is a mandatory requirement - do not leave documentation out of sync with the code.
- **Use Q_INVOKABLE for Deferred Calls**: Methods called via `QMetaObject::invokeMethod` with `Qt::QueuedConnection` MUST be declared as `Q_INVOKABLE` in the header file, even if they are in the `private` or `private slots` sections. Without this, the invocation will fail silently at runtime with a warning like `No such method DownloadManager::saveQueueState()`.
### You MUST NOT:
- Change the schema of `download_archive.db` without a migration plan.
- Introduce new external runtime dependencies without explicit instruction.
- Break the standalone, portable nature of the application.
- Assume network availability beyond what's needed for `yt-dlp` and the app updater.
- Use native application menu bars (`QMenuBar`, `QMenu`, `QAction`) at the top of the main window. All navigation and actions MUST be handled via in-app UI elements (buttons, tabs, sidebars).
---
## 7. Task Tracking
Agents MUST use `TODO.md` to track pending tasks, planned features, and known issues. Before starting work, check `TODO.md` for high-priority items. After completing a task or identifying a new one, update `TODO.md` accordingly.
