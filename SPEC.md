# LzyDownloader C++ Specification

## 1. Overview
This document outlines the specifications for the C++ port of the LzyDownloader application. The goal is to create a drop-in replacement for the existing Python application, ensuring 100% feature parity and seamless transition for users.

### 1.1 Documentation Requirement
- **Mandatory**: When any agent or developer makes changes to how the app works (e.g., progress parsing, download pipeline, UI behavior, configuration, external binary handling), they MUST update the relevant MD documentation files (`AGENTS.md`, `SPEC.md`, `ARCHITECTURE.md`, `TODO.md`, `CHANGELOG.md`) to reflect the new behavior. Documentation MUST stay in sync with the code.

### 1.2 File Size Constraints
- **Context Preservation**: To ensure optimal performance with AI agents and preserve context usage, no single file (source code, headers, or documentation) should exceed **500 lines** in length. When a file approaches this limit, it must be refactored or split into smaller, focused modules.

## 2. Core Requirements

### 2.1. Single Instance Enforcement
- The application must ensure that only one instance of itself can run at any given time for a given mode. Standard GUI launches use one lock, while headless/server launches (`--headless`, `--server`) use a separate `_Server` lock, allowing one GUI instance and one background server instance to safely co-exist. User preferences remain shared through the main app-local `settings.ini`; only server/headless runtime state is isolated under `Server/`.

### 2.2. Configuration Compatibility
- **File Format**: `settings.ini` (INI format).
- **Location**: The main app-local user data directory, for example `%LOCALAPPDATA%\LzyDownloader\settings.ini` on Windows. GUI and server/headless mode must use this same file as the single source of truth for user preferences.
- **Parsing**: Must handle raw strings (no interpolation).
- **Legacy Support**: Backwards compatibility with the Python application's settings file is NO LONGER required. The application uses standard Qt `QSettings` behavior and automatically prunes unrecognised or legacy keys on startup to maintain a clean configuration file.
    - `restrict_filenames`, `exit_after`.

**Required settings keys include:**
    - `output_template`.
    - `subtitles_embed`, `subtitles_write`, `subtitles_langs`, etc.
    - `restrict_filenames`, `exit_after`.
    - `convert_thumbnails`, `high_quality_thumbnail`.
    - `use_aria2c`.
    - `force_playlist_as_album`.
    - `output_template`.
    - `cookies_from_browser`, `gallery_cookies_from_browser`.
    - `theme`.
    - `playlist_logic`, `max_threads`, `rate_limit`.
    - `override_archive`.
    - `enable_local_api`.
    - `embed_chapters`, `embed_metadata`, `embed_thumbnail`.
    - `video_quality`, `video_ext`, `vcodec`, `audio_quality`, `audio_ext`, `acodec`.
    - `lock_settings` for video and audio.
    - `livestream/live_from_start`, `livestream/wait_for_video`, `livestream/download_as`, `livestream/use_part`, `livestream/quality`, `livestream/convert_to`.

### 2.3. Archive Compatibility
- **Database**: `download_archive.db` (SQLite).
- **Schema**: Must match the Python version's schema exactly.
- **Logic**: URL normalization and duplicate detection logic must be identical to the Python version.

