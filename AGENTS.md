# AGENTS.md - AI Contributor Guide for LzyDownloader (C++ Port)

This document is the **canonical instruction set for all AI agents** working on the C++ port of the LzyDownloader project. It defines the project's purpose, architecture, constraints, and rules for safe and effective contributions.

All agents MUST follow this document as the source of truth.

**Fast Start:** Keep the UI responsive (no blocking I/O on the GUI thread; use `QThread` or `QtConcurrent`). Preserve the download lifecycle (temp → verify → move to completed dir). Prefer discovered or user-configured external binaries. Update `CHANGELOG.md` for significant changes. For releases, run `python build_release.py` on the target platform to compile and package. Update Section 3 + Quick-Reference list when files/roles change. For file locations, jump to the Quick-Reference list in Section 3.

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
- **Livestream Replay Handling**: The app MUST preserve yt-dlp `live_status` from playlist expansion, runtime metadata, and `info.json`. `post_live` and `was_live` items are completed replays and must download as archived media, while active/upcoming livestreams keep wait/finalization behavior. Generic URL-shape hints such as a path segment named `live` may be used before extractor metadata is available, but hostname-specific livestream or replay overrides are forbidden.
- **Browser Cookie Fallback**: When a download using `--cookies-from-browser` fails because browser-cookie extraction or browser-cookie extractor state breaks an otherwise public download, `YtDlpWorker` may retry once without browser cookies and must keep clear diagnostics for the terminal failure path.
- **Completed-with-warning Handling**: When `yt-dlp` exits non-zero after producing a final media file, the app may continue finalization only for recoverable post-processing/tool warnings, and it MUST preserve a visible completion warning instead of presenting the result as an ordinary clean success. Critical extractor diagnostics such as unavailable, private, removed, or policy-blocked media MUST still fail the item even if a stale final path was observed.
- **yt-dlp Diagnostic Guidance**: Worker output parsing MUST keep actionable error/warning lines for terminal diagnostics, surface missing FFmpeg/FFprobe and FFmpeg post-processing failures with specific guidance, and treat optional browser-impersonation warnings as completion recommendations rather than hard failures.

---

## 3. Architecture Overview (C++ Port)

**POLICY:** For a breakdown of the directory structure, file responsibilities, and where to find specific features (Quick-Reference), you MUST read `docs/ARCHITECTURE.md`.

## 4. Dependencies

The application is transitioning to an **unbundled external-binary model**.

Current expectations:
- Prefer user-installed or manually configured executables for `yt-dlp`, `ffmpeg`, `ffprobe`, `gallery-dl`, `aria2c`, and `deno`.
- External binary resolution should preserve manual overrides first, then consider the app-local `bin` folder, user AppData `bin` folders, system `PATH`, and discovered package-manager candidates through the shared resolver. Startup and the External Binaries page may persist freshly auto-detected best paths back to `settings.ini` so later runtime lookups use the same executable, but must track auto-detected paths separately from manual overrides so rediscovery never replaces an explicit user selection.
- External binary updates must avoid overwriting package-managed tools directly; prefer package-manager commands or tool-native self-updaters (`yt-dlp -U`, `gallery-dl -U`, `deno upgrade`) surfaced through the External Binaries UI.
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
- **Update Documentation on Functional Changes**: When you make changes to how the app works (e.g., progress parsing, download pipeline, UI behavior, configuration, external binary handling), you MUST update the relevant MD documentation files (`AGENTS.md`, `docs/SPEC.md`, `docs/ARCHITECTURE.md`, `TODO.md`, `CHANGELOG.md`, `docs/CODING_STANDARDS.md`) to reflect the new behavior. This is a mandatory requirement - do not leave documentation out of sync with the code.
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
