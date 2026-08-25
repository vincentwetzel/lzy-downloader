# AGENTS.md - AI Contributor Guide for LzyDownloader (C++ Port)

This document is the **canonical instruction set for all AI agents** working on the C++ port of the LzyDownloader project. It defines the project's purpose, architecture, constraints, and rules for safe and effective contributions.

All agents MUST follow this document as the source of truth.

**Fast Start:** Keep the UI responsive (no blocking I/O on the GUI thread; use `QThread` or `QtConcurrent`). Preserve the download lifecycle (temp -> verify -> move to completed dir). Prefer discovered or user-configured external binaries. Update the maintained docs and `CHANGELOG.md` for significant changes. For releases, run `python build_release.py` on the target platform to compile and package; the helper inherits the invoking terminal and does not launch a separate Windows shell. The NSIS finish page offers to launch the installed `LzyDownloader.exe` after a successful installation. Update Section 3 + Quick-Reference list when files/roles change. For file locations, jump to the Quick-Reference list in Section 3.

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
- **No site-specific overrides**: Agents MUST NOT add hardcoded per-domain behavior for individual services (for example special arguments, referers, downloader choices, or extractor workarounds for one named website). Fixes must be generic and based on extractor metadata, documented tool behavior, user-configured settings, protocol/URL structure that applies across sites, or upstream yt-dlp/gallery-dl support. Generic URL-shape hints such as detecting playlist-like or live-like path/query markers are acceptable only when they do not branch on a specific hostname.
- Concurrent download management with user-defined limits (capped at a maximum of 4 concurrent threads upon application startup, though users can manually increase it up to 8 during a session).
- Playlist expansion and processing (for `yt-dlp` downloads), including hostname-independent query index hints such as `img_index`, `slide`, `item`, `index`, and `playlist_index` so carousel or playlist item URLs can resolve to the intended entry without adding site-specific overrides.
- Playlist probing is a recoverable optimization for ordinary media URLs: if the asynchronous probe times out or returns a transient expansion/JSON error, non-playlist-shaped URLs must fall back to the normal single-item yt-dlp worker. Explicit playlist-shaped URLs and missing yt-dlp errors remain terminal probe failures.
- **Configuration**: The app uses a Qt-native `QSettings` INI implementation. It does not need to conform to Python `configparser` quirks. Playlist downloads prefix audio filenames with zero-padded playlist indices by default; users may disable this under Advanced Settings -> Download Options.
- **Argument construction safety**: Metadata-only playlist expansion must remain read-only even when duplicate/archive override is enabled. Generic aria2c referer forwarding is permitted only when the request URL has both a scheme and host; incomplete URLs must not produce a referer argument.
- **Archive Portability**: The C++ app MUST use the same `download_archive.db` (SQLite) to respect the user's download history.
- **Download History Links**: Valid HTTP/HTTPS source URLs in the Download History tab MUST be displayed as escaped, keyboard-accessible hyperlinks that open in the default browser; invalid or incomplete values remain plain text.
- File lifecycle: download into temp dir → verify file stability → move to completed downloads directory. Terminal finalization must also remove the guarded per-download UUID temp folder on failure exits, while stopped downloads retain their partial files for resume. Startup reconciles orphaned UUID folders after queue restoration while preserving stopped/failed IDs; the shared temp root and non-UUID folders are never removed by that sweep.
- Metadata embedding (title, artist, etc.) and thumbnail embedding.
- If yt-dlp leaves a downloaded thumbnail sidecar after its native embedding step, the existing `MetadataEmbedder` remux path MUST consume that tracked image as an `attached_pic` stream before terminal cleanup; it MUST NOT merely store the path as an unused QObject property.
- For audio playlist items, metadata embedding must preserve an explicit track-level `artist`; when it is absent, yt-dlp arguments may derive `artist` only from item-level `artists`, `creator`, `channel`, or `uploader` fields. Never use playlist-level owner fields such as `playlist_uploader` or `playlist_owner` as the track artist.
- Accurate SponsorBlock/section cuts must normalize audio timestamps rather than copying packets from the pre-cut timeline; FFmpeg cut work must remain resource-bounded and background priority where the platform supports it.
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
- **Queued Thumbnails**: When queue metadata contains a thumbnail URL, the Active Downloads row MUST begin its bounded asynchronous thumbnail request while the item is queued. Playlist expansion must preserve the thumbnail URL when replacing a single-item placeholder, and the UI must not wait for the main download worker to emit a converted thumbnail.
- **Compact Active Downloads Layout**: The Active Downloads scroll content and each download row MUST be allowed to shrink to the viewport width. Long title text MUST wrap/yield space so right-side Cancel, Retry, and folder actions remain visible; horizontal scrolling MUST remain disabled for the list.
- **Headless State Persistence**: The application MUST guarantee that final terminal states (such as a fully cleared queue upon completion) are successfully serialized to `downloads_backup.json` before `QCoreApplication::quit()` is called, especially during `--server --exit-after` execution flows that bypass `closeEvent()`.
- **Livestream Replay Handling**: The app MUST preserve yt-dlp `live_status` from playlist expansion, runtime metadata, and `info.json`. `post_live` and `was_live` items are completed replays and must download as archived media, while active/upcoming livestreams keep wait/finalization behavior. Livestream mode must not be inferred from URL words such as a path segment named `live`; use extractor metadata or an explicit livestream/wait option, and never add hostname-specific livestream or replay overrides.
- **Browser Cookie Fallback**: When a download using `--cookies-from-browser` fails because browser-cookie extraction or browser-cookie extractor state breaks an otherwise public download, `YtDlpWorker` may retry once without browser cookies and must keep clear diagnostics for the terminal failure path. It may also retry once when cookie-backed selection produces a low-resolution combined progressive stream for an uncapped or higher-capped `bestvideo` request; this covers manifests that omit the adaptive formats needed to prove the downgrade. Direct/runtime format choices, explicit caps at the selected resolution, and active livestreams are excluded. Detection must require explicit cookie/browser-database/sign-in evidence or the generic metadata-backed degraded selection; ordinary media text containing words such as `locked` must never terminate the worker or enter a retry state.
- **Livestream Wait Retry**: When yt-dlp's pre-wait livestream probe reports a stream as offline or unavailable while `--wait-for-video` or `--live-from-start` is active, `YtDlpWorker` may retry once without those wait flags. This recovery must stay generic and may not introduce hostname-specific livestream behavior.
- **Completed-with-warning Handling**: When `yt-dlp` exits non-zero after producing a final media file, the app may continue finalization only for recoverable post-processing/tool warnings, and it MUST preserve a visible completion warning instead of presenting the result as an ordinary clean success. Critical extractor diagnostics such as unavailable, private, removed, or policy-blocked media MUST still fail the item even if a stale final path was observed.
- **Incomplete Media Diagnostics**: Missing stream fragments, empty data blocks, invalid media headers, and FFmpeg input errors caused by invalid downloaded media MUST force terminal download failure even when yt-dlp printed a final path. The user-facing diagnostic must identify the transfer/media as incomplete rather than incorrectly blaming a missing FFmpeg installation.
- **yt-dlp Diagnostic Guidance**: Worker output parsing MUST keep actionable error/warning lines for terminal diagnostics, surface missing FFmpeg/FFprobe and FFmpeg post-processing failures with specific guidance, and treat optional browser-impersonation warnings as completion recommendations rather than hard failures.
- **Media-Type Quality Diagnostics**: The low-quality resolution warning MUST apply only to video downloads. Audio-only jobs may retain source video metadata such as `height`, and that metadata MUST NOT produce a video-quality warning. When an audio extraction uses a combined source, transfer status must remain audio-oriented even if the source MIME type is `video/*`.
- **Low-Quality Warning Context**: The video-quality warning MUST include the downloaded title when available and render a complete HTTP/HTTPS source URL as an escaped, keyboard-accessible hyperlink; incomplete URLs remain plain text.
- **aria2c Recovery**: Ordinary non-livestream downloads using aria2c MUST apply bounded retry/backoff and a conservative per-server connection limit. When aria2c reports a transient timeout, slow-transfer, network, or temporary-server failure (codes 2, 5, 6, or 29), or returns without the temporary media `.part` file that yt-dlp expects, `YtDlpWorker` may retry once with yt-dlp's native downloader after removing stale `.info.json` sidecars while preserving media `.part` files. The missing-output recovery must require an `Unable to download video` file-not-found diagnostic for the expected `.part` path. The worker MUST surface the recovery stage and retain terminal diagnostics if the native retry also fails.