### 2.4. User Interface (Qt Widgets)
- **Main Window**: Tabbed interface with a footer containing links to GitHub and Discord.
- **Supported Sites Dialog**: A searchable dialog (accessible from the main UI) that lists all domains supported by yt-dlp and gallery-dl, indicating what type of media can be downloaded from each.
- **Start Tab**:
    - URL Input.
    - Clipboard auto-paste: when the URL field is focused/clicked, the app checks the clipboard against the extractor-domain list stored next to `LzyDownloader.exe` and auto-pastes a matching URL.
    - Auto-paste is controlled by the integer `General/auto_paste_mode` setting. Depending on the selected mode, focusing/hovering the app or detecting a new clipboard URL can auto-paste and optionally auto-enqueue. This logic is handled by `src/ui/start_tab/StartTabUrlHandler.h/.cpp` plus `MainWindow`.
    - Download Type dropdown, including "View Formats".
    - A dedicated **Supported Sites** button must open the searchable supported-domains dialog without relying on a native OS menu bar.
    - No per-download runtime quality/codec override dropdowns may appear on the Start tab; runtime format selection must be driven by Advanced Settings and download-time dialogs.
    - Video Settings group with quality, codec, extension, and audio codec defaults. Choosing `Quality = Select at Runtime` must hide the other video-format defaults on that page and defer the whole format decision to the runtime picker. Includes a "Lock Video Settings" checkbox.
    - Audio Settings group with quality, codec, and extension defaults. Choosing `Quality = Select at Runtime` must hide the other audio-format defaults on that page and defer the whole format decision to the runtime picker. Includes a "Lock Audio Settings" checkbox.
    - Operational Controls including Playlist logic, Max Concurrent downloads (capped at 4 on application startup), a global Rate Limit (app-wide, not per-download), and "Override duplicate download check". Changing these controls must instantly save to configuration and immediately react in the backend. "Exit after all downloads complete" is controlled from the main window footer.
- **Active Downloads Tab**:
    - Displays a list of queued, actively downloading, and completed items.
    - Each download GUI element must play/display a thumbnail preview for audio/video downloads on the left side of the widget.
    - The toolbar provides Stop All, Resume All, Clear Inactive, Open Temporary Folder, and Open Downloads Folder actions.
- **Advanced Settings Tab**:
    - **Organization**: Settings are grouped into logical sections:
        - **Configuration**: Output folder, Temporary folder, Theme, Enable Local API Server.
        - **Authentication Access**: Cookies from browser (Video/Audio), Cookies from browser (Galleries). The cookie access check is handled directly within `AdvancedSettingsTab` using `QProcess`, with a 30-second timeout and improved logging. The check uses a specific YouTube Shorts URL for more reliable validation.
        - **Output Template**: Filename Pattern (with "Insert token...", "Save", and "Reset" buttons). The "Save" button validates the pattern using `yt-dlp`.
        - **Download Options**: External Downloader (aria2c), Enable SponsorBlock, Restrict filenames, Embed video chapters, Enable Download Sections, and the multi-mode auto-paste setting.
        - **Metadata / Thumbnails**: Embed metadata, Embed thumbnail, Use high-quality thumbnail converter, Convert thumbnails to, Force Playlist as Single Album.
        - **Livestream Settings**: Record from beginning, Wait for video (with min/max intervals). The app dynamically scales these wait intervals for streams hours away vs seconds away. Download As (MPEG-TS or MKV), Use .part files, Quality, Convert To.
        - **Subtitles**: Subtitle language (using full words in a combo box), Embed subtitles in video, Write subtitles (separate file), Include automatically-generated subtitles, Subtitle file format (greyed out if "Embed subtitles in video" is selected).
        - **External Binaries**: Per-binary status rows for `yt-dlp`, `ffmpeg`, `ffprobe`, `gallery-dl`, `aria2c`, and `deno`, with brief explanations of what each tool does, auto-detection status, manual `Browse` overrides, and `Install` actions that offer detected package-manager commands plus manual-download links. yt-dlp install suggestions should prefer nightly-capable commands where the platform supports them and clearly label stable-only package-manager options.
        - **Restore defaults** button.
    - **Navigation Styling**: The left column uses a palette-aware `QListWidget` whose stylesheet is rebuilt on palette changes so the category list stays compact and theme-consistent without reverting to a plain scrollbar-heavy layout.
    - **Saving Behavior**: Most settings auto-save on change. The "Output Template" requires a dedicated "Save" button.
