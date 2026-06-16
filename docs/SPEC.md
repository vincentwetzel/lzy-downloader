# LzyDownloader C++ Specification

## 1. Overview
This document outlines the specifications for the C++ port of the LzyDownloader application. The goal is to create a drop-in replacement for the existing Python application, ensuring 100% feature parity and seamless transition for users.

### 1.1 Documentation Requirement
- **Mandatory**: When any agent or developer makes changes to how the app works (e.g., progress parsing, download pipeline, UI behavior, configuration, external binary handling), they MUST update the relevant MD documentation files (`AGENTS.md`, `docs/SPEC.md`, `docs/ARCHITECTURE.md`, `TODO.md`, `CHANGELOG.md`) to reflect the new behavior. Documentation MUST stay in sync with the code.

### 1.2 File Size Constraints
- **Context Preservation**: To ensure optimal performance with AI agents and preserve context usage, no single file (source code, headers, or documentation) should exceed **500 lines** in length. When a file approaches this limit, it must be refactored or split into smaller, focused modules.

## 2. Core Requirements

### 2.1. Single Instance Enforcement
- The application must ensure that only one instance of itself can run at any given time for a given mode. Standard GUI launches use one lock, while headless/server launches (`--headless`, `--server`, `--background`) use a separate `_Server` lock, allowing one GUI instance and one background server instance to safely co-exist. User preferences remain shared through the main app-local `settings.ini`; only server/headless runtime state is isolated under `Server/`.
- Startup must recover cleanly from stale Qt shared-memory segments left by a previous crash by attaching/detaching before creating the active single-instance marker, and semaphore release must be exception/early-return safe.

### 2.2. Configuration Compatibility
- **File Format**: `settings.ini` (INI format).
- **Location**: The main app-local user data directory, for example `%LOCALAPPDATA%\LzyDownloader\settings.ini` on Windows. GUI and server/headless mode must use this same file as the single source of truth for user preferences.
- **Parsing**: Must handle raw strings (no interpolation).
- **Legacy Support**: Backwards compatibility with the Python application's settings file is NO LONGER required. The application uses standard Qt `QSettings` behavior and automatically prunes unrecognised or legacy keys on startup to maintain a clean configuration file.

**Required configuration layout, settings groups, and valid key types are documented thoroughly in docs/SETTINGS.md.**

### 2.3. Archive Compatibility
- **Database**: `download_archive.db` (SQLite).
- **Schema**: Must match the Python version's schema exactly.
- **Logic**: URL normalization and duplicate detection logic must be identical to the Python version.
- **Threading**: SQLite access must use Qt SQL connections scoped to the calling thread. Archive shutdown/cleanup must close and remove only the current thread's connection so tests and shutdown release locks without touching connections owned by other threads.

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
    - When Playlist logic is `Ask`, detected multi-item playlists must let the user queue every item, queue only selected items through a range/checkbox selector, queue only the first item, or cancel. Partial selection ranges are one-based and comma-separated, such as `1-5, 8, 11-13`.
- **Active Downloads Tab**:
    - Displays a list of queued, actively downloading, and completed items.
    - Each download GUI element must play/display a thumbnail preview for audio/video downloads on the left side of the widget. Remote thumbnails must use bounded network requests and may share an app-owned `QNetworkAccessManager` instead of creating one manager per widget.
    - The toolbar provides Stop All, Resume All, Clear Inactive, Open Temporary Folder, and Open Downloads Folder actions.
    - Adding a row for an existing internal download ID must replace the previous row instead of rendering duplicates, so placeholder refreshes, restored items, and playlist transitions keep the UI one-row-per-download.
    - Stopped, failed, or cancelled rows must show their row-level `Clear Temp` action only when the item has an existing per-download UUID temp folder, tracked temp path, original downloaded temp path, or persisted cleanup candidate on disk.