---

## 3. Architecture Overview (C++ Port)

**POLICY:** For a breakdown of the directory structure, file responsibilities, and where to find specific features (Quick-Reference), you MUST read `docs/ARCHITECTURE.md`.

**Quick reference:** `src/core/YtDlpWorkerDiagnostics.cpp` owns reusable fatal/incomplete-media and bounded aria2c missing-output recovery classification used by yt-dlp output parsing and process completion. `src/core/YtDlpWorker.cpp` owns bounded browser-cookie recovery, including metadata-backed degraded-format recovery without hostname-specific client overrides. `src/core/YtDlpWorkerTransfers.cpp` owns stream-stage inference, including audio extraction from combined sources. `src/core/DownloadManagerWorkers.cpp` owns terminal quality-warning classification and must keep video-only checks separate from audio metadata. `src/core/YtDlpLiveStatus.h` owns the narrow metadata mapping for explicit yt-dlp upcoming/premiere diagnostics used during playlist-probe fallback. `src/core/AppUpdater.cpp` owns platform/CPU-matched release asset selection and Finder-based macOS DMG handoff. `src/core/MetadataEmbedder.cpp` owns the existing FFmpeg rewrite path, including remuxing a tracked abandoned thumbnail as `attached_pic` artwork before cleanup. `src/ui/MainWindowUiBuilder.cpp` owns the compact footer row layout, including keeping the exit-after-downloads control rightmost. The top-level `tests/` directory owns Qt regression tests, shared fixtures, workflow templates, and test-only helper scripts; `tests/run_headless_tests.py` owns timestamped build/test orchestration, fail-fast compilation, final summaries, and the build-local `--suspects` cache; `TestDownloadQueueState.cpp` covers queue-backup serialization/restoration and malformed-entry filtering, `TestDownloadTempCleanup.cpp` covers root fallback and ownership guards, and `TestYtDlpWorker.cpp` covers positive and negative aria2c recovery paths.