- **System Integration**: A system tray icon for quick show/hide and quit actions. Clicking the window close button (`X`) must exit the application (it must not keep running in the background).
- **Local API Server**: In GUI mode, when `General/enable_local_api` is enabled, the app must bind a small HTTP API only to localhost (`127.0.0.1:8765`). In `--server` or `--headless` mode, the API must start automatically because the launch mode explicitly requested server behavior, without writing `General/enable_local_api=true`. The API requires a Bearer token stored in `api_token.txt` (or `Server/api_token.txt` if running headless/server), accepts `POST /enqueue` with a JSON `url` plus optional `type` (`video`, `audio`, or `gallery`), and returns queue snapshots from `GET /status`. API and direct CLI requests must be treated as non-interactive: no modal prompts, playlist download-all behavior, runtime picker bypasses, and log-only UI warnings.
- **Theming**: Support for Light, Dark, and System themes.

### 2.5. Download Engine (yt-dlp & gallery-dl)
- **Execution**: `QProcess` to run `yt-dlp.exe` or `gallery-dl.exe`.
- **Launch Error Handling**: If process start fails (`QProcess::FailedToStart` or related launch errors), the download must transition to a terminal error state with a clear message (no indefinite "Downloading..." state).
- **Binary Discovery & Enforcement**: On launch, the application must search for required external binaries in system `PATH` and other common locations. The application must actively prevent the user from queuing video/audio downloads if `yt-dlp`, `ffmpeg`, `ffprobe`, or `deno` are missing, and block gallery downloads if `gallery-dl`, `ffmpeg`, or `ffprobe` are missing. Interactive missing-binary flows must open a guided setup dialog that lists the missing tools, offers install and browse actions, and re-scans status in place instead of only directing users to the Advanced Settings page.
- **Binary Management UI**: The External Binaries page must show the detected/configured path and current version for each supported binary. `yt-dlp` and `gallery-dl` update actions must live there, and in-app binary updates must refuse to overwrite package-managed installations.
- **Argument Construction**: Must dynamically build arguments based on all user settings, including:
    - Config isolation (`--ignore-config`) so user-level yt-dlp config files cannot override app-controlled downloads, including headless/API jobs.
    - Subtitle flags (`--write-subs`, `--embed-subs`, `--sub-langs` including the special `live_chat` value).
    - Filename restriction (`--restrict-filenames`).
    - External downloader (`--external-downloader aria2c`).
    - Thumbnail conversion (`--convert-thumbnails`).
    - SponsorBlock (`--sponsorblock-remove all`; video/livestream jobs preflight SponsorBlock segment availability and only add `--force-keyframes-at-cuts` plus cut-encoder postprocessor args when segments are confirmed or the preflight cannot be completed).
    - Cookies from browser (`--cookies-from-browser`).
    - Download sections (`--download-sections`).
    - Output template (`-o`).
    - Max concurrent downloads (`--concurrent-fragments`).
    - Rate limit (`--limit-rate`).
    - Override archive (`--no-download-archive`).
    - **Playlist Album Unification**: When `Force Playlist as Single Album` is enabled for audio playlist downloads, the builder must inject `--parse-metadata "playlist_title:%(album)s"` and `--parse-metadata "Various Artists:%(album_artist)s"` to ensure consistent album grouping in local music players.
    - Embed chapters (`--embed-chapters`).
    - **Section Container Preservation**: Section downloads must preserve the user's requested output container/extension (for example MP4 video clips stay MP4) instead of forcing an intermediate MKV remux. The app may add `--force-keyframes-at-cuts` for video precision, but it must not silently switch the container behind the user's back.
    - **Section Filename Labeling**: When a section/chapter clip is queued, the output filename must include a filename-safe label describing the selected range or chapter (for example `[section 15-00_to_end]`) before the extension so the saved file clearly identifies which part of the source it contains.
    - **Codec Preference Fidelity**: Advanced Settings video/audio codec choices must map to yt-dlp selectors that recognize common codec aliases reported by sites and containers (for example H.264 matching `avc1` streams, H.265 matching `hev1`/`hvc1`, and AAC matching `mp4a`), rather than only matching one literal codec token.
    - **Runtime Format Overrides**: When the runtime picker supplies a concrete `format` ID, the downloader must treat it as an explicit `-f` override instead of re-opening the picker or falling back to the saved quality/codec defaults. If video and audio runtime format IDs are selected separately, both IDs must be merged into the final video `-f` expression.