- **Advanced Settings Tab**:
    - **Organization**: Settings are grouped into logical sections:
        - **Configuration**: Output folder, Temporary folder, Theme, Enable Local API Server.
        - **Authentication Access**: Cookies from browser (Video/Audio), Cookies from browser (Galleries). The cookie access check is handled directly within `AdvancedSettingsTab` using `QProcess`, with a 30-second timeout and improved logging. The check uses a specific YouTube Shorts URL for more reliable validation.
        - **Output Template**: Filename Pattern (with "Insert token...", "Save", and "Reset" buttons). The "Save" button validates video/audio templates asynchronously using `yt-dlp` with explicit timeouts and guarded callbacks so the settings page remains responsive. Blank video/audio templates inherit the current shared default, while gallery templates use gallery-dl syntax and reset to the factory gallery default.
        - **Download Options**: External Downloader (aria2c), Enable SponsorBlock, Restrict filenames, Embed video chapters, Enable Download Sections, and the multi-mode auto-paste setting.
        - **Metadata / Thumbnails**: Embed metadata, Embed thumbnail, Use high-quality thumbnail converter, Convert thumbnails to, Force Playlist as Single Album.
        - **Livestream Settings**: Record from beginning, Wait for video (with min/max intervals). The app dynamically scales these wait intervals for streams hours away vs seconds away, clamps the minimum wait interval to at least 15 seconds, and repairs invalid max/min pairs. Download As (MPEG-TS or MKV), Use .part files, Quality, Convert To.
        - **Subtitles**: Subtitle language (using full words in a combo box), Embed subtitles in video, Write subtitles (separate file), Include automatically-generated subtitles, Subtitle file format (greyed out if "Embed subtitles in video" is selected).
        - **External Binaries**: Per-binary status rows for `yt-dlp`, `ffmpeg`, `ffprobe`, `gallery-dl`, `aria2c`, and `deno`, with brief explanations of what each tool does, auto-detection status, current version, update warnings, manual `Browse` overrides, `Clear` override actions, and `Install` actions that offer detected package-manager commands plus manual-download links. yt-dlp install suggestions should prefer nightly-capable commands where the platform supports them and clearly label stable-only package-manager options. The app may install FFmpeg/FFprobe together into its local `bin` folder on Windows and should save sibling overrides together when appropriate. Install and update progress dialogs must use the app-managed process environment, allow cancellation by terminating the process tree, quote command paths with spaces, close stdin for helper processes, guard dialog callbacks after destruction, clear update-warning flags after successful updates, and report permission-denied failures clearly. Version checks must be bounded by a short timeout and parsed into compact one-line versions instead of saving raw multiline tool banners. Saved binary paths must distinguish auto-detected paths from explicit user overrides so startup rediscovery can refresh stale automatic choices without replacing manual selections.
        - **Restore defaults** button.
    - **Navigation Styling**: The left column uses a palette-aware `QListWidget` whose stylesheet is rebuilt on palette changes so the category list stays compact and theme-consistent without reverting to a plain scrollbar-heavy layout.
    - **Saving Behavior**: Most settings auto-save on change. The "Output Template" requires a dedicated "Save" button.
- **System Integration**: A system tray icon for quick show/hide and quit actions. Clicking the window close button (`X`) must exit the application (it must not keep running in the background).
- **Local API Server**: In GUI mode, when `General/enable_local_api` is enabled, the app must bind a small HTTP API only to localhost (`127.0.0.1:8765`). In `--server`, `--headless`, or `--background` mode, the API must start automatically because the launch mode explicitly requested server behavior, without writing `General/enable_local_api=true`. The API requires a Bearer token stored in `api_token.txt` (or `Server/api_token.txt` if running headless/server), accepts `POST /enqueue` with a JSON `url` plus optional `type` (`video`, `audio`, or `gallery`) and optional caller-provided `id`, and returns queue snapshots from `GET /status`. If `id` is omitted, the API must generate a UUID. Requests must be payload-bounded, empty or malformed request lines must return JSON errors instead of hanging, and Host/Origin validation must only allow localhost plus trusted browser-extension origins. Permitted browser-extension and localhost origins may receive CORS headers, and authorized `OPTIONS` preflight requests must return `204 No Content`; unauthorized origins must still be rejected before CORS headers are granted. API and direct CLI requests must be treated as non-interactive: no modal prompts, playlist download-all behavior, runtime picker bypasses, and log-only UI warnings. The application must also emit real-time HTTP POST JSON payloads to `http://127.0.0.1:8766/webhook` during download progress, completion, and cancellation, utilizing a main-thread `QNetworkAccessManager` to prevent event loop silent failures, sending immediately when status or numeric progress changes while throttling lower-value active-download refreshes, applying explicit request timeouts, sanitizing long or multi-line status strings, preserving terminal state long enough for bridge clients to observe completion/cancellation, propagating `parent_id` for playlist items via explicit playlist placeholder IDs, and tying webhook callbacks to the main-window context so queued network completions cannot outlive the bridge owner.
- **Theming**: Support for Light, Dark, and System themes.