The worker regression coverage for degraded cookie-backed format selection is in `tests/TestYtDlpWorker.cpp`; keep it aligned with the recovery exclusions when changing selector or metadata logic.

## 4. Dependencies

The application is transitioning to an **unbundled external-binary model**.

Current expectations:
- Prefer user-installed or manually configured executables for `yt-dlp`, `ffmpeg`, `ffprobe`, `gallery-dl`, `aria2c`, and `deno`.
- External binary resolution should preserve manual overrides first, then consider the app-local `bin` folder, user AppData `bin` folders, system `PATH`, and discovered package-manager candidates through the shared resolver. Startup and the External Binaries page may persist freshly auto-detected best paths back to `settings.ini` so later runtime lookups use the same executable, but must track auto-detected paths separately from manual overrides so rediscovery never replaces an explicit user selection.
- Fresh interactive launches must use the guided binary setup flow: only offer a system-first versus app-managed-first choice when a system tool is actually available, select optional tools by default, and use visible install progress. App-managed tools may update automatically according to the configured cadence; external or package-managed tools must retain an explicit user-confirmed update flow.
- External binary updates must avoid overwriting package-managed tools directly; prefer package-manager commands or tool-native self-updaters (`yt-dlp -U`, `gallery-dl -U`, `deno upgrade`) surfaced through the External Binaries UI.
- Windows release CI must install NSIS before invoking `build_release.py`; the release helper should accept `makensis` from `PATH` as well as the standard local install path.
- Linux release CI must install vcpkg Qt Base's archive utilities (`curl`, `tar`, `unzip`, and `zip`), host build tools (`autoconf`, `automake`, `autoconf-archive`, `bison`, `flex`, and `libtool`), and XCB development packages (`^libxcb.*-dev`, `libx11-xcb-dev`, `libxkbcommon-dev`, `libxkbcommon-x11-dev`, `libxi-dev`, `libxrender-dev`, `libegl1-mesa-dev`, `libgl1-mesa-dev`, and `libglu1-mesa-dev`) before manifest resolution; runtime-only X11/XCB packages do not satisfy the forced XCB backend's header and pkg-config checks.
- Release validation installs yt-dlp from the prerelease channel with `python -m pip install --pre --upgrade yt-dlp` so current extractor/runtime behavior is exercised.
- Linux AppImage packaging must query the Qt `qmake` installed by the vcpkg release build; the separate Qt SDK used by Windows CI must not be selected for linuxdeploy. Release-only linuxdeploy helpers are cached under `build-release/tooling/`, and the final AppImage is emitted under `build-release/`. The release workflow supports manual non-publishing runs for validation and may generate a minimal tag-matched release-notes fallback when the repository does not contain one.
- Linux AppImage packaging detects static versus dynamic Qt before invoking linuxdeploy-plugin-qt. Static vcpkg Qt builds skip the plugin because their SQL directory contains `.a`/`.prl` files; dynamic builds retain Qt/SQLite deployment. vcpkg's `libdbus-1.so.3` remains excluded from linuxdeploy's ELF scan.
- macOS release CI must build separate Intel (`macos-13`, `clang_64`) and Apple Silicon (`macos-14`, `clang_arm64`) bundles with the hosted Qt SDK, run `macdeployqt`, and publish architecture-labelled DMGs. The app updater must only select a DMG matching the current CPU architecture; opening a DMG hands installation to Finder rather than attempting to execute it.
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
- **Update `docs/SPEC.md`, `docs/ARCHITECTURE.md`, and `TODO.md`** to reflect any changes to functional requirements, system design, or pending tasks.
- **Discard Invalid Settings**: If any setting loaded from `settings.ini` does not match the current application's expected format, it MUST be discarded and replaced with the default value.
- **Adhere to Coding Standards**: You MUST read and strictly follow the rules defined in `docs/CODING_STANDARDS.md` to ensure code quality, maintainability, and security.
- **Update Documentation on Functional Changes**: When you make changes to how the app works (e.g., progress parsing, download pipeline, UI behavior, configuration, external binary handling), you MUST update the maintained documentation set (`AGENTS.md`, `README.md`, `CHANGELOG.md`, `TODO.md`, `UPDATE_AND_RELEASE.md`, and the active files under `docs/`: `API_SURFACE.md`, `ARCHITECTURE.md`, `CODING_STANDARDS.md`, `FILE_MANIFEST.md`, `LANGUAGES.md`, `SETTINGS.md`, and `SPEC.md`) to reflect the new behavior. Historical entries in `docs/CHANGELOG_ARCHIVE.md` are reference material and must not be rewritten.
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

Windows FFmpeg/FFprobe installs stage extracted executables beside the destination and retry replacement for a bounded period, because active downloader/post-processing processes may temporarily hold the existing binaries open.

Local API automation clients may send `override_archive: true` (top-level or under `options`) on intentional re-download requests; the server forwards this explicit confirmation into the normal non-interactive queue path.

Application updates emit a pre-install handoff after the payload is saved. The main window must synchronously persist resumable queue state and terminate downloader/helper process trees before the installer is launched, because the updater's direct quit path bypasses `closeEvent()`. The Windows NSIS installer must relaunch the freshly installed `LzyDownloader.exe` in silent `/S` mode because that mode suppresses the finish page.