- **Output Parsing**: Must parse `yt-dlp` stdout/stderr for progress, final filename, and metadata JSON.
- **Headless Runtime-State Isolation**: Headless/server runs must use the shared main `settings.ini` for user preferences and the `Server/` app-data subfolder for runtime state such as `downloads_backup.json`, `api_token.txt`, and timestamped log files so Discord-bot/API activity can be diagnosed separately from GUI sessions without creating a second preferences profile.
- **Process Lifetime on Exit**: Closing the application must explicitly terminate active downloader/post-processor process trees (including child tools such as `aria2c` and `ffmpeg`) as well as any transient utility processes (updaters, validators, template checkers, cookie testers) so no background tasks survive after the UI exits. **This termination must be non-blocking to the GUI thread (e.g., using detached OS commands for process tree cleanup) to prevent UI freezes when mass-stopping downloads or exiting.**
- **Progress Compatibility**: The worker MUST understand and emit progress from **both** native `yt-dlp` progress lines **and** aria2c external downloader output, including:
  - Native yt-dlp format: `[download] XX.X% of YY.YMiB at ZZ.ZMiB/s ETA 0:00`
  - aria2c format: `[#XXXXX AA.AMiB/BB.BMiB(CC.C%)(...)ETA:0:00]`
  - Unknown or approximate totals (e.g., `~XX.XMiB`, `Unknown total size`)
  - Destination-aware native stages, including fragment downloads and auxiliary-file transfers
  - Primary-stream handoff detection must correctly switch the label from video to audio when multi-stream downloads restart progress on the next requested format, even if the temporary filename ends in `.part` or yt-dlp omits a fresh destination line
  - Stream labeling should prefer yt-dlp `format_id` clues from temp filenames such as `.f251.webm.part` before falling back to container extensions, since extensions like `.webm` can represent either audio-only or video streams
  - **Download Sections Dialog**: When section downloads are enabled, the application presents a dialog. This dialog must include a visible instruction indicating how users can disable the "Download Sections" prompt in the Advanced Settings tab if they no longer wish to use it.
- If `info.json` does not include `requested_downloads`, stream labeling should fall back to yt-dlp's announced format list (for example `Downloading 1 format(s): 399+251-13`) and aria2 command-line URL metadata such as `itag=251` and `mime=audio/webm` before relying on ambiguous extensions or progress-reset heuristics
- If stream order was already inferred from stderr/stdout and `info.json` later arrives without `requested_downloads`, the worker must preserve the inferred order rather than clearing it and regressing the visible stage label
  - If no trustworthy filename handoff is available yet, the worker should also consider the active stream's emitted total size as a secondary clue before falling back to simple progress-reset ordering
  - The displayed downloaded/total bytes for multi-stream jobs should remain scoped to the active stream rather than being silently converted into an aggregate whole-job byte counter
  - The UI may additionally show a small secondary overall-progress indicator for multi-stream jobs, but it must remain visually subordinate to the main active-stream progress bar
  - Auxiliary transfers (thumbnails, subtitles, `.info.json`) must not hijack the main media progress percentage/size display
  - aria2 `FILE:` lines should be used as an additional source of truth for the active transfer target so stage labels stay aligned with the actual stream being downloaded
  - Pre-download extraction/setup stages should drive the main bar into an indeterminate state instead of leaving a stale queued `0%` display on screen
  - UTF-8 filenames and metadata
  - **The progress bar MUST update correctly regardless of which downloader (native or aria2c) is active**