### 2.5. Download Engine (yt-dlp & gallery-dl)
- **Execution**: `QProcess` to run `yt-dlp.exe` or `gallery-dl.exe`.
- **Launch Error Handling**: If process start fails (`QProcess::FailedToStart` or related launch errors), the download must transition to a terminal error state with a clear message (no indefinite "Downloading..." state).
- **Binary Discovery & Enforcement**: On launch, the application must search for required external binaries in manual overrides, the app-local `bin` directory, user AppData `bin` directories, system `PATH`, and common package-manager or user-local locations. Startup must clear stale binary-resolution cache entries, rediscover all candidates, choose the newest usable executable with bounded version probes, persist the selected auto-detected path to `settings.ini`, remember whether that path was auto-detected, and clear the cache again so later downloader launches use the same path. Manual overrides remain the highest priority during normal runtime resolution and must not be overwritten by startup rediscovery. The application must actively prevent the user from queuing video/audio downloads if `yt-dlp`, `ffmpeg`, `ffprobe`, or `deno` are missing, and block gallery downloads if `gallery-dl`, `ffmpeg`, or `ffprobe` are missing. Interactive missing-binary flows must open a guided setup dialog that lists the missing tools, offers install and browse actions, and re-scans status in place instead of only directing users to the Advanced Settings page.
- **Binary Management UI**: The External Binaries page must show the detected/configured path and current version for each supported binary, plus update warnings when startup/version checks detect a newer available release. Status refreshes may save auto-detected paths when no explicit path exists, keeping the UI and runtime resolver aligned. Updates must run through package-manager commands or tool-native self-updaters instead of directly overwriting package-managed executables, and successful updates must refresh binary status/version labels. Update checks must re-probe the active local executable before comparing remote versions and must handle semantic, date-like, and FFmpeg/FFprobe version-resource values consistently.
- **Application Updates**: GitHub release checks must validate JSON object/array types before reading release notes or assets, skip malformed asset entries, compare normalized semantic/date-like version tags, and report a clear update-check failure if a newer release has no usable assets.
- **Argument Construction**: Must dynamically build arguments based on all user settings, including:
    - Config isolation (`--ignore-config`) so user-level yt-dlp config files cannot override app-controlled downloads, including headless/API jobs.
    - Subtitle flags (`--write-subs`, `--embed-subs`, `--sub-langs` including the special `live_chat` value).
    - Filename restriction (`--restrict-filenames`).
    - External downloader (`--external-downloader aria2c`) for ordinary non-livestream downloads only; active/upcoming livestreams, completed live replays (`post_live`/`was_live`), and generic `/live/` URL-shape hints must stay on yt-dlp's native downloader so wait-state, replay manifests, and graceful finish handling remain reliable.
    - Thumbnail conversion (`--convert-thumbnails`).
    - SponsorBlock (`--sponsorblock-remove all`; non-livestream video jobs preflight SponsorBlock segment availability and only add `--force-keyframes-at-cuts` plus cut-encoder postprocessor output args when segments are confirmed or the preflight cannot be completed. Livestream jobs must skip SponsorBlock because active captures do not have stable removable segments or chapters. The builder must avoid injecting FFmpeg input options such as `-ignore_editlist` into yt-dlp's ModifyChapters/SponsorBlock input phase because unsupported FFmpeg builds can reject them before post-processing begins).
    - Cookies from browser (`--cookies-from-browser`).
    - Browser-cookie fallback: if yt-dlp fails while using browser cookies because cookie extraction is denied or cookie-backed extractor state incorrectly reports public media as unavailable/ended, the worker may retry once without `--cookies-from-browser` and must surface clear diagnostics if the retry still fails.
    - Download sections (`--download-sections`).
    - Output template (`-o`), including type-specific templates that inherit the shared default when unset.
    - Max concurrent downloads (`--concurrent-fragments`).
    - Rate limit (`--limit-rate`).
    - Override archive (`--no-download-archive`).
    - **Playlist Album Unification**: When `Force Playlist as Single Album` is enabled for audio playlist downloads, the builder must inject `--parse-metadata "playlist_title:%(album)s"` and `--parse-metadata "Various Artists:%(album_artist)s"` to ensure consistent album grouping in local music players.
    - Embed chapters (`--embed-chapters`) for non-livestream media only.
    - **Section Container Preservation**: Section downloads must preserve the user's requested output container/extension (for example MP4 video clips stay MP4) instead of forcing an intermediate MKV remux. The app may add `--force-keyframes-at-cuts` for video precision, but it must not silently switch the container behind the user's back.
    - **Section Filename Labeling**: When a section/chapter clip is queued, the output filename must include a filename-safe label describing the selected range or chapter (for example `[section 15-00_to_end]`) before the extension so the saved file clearly identifies which part of the source it contains.
    - **Codec Preference Fidelity**: Advanced Settings video/audio codec choices must map to yt-dlp selectors that recognize common codec aliases reported by sites and containers (for example H.264 matching `avc1` streams, H.265 matching `hev1`/`hvc1`, and AAC matching `mp4a`), rather than only matching one literal codec token.
    - **Runtime Format Overrides**: When the runtime picker supplies a concrete `format` ID, the downloader must treat it as an explicit `-f` override instead of re-opening the picker or falling back to the saved quality/codec defaults. If video and audio runtime format IDs are selected separately, both IDs must be merged into the final video `-f` expression.
- **Livestream Replay Metadata**: Playlist expansion, runtime format metadata, queue options, and worker `info.json` parsing must preserve yt-dlp `live_status`. `post_live`, `was_live`, and `not_live` are non-live for download-argument purposes; only `is_live`, `is_upcoming`, explicit wait options, or equivalent extractor metadata should activate livestream recorder/wait behavior. Worker progress metadata may update the active row's stored `is_live` value after queue creation so livestream-specific controls remain accurate.
- **Output Parsing**: Must parse `yt-dlp` stdout/stderr for progress, final filename, and metadata JSON.
- **Completed-with-warning handling**: If yt-dlp exits with a non-zero code after producing a valid final media path, the download may continue through finalization only when the captured diagnostics indicate a recoverable post-processing/tool warning. The worker must attach an explicit exit-code warning and the manager must surface a "Completed with warnings" state instead of hiding the partial failure. Critical diagnostics such as unavailable, private, removed, or policy-blocked media must force a terminal failure even if a final path was observed.
- **yt-dlp diagnostic guidance**: Worker output parsing must retain actionable error/warning lines, classify missing FFmpeg/FFprobe and FFmpeg post-processing failures separately from unavailable-media errors, and convert optional browser-impersonation warnings into completion metadata recommending the missing Python dependency instead of failing an otherwise successful download.
- **Headless Runtime-State Isolation**: Headless/server runs must use the shared main `settings.ini` for user preferences and the `Server/` app-data subfolder for runtime state such as `downloads_backup.json`, `api_token.txt`, and timestamped log files so Discord-bot/API activity can be diagnosed separately from GUI sessions without creating a second preferences profile.
- **Discord Bridge Queue Ordering**: Webhook payloads sent to the local Discord bridge must include a `queue_position` value for queued jobs, clear it for active/terminal jobs, and refresh affected payloads when queued downloads are moved, started, paused, cancelled, completed, or removed.
- **Process Lifetime on Exit**: Closing the application must explicitly terminate active downloader/post-processor process trees (including child tools such as `aria2c` and `ffmpeg`) as well as any transient utility processes (updaters, validators, template checkers, cookie testers) so no background tasks survive after the UI exits. Windows process-tree cleanup should let `taskkill` enumerate child processes before the parent is killed, with only a short bounded wait so shutdown cannot hang indefinitely.
- **Headless Shutdown Sequence**: When exiting automatically via `--exit-after`, the application MUST explicitly flush the terminal queue state (with all completed items pruned or marked) to `downloads_backup.json` and cleanly shutdown managers *before* calling `QCoreApplication::quit()`, as this function bypasses standard `QCloseEvent` hooks.
- **Progress Compatibility**: The worker MUST understand and emit progress from **both** native `yt-dlp` progress lines **and** aria2c external downloader output, including:
  - Native yt-dlp format: `[download] XX.X% of YY.YMiB at ZZ.ZMiB/s ETA 0:00`
  - aria2c format: `[#XXXXX AA.AMiB/BB.BMiB(CC.C%)(...)ETA:0:00]`
  - Unknown or approximate totals (e.g., `~XX.XMiB`, `Unknown total size`)
  - Destination-aware native stages, including fragment downloads and auxiliary-file transfers. For HLS streams, the parser must intercept `(frag X/Y)` tags to calculate and display the true overall progress percentage and segment counts, preventing the progress bar from repeatedly snapping to 0%.
  - Primary-stream handoff detection must correctly switch the label from video to audio when multi-stream downloads restart progress on the next requested format, even if the temporary filename ends in `.part` or yt-dlp omits a fresh destination line
  - Stream labeling should prefer yt-dlp `format_id` clues from temp filenames such as `.f251.webm.part` before falling back to container extensions, since extensions like `.webm` can represent either audio-only or video streams
  - Scheduled livestream and upcoming-premiere wait output must immediately emit indeterminate progress with a human-readable status. Countdown lines should show "Next check in ..."; generic wait lines should show "Waiting for livestream to start..."; upcoming/offline prompt-like process exits should delay terminal failure while waiting for the user's response.
  - **Download Sections Dialog**: When section downloads are enabled, the application presents a dialog. This dialog must include a visible instruction indicating how users can disable the "Download Sections" prompt in the Advanced Settings tab if they no longer wish to use it.
- If `info.json` does not include `requested_downloads`, stream labeling should fall back to yt-dlp's announced format list (for example `Downloading 1 format(s): 399+251-13`) and aria2 command-line URL metadata such as `itag=251` and `mime=audio/webm` before relying on ambiguous extensions or progress-reset heuristics
- If stream order was already inferred from stderr/stdout and `info.json` later arrives without `requested_downloads`, the worker must preserve the inferred order rather than clearing it and regressing the visible stage label
  - If no trustworthy filename handoff is available yet, the worker should also consider the active stream's emitted total size as a secondary clue before falling back to simple progress-reset ordering
  - The displayed downloaded/total bytes for multi-stream jobs should remain scoped to the active stream rather than being silently converted into an aggregate whole-job byte counter
  - The UI may additionally show a small secondary overall-progress indicator for multi-stream jobs, but it must remain visually subordinate to the main active-stream progress bar
  - Auxiliary transfers (thumbnails, subtitles, `.info.json`) must not hijack the main media progress percentage/size display
  - aria2 `FILE:` lines should be used as an additional source of truth for the active transfer target so stage labels stay aligned with the actual stream being downloaded
  - Hot progress parsers should use cheap prefix/substring checks and `QStringView` parsing where possible before falling back to regex, because native yt-dlp and aria2 progress lines can arrive at very high frequency.
  - Pre-download extraction/setup stages should drive the main bar into an indeterminate state instead of leaving a stale queued `0%` display on screen
  - UTF-8 filenames and metadata
  - **The progress bar MUST update correctly regardless of which downloader (native or aria2c) is active**
- **Process Output Bounds**: Long-running process wrappers must avoid retaining unbounded stdout/stderr. Workers may keep recent diagnostic tails, but livestreams, large galleries, and mux failures must not grow memory linearly with process output.
- **Progress Bar Color Coding**: The UI progress bar MUST use color-coding to provide clear visual feedback on download state:
  - **Colorless/Default** (no custom stylesheet): When download is queued, initializing, or in indeterminate state (progress < 0)
  - **Light Blue** (`#3b82f6`): While actively downloading (0% < progress < 100%)
  - **Teal** (`#008080`): During post-processing phase (indeterminate scrolling animation with status containing "Processing", "Merging", "Finalizing", etc.)
  - **Green** (`#22c55e`): When download is fully completed (progress at 100% and post-processing finished)
  - Active main and overall progress value changes may be animated, but animations must stop when the row becomes indeterminate, cancelled, cleared, or completed so stale animations cannot overwrite terminal state.
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
    - If the user chooses partial playlist queueing, only selected expanded entries are enqueued and the original placeholder is still removed cleanly.
  - Queue state persistence (handled by `DownloadQueueManager`) MUST be deferred via `Qt::QueuedConnection` to prevent GUI thread blocking
  - Playlist-derived items must preserve enough metadata (`is_playlist`, `playlist_title`, and playlist index/context) for downstream sorting rules and finalization to treat them as playlist downloads instead of single-item fallbacks.
- **Active item actions**: Active rows must label destructive cancellation separately from livestream finalization. `Cancel` discards partial files, while `Stop & Save` is shown for livestreams and sends a graceful interrupt so the captured media can continue through normal finalization.
- **Runtime Format Selection**: When Advanced Settings `Quality` is set to `Select at Runtime` for video or audio downloads, the app must asynchronously fetch format metadata with `yt-dlp` and present a structured selection dialog. Selecting multiple formats must enqueue one download per selected format.
- **Encoding Robustness**: Worker process environment must force UTF-8 text output (`PYTHONUTF8=1`, `PYTHONIOENCODING=utf-8`) so Unicode filenames are preserved in stdout/stderr parsing.
- **Process Output Robustness**: Downloader workers must buffer raw stdout/stderr bytes, decode complete UTF-8 lines through one shared parser, and flush the final partial line through that parser when the process exits so trailing diagnostics, final paths, and progress lines are not lost or corrupted.
- **JSON Robustness**: Downloader-adjacent JSON parsing (`info.json`, aria2 RPC responses, queue/history files, GitHub release responses, and yt-dlp metadata probes) must check `QJsonParseError` and validate expected value types. Reused parsing paths should share helper logic where practical so retry parsing and process-finish parsing cannot diverge. Queue backup restore must skip non-object array entries and surface parse failures clearly instead of treating malformed data as empty success.

### 2.6. Post-Processing
- **File Lifecycle:**
    - Section clips saved in MP4-family containers may run through one additional asynchronous ffprobe+ffmpeg normalization pass before final move. The app probes the clipped container duration with `ffprobe`, then remuxes with `-fflags +genpts`, `-ignore_editlist 1`, `-fix_sub_duration`, `-c:s mov_text`, `-t <clip_duration>`, `-shortest`, and `-movflags +faststart` so embedded subtitle streams are hard-limited to the clip timeline and players like VLC read the clipped duration more accurately.
    - Un-embedded thumbnails (e.g., from `.ts` to `.mp4` livestream remuxes) are automatically detected and injected into the final container using a native FFmpeg fallback pass.
    - All downloads must go to a temporary directory first.
    - **Resuming and Clearing:** Partially downloaded files are retained in the temporary directory when a download is stopped. `DownloadQueueState` persists the current `tempFilePath`, `originalDownloadedFilePath`, and tracked cleanup candidates so users can resume or manually clean up across application restarts.
    - **Manual Cleanup:** If a user clicks `Clear Temp` for a stopped or failed download, the application must use the tracked cleanup candidate paths gathered during the worker run and then apply literal stem matching to sweep up associated media files, fragments (`.part-Frag`), tracking files (`.aria2`, `.ytdl`), metadata (`.info.json`), thumbnails, subtitles, and other partial sidecars. The row-level action must be hidden when no associated temporary files currently exist. When the tracked candidate is a per-download UUID directory, the whole directory may be removed recursively. Worker-side empty-directory cleanup must also derive the temporary directory from `<completed_downloads_directory>/temp_downloads` if `temporary_downloads_directory` is unset, and must use the same fallback path during normal finish and process-start/error failures. Worker-owned wait thumbnails must be identified by the download ID prefix before removal or relocation into managed cleanup scope. The cleanup filter must use literal string matching (`==` and `startsWith`) rather than wildcard globbing to prevent failure when video titles contain bracket characters `[]` (like YouTube IDs).
    - A file stability check must be performed before moving.
    - Files are moved to a final destination directory upon success.
    - Final file movement must be Unicode-safe and shell-independent (use Qt file APIs with copy/remove fallback for cross-volume moves).
    - Final replacement and temporary sidecar cleanup should retry briefly when file removal fails, because Windows virus scanners, thumbnailers, and recently exited child processes can hold handles for a short time after the download process exits.
- **Audio Playlist Tagging**: For audio downloads from a playlist, `ffmpeg` must be used as a post-processing step to embed the `track` number metadata into the completed file.
- **Filename Prefixing**: Audio files from playlists must have their filenames prefixed with a zero-padded track number (e.g., `01 - Title.mp3`).
- **Audio Playlist Artwork**: Audio playlist downloads should generate `folder.jpg` by default and should treat playlist metadata (`playlist_title`, `is_playlist`, or playlist index) as enough context to enable playlist artwork/tag behavior.
- **Sorting Path Sanitization**: Sorting destination tokens must replace illegal filesystem characters with safe separators and collapse repeated spaces so metadata such as titles, album names, and uploader fields remain readable while still preventing path traversal or invalid paths.

### 2.7. Updaters & Deployment
- **Application Updater**: Checks GitHub for new application releases and provides a download/install mechanism.
- **Application Update UX**: Startup update checks must wire `AppUpdater` signals to a user prompt, compare normalized semantic and date-like version tags, surface update-check failures clearly, and prefer installer assets named `LzyDownloader-Setup-*.exe`.
- **Dependency Management**:
  - Provides a UI for users to see the status of external binaries (`yt-dlp`, `ffmpeg`, etc.), manually locate them, or trigger an installation process.
  - The installation process will guide the user through automated (package manager) or manual (web download) methods.
  - Startup checks, enqueue-time checks, and Start-tab format checks must surface missing required binaries through the guided setup dialog and reuse the External Binaries install/browse logic.
  - Update commands for package-managed binaries must call the relevant package manager instead of overwriting files directly; standalone binaries should use their own updater command such as `yt-dlp -U`, `gallery-dl -U`, or `deno upgrade`.
- **Executable Name**: Must be `LzyDownloader.exe`.
- **Binaries**: The application does not bundle external binaries. It requires the user to have `ffmpeg`, `ffprobe`, `deno`, and `yt-dlp` installed. `gallery-dl` and `aria2c` are optional.
- **Qt Image Plugins**: Windows builds must deploy the Qt `imageformats` plugins required to display active-download thumbnails and converted artwork, including JPEG, PNG, WebP, and ICO support.
- **Qt TLS Runtime**: Windows builds must deploy OpenSSL runtime DLLs (`libcrypto-3-x64.dll`, `libssl-3-x64.dll`) beside the executable when Qt's OpenSSL backend requires them.
- **vcpkg Reproducibility**: Manifest-mode builds must pin a `builtin-baseline` in `vcpkg.json`; release version bumps must keep `version-string` synchronized with `CMakeLists.txt`.
- **Release Packaging Automation**: `build_release.py` is the canonical release builder. It must parse the version from `CMakeLists.txt`, refresh extractor JSON files, clean/configure/build `build-release`, package `LzyDownloader-Setup-X.X.X.exe` on Windows via NSIS, and package `LzyDownloader-X.X.X-x86_64.AppImage` on Linux via linuxdeploy. The GitHub Actions release workflow must trigger on `v*` tags and upload those platform assets to the matching GitHub Release. Qt setup must rely on the installer action's implicit Qt Base package for Widgets/Core/Network/Sql and request only add-on modules from `aqt` when the project actually links one.

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
- **Qt SDK Discovery**: CMake must honor explicit `Qt6_DIR`/`CMAKE_PREFIX_PATH` configuration and also auto-check common Windows Qt install prefixes (for example `C:\Qt\6.*\msvc2022_64` and `C:\Qt\6.*\mingw_64`) so IDE-driven configure steps can find Qt without manual edits on typical developer machines.
- **Local Debug Builds**: Developer debug presets may build into `build-debug` with a standalone Qt install instead of vcpkg. When `LZY_SKIP_QT_DEPLOY=ON`, CMake must avoid `windeployqt` and copy only the minimal Qt/MinGW runtime DLLs and plugin folders needed to launch `LzyDownloader.exe` from the build tree.
- **Database**: SQLite (via Qt SQL module)
- **Process Management**: `QProcess`

## 4. Test Requirements
- CMake must register new single-source Qt test executables through `lzy_add_test(...)`.
- The test suite must remain runnable through `ctest -C <config> --output-on-failure`.
- `run_headless_tests.py` is the canonical helper for non-interactive Windows test runs; it builds the requested configuration, sets `QT_QPA_PLATFORM=offscreen`, and runs CTest with parallel jobs based on the host CPU count.
- Current required coverage includes yt-dlp argument generation/progress parsing, archive normalization, configuration defaults/reset and legacy cleanup, Local API token/auth/enqueue behavior, process binary-resolution cache behavior, URL validation, sorting sanitization, playlist range selection, UI progress widgets, and the local end-to-end download fixture.