- **Progress Bar Color Coding**: The UI progress bar MUST use color-coding to provide clear visual feedback on download state:
  - **Colorless/Default** (no custom stylesheet): When download is queued, initializing, or in indeterminate state (progress < 0)
  - **Light Blue** (`#3b82f6`): While actively downloading (0% < progress < 100%)
  - **Teal** (`#008080`): During post-processing phase (indeterminate scrolling animation with status containing "Processing", "Merging", "Finalizing", etc.)
  - **Green** (`#22c55e`): When download is fully completed (progress at 100% and post-processing finished)
- **Detailed Progress Information Display**: The application MUST provide comprehensive, real-time progress information to users, comparable to command-line yt-dlp output:
  - **Status Label**: Must display the current download stage with descriptive messages:
    - Stage text must come from the active worker/finalizer lifecycle and must not be inferred in the UI from percentage resets alone
    - "Extracting media information..." (during metadata extraction)
    - "Downloading N segment(s)..." (during aria2c multi-segment downloads)
    - "Merging segments with ffmpeg..." (during post-processing)
    - "Verifying download completeness..." (during file stability checks)
    - "Applying sorting rules..." (during directory sorting)
    - "Moving to final destination..." / "Copying file to destination..." (during file movement)
    - "Embedding metadata..." (during metadata embedding for audio playlists)
    - "Next check in MM:SS" (while waiting for a scheduled livestream)
  - **Progress Details**: Below the progress bar, display formatted details including:
    - File sizes: "Downloaded / Total" (e.g., "15.3 MiB / 45.7 MiB")
    - Download speed (e.g., "Speed: 2.4 MiB/s")
    - Estimated time remaining (e.g., "ETA: 0:12")
  - All information MUST be separated by bullet points (•) for readability
  - Workers MUST emit detailed progress data including speed, ETA, downloaded_size, and total_size
- **Immediate Queue UI Feedback**: Downloads MUST appear in the Active Downloads tab immediately upon queuing:
  - **Gallery downloads**: Appear instantly with "Queued" status
  - **Video/Audio downloads**: Appear instantly with "Checking for playlist..." status while playlist expansion runs asynchronously
    - **Queue state persistence**: `DownloadQueueState` class handles saving/loading queue state, deferring calls via `Qt::QueuedConnection` to prevent GUI thread blocking.
  - **Single videos**: Status updates from "Checking for playlist..." to "Queued" once expansion completes. `DownloadQueueManager` handles updating the placeholder item.
  - **Playlists**: Placeholder item is removed and replaced with individual items for each track
  - Queue state persistence (handled by `DownloadQueueManager`) MUST be deferred via `Qt::QueuedConnection` to prevent GUI thread blocking
  - Playlist-derived items must preserve enough metadata (`is_playlist`, `playlist_title`, and playlist index/context) for downstream sorting rules and finalization to treat them as playlist downloads instead of single-item fallbacks.
- **Runtime Format Selection**: When Advanced Settings `Quality` is set to `Select at Runtime` for video or audio downloads, the app must asynchronously fetch format metadata with `yt-dlp` and present a structured selection dialog. Selecting multiple formats must enqueue one download per selected format.
- **Encoding Robustness**: Worker process environment must force UTF-8 text output (`PYTHONUTF8=1`, `PYTHONIOENCODING=utf-8`) so Unicode filenames are preserved in stdout/stderr parsing.

### 2.6. Post-Processing
- **File Lifecycle:**
    - Section clips saved in MP4-family containers may run through one additional asynchronous ffprobe+ffmpeg normalization pass before final move. The app probes the clipped container duration with `ffprobe`, then remuxes with `-fflags +genpts`, `-ignore_editlist 1`, `-fix_sub_duration`, `-c:s mov_text`, `-t <clip_duration>`, `-shortest`, and `-movflags +faststart` so embedded subtitle streams are hard-limited to the clip timeline and players like VLC read the clipped duration more accurately.
    - Un-embedded thumbnails (e.g., from `.ts` to `.mp4` livestream remuxes) are automatically detected and injected into the final container using a native FFmpeg fallback pass.
    - All downloads must go to a temporary directory first.
    - **Resuming and Clearing:** Partially downloaded files are retained in the temporary directory when a download is stopped. `DownloadQueueState` persists the current `tempFilePath`, `originalDownloadedFilePath`, and tracked cleanup candidates so users can resume or manually clean up across application restarts.
    - **Manual Cleanup:** If a user clicks "Clear Temp Files" for a stopped or failed download, the application must use the tracked cleanup candidate paths gathered during the worker run and then apply literal stem matching to sweep up associated media files, fragments (`.part-Frag`), tracking files (`.aria2`, `.ytdl`), metadata (`.info.json`), thumbnails, subtitles, and other partial sidecars. The cleanup filter must use literal string matching (`==` and `startsWith`) rather than wildcard globbing to prevent failure when video titles contain bracket characters `[]` (like YouTube IDs).
    - A file stability check must be performed before moving.
    - Files are moved to a final destination directory upon success.
    - Final file movement must be Unicode-safe and shell-independent (use Qt file APIs with copy/remove fallback for cross-volume moves).
- **Audio Playlist Tagging**: For audio downloads from a playlist, `ffmpeg` must be used as a post-processing step to embed the `track` number metadata into the completed file.
- **Filename Prefixing**: Audio files from playlists must have their filenames prefixed with a zero-padded track number (e.g., `01 - Title.mp3`).

### 2.7. Updaters & Deployment
- **Application Updater**: Checks GitHub for new application releases and provides a download/install mechanism.
- **Application Update UX**: Startup update checks must wire `AppUpdater` signals to a user prompt, compare dotted version numbers numerically, and prefer installer assets named `LzyDownloader-Setup-*.exe`.
- **Dependency Management**:
  - Provides a UI for users to see the status of external binaries (`yt-dlp`, `ffmpeg`, etc.), manually locate them, or trigger an installation process.
  - The installation process will guide the user through automated (package manager) or manual (web download) methods.
  - Startup checks, enqueue-time checks, and Start-tab format checks must surface missing required binaries through the guided setup dialog and reuse the External Binaries install/browse logic.
- **Executable Name**: Must be `LzyDownloader.exe`.
- **Binaries**: The application does not bundle external binaries. It requires the user to have `ffmpeg`, `ffprobe`, `deno`, and `yt-dlp` installed. `gallery-dl` and `aria2c` are optional.
- **Qt Image Plugins**: Windows builds must deploy the Qt `imageformats` plugins required to display active-download thumbnails and converted artwork, including JPEG, PNG, WebP, and ICO support.
- **Qt TLS Runtime**: Windows builds must deploy OpenSSL runtime DLLs (`libcrypto-3-x64.dll`, `libssl-3-x64.dll`) beside the executable when Qt's OpenSSL backend requires them.

### 2.8. Logging
- A structured file logger (`LzyDownloader_YYYY-MM-dd_HH-mm-ss.log`) must be implemented to capture application output for debugging.
- **One log file per run**: Each application launch creates a new log file with a timestamp in the filename.
- **Log retention**: The application automatically keeps only the 5 most recent log files, deleting older ones on startup.
- Log files must be stored in the user's AppData configuration directory (`%LOCALAPPDATA%\LzyDownloader\` on Windows).
- The app-update checker must tolerate repository renames by trying a primary GitHub Releases API URL plus fallback repository API URLs before surfacing an update-check failure.

## 3. Technical Stack
- **Language**: C++20
- **Framework**: Qt 6 (Widgets)
- **Build System**: CMake
- **Qt SDK Discovery**: CMake must honor explicit `Qt6_DIR`/`CMAKE_PREFIX_PATH` configuration and also auto-check common Windows Qt install prefixes (for example `C:\Qt\6.*\msvc2022_64`) so IDE-driven configure steps can find Qt without manual edits on typical developer machines.
- **Database**: SQLite (via Qt SQL module)
- **Process Management**: `QProcess`
