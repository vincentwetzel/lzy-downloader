# LzyDownloader C++ Port TODO

## In Progress

### Phase 19: Comprehensive Testing Suite
- [x] **Step 1: Static Library Architecture (The Foundation)**
  - [x] Refactor `CMakeLists.txt` to gather all `src/core/`, `src/utils/`, and `src/ui/` files into a new `STATIC` library target (e.g., `LzyAppLib`).
  - [x] Update the main executable (`LzyDownloader.exe`) to only contain `main.cpp` and link against `LzyAppLib`.
  - [x] Ensure the test executable (`LzyDownloaderTests`) links to `LzyAppLib` so it can cleanly instantiate application classes.
  - [x] Verify CMake's `AUTOMOC` properly generates moc files for the static library to support Qt signals/slots in tests.
- [x] **Step 2: Test Environment Isolation (Mocking & Safety)**
  - [x] Modify `ConfigManager` to allow injecting a temporary settings file path (e.g., an in-memory `.ini` or a temp dir) during tests to protect the user's real `settings.ini`.
  - [x] Modify `ArchiveManager` to support an in-memory SQLite database (`:memory:`) or a temporary file path for tests to protect the real `download_archive.db`.
  - [x] Create a base test utility or setup/teardown methods to automatically provision and clean up these isolated environments for every test run.
- [x] **Step 3: Unit Tests (Fast & Frequent)**
  - [x] **Logic Tests:** Write tests for `YtDlpArgsBuilder`. Inject mock `ConfigManager` states and verify the generated `QStringList` matches the expected command-line flags (e.g., SponsorBlock, restrict filenames).
  - [x] **Signal/Parsing Tests:** Write tests for `YtDlpWorker`'s progress parsing using `QSignalSpy`. Feed it raw `stderr` strings from both native `yt-dlp` and `aria2c`, and assert that the emitted progress, speed, ETA, and status signals are perfectly accurate.
  - [x] **Sorting Tests:** Write tests for `SortingManager` to ensure custom tokens (`{album}`, `{uploader}`) are correctly evaluated against mock `info.json` metadata and illegal characters are sanitized.
  - [x] **URL Normalization Tests:** Write tests for `ArchiveManager` to verify that various URL formats normalize to identical database keys to prevent duplicate downloads.
- [x] **Step 4: Integration & GUI Tests (Slow & Thorough)**
  - [x] **Headless GUI Tests:** Instantiate UI widgets like `DownloadItemWidget` and `ProgressLabelBar` in memory. Emit simulated 100% completion signals and assert that the progress bar fills up and changes its stylesheet color to green (`#22c55e`).
  - [x] **End-to-End Tests:** Write a dedicated integration test that feeds a tiny, reliable 5-second test video URL into `DownloadManager`, allows it to launch the real `yt-dlp` process, and asserts that the final media file appears in the temporary test output folder.

### Phase 18: Local API Server & Discord Bridge
- [x] **Local API Server implementation**:
  - [x] Create `LocalApiServer` class using `QTcpServer` for zero-dependency local networking.
  - [x] Implement API key generation and storage (`api_token.txt`).
  - [x] Add `POST /enqueue` endpoint to accept links safely.
  - [x] Add `GET /status` endpoint to return active queue progress, speed, and ETA.
  - [x] Add GUI Toggle in Advanced Settings (`ConfigurationPage`) to "Enable Local API Server".
  - [x] **Headless Environment Isolation**: Branch single-instance memory locks, share the main app-local `settings.ini` for preferences, and route runtime files (`downloads_backup.json`, `api_token.txt`, logs) to a `Server` subfolder when running with `--headless` or `--server`, allowing a bot and the GUI to run concurrently without creating a second preferences profile.
  - [x] **Headless logging isolation**: Route headless/server log files to the same `Server` app-data subfolder and log the actual `settings.ini` path used by `ConfigManager`.
  - [x] Wire server signals to `DownloadQueueManager` and `MainWindow`.
  - [x] Handle graceful connection loss when "Exit after all downloads complete" is enabled by checking the final backup state.
  - [x] **Discord Bridge Webhook**: Emits HTTP POST payloads to a local webhook port whenever a download job's progress, status, speed, or ETA changes to keep the Python bot fully in sync.

### Phase 14: Unbundled Binaries & Dependency Management
- [x] **Project Cleanup**:
  - [x] Remove the `bin/` directory and all bundled executables from the source repository. **COMPLETED**: The `bin/` directory was already not tracked in git. Removed fallback code from `ProcessUtils::resolveBinary()` that checked for bundled binaries.
  - [x] Update `CMakeLists.txt` and deployment scripts (`UPDATE_AND_RELEASE.md`) to reflect the unbundled approach. **COMPLETED**: Updated `SETTINGS.md`, `README.md`, and `AGENTS.md` to remove references to `bin/` directory. Binary resolution now only checks: (1) User-configured path, (2) System PATH, (3) User-local install locations.
  - [x] Remove binary update scripts (e.g., `update_binaries.ps1`). **COMPLETED**: Deleted `update_binaries.ps1` from repository.

### Error Handling
- [x] **Private Videos**: Display an error popup window explaining to the user that the video is private (e.g., `https://www.youtube.com/watch?v=V47ahjtbcaA`). **IMPLEMENTED**: YtDlpWorker now parses stderr for "private" errors and emits `ytDlpErrorDetected` signal, which triggers a user-friendly QMessageBox popup via DownloadManager and MainWindow.
- [x] **Unavailable Videos**: Display an error popup window explaining to the user that the video is unavailable (e.g., `https://www.youtube.com/watch?v=ZFVk4kK6fKs`). **IMPLEMENTED**: Same infrastructure as private videos, also detects "unavailable", "no longer available", and "does not exist" error patterns. Extended to also detect geo-restricted, members-only, and age-restricted videos.

### User Prompts & Validation
- [x] **Supported Sites UI**: Create a dialog that lists all supported domains, combining `extractors_yt-dlp.json` and `extractors_gallery-dl.json` into a searchable list indicating support for Video/Audio and/or Image Galleries. *(Implemented as a dedicated button on the Start tab to comply with the no-application-menu rule).*
- [x] **Livestream Implementation**: 
  - [x] **Detection**: Check `info.json` metadata to determine if `is_live` is true before queuing.
  - [x] **Livestream Settings UI**: Add a "Livestream Settings" section beneath "Audio Settings" in the Advanced Settings tab.
    - [x] `Record from beginning`: Toggle for `--live-from-start` vs `--no-live-from-start`.
    - [x] `Wait for video`: Toggle and min/max spinboxes to construct `--wait-for-video MIN-MAX`.
    - [x] `Download As`: Format enforcement (MPEG-TS or MKV), preferring MPEG-TS (`--hls-use-mpegts`).
    - [x] `Use .part files`: Toggle for the `--part` and `--no-part` flags.
    - [x] `Quality`: Dropdown specifically targeting livestream resolutions.
    - [x] `Convert To`: Video codec extensions linked to FFmpeg post-processing options.
  - [x] **Argument Builder**: Update `YtDlpArgsBuilder` to route to Livestream parameters when the flag is detected.
  - [x] **Live Chat**: Download live chat transcripts as a subtitle file.
  - [x] **Interactive Prompt**: When a scheduled livestream is detected, prompt the user to wait for it.
  - [x] **Worker Resilience**: Parse livestream indeterminate progress output and handle "Waiting for video" lines.

### Bug Fixes
- [x] **Abandoned thumbnail remuxing**: Fixed livestream TS->MP4 remuxing abandoning the thumbnail by adding a native C++ FFmpeg fallback to embed abandoned thumbnails and explicitly sweeping leftover sidecar files in the finalizer.
- [x] **Single-item playlist prompt**: Fixed 1-item playlists prompting the user to "Download All" vs "Download Single Item" by automatically bypassing the prompt.
- [x] **Mass-stop GUI freeze**: Fixed severe GUI locking when stopping multiple downloads at once (like "Stop All"). Process-tree cleanup (`taskkill`) is now offloaded to a detached background process, and queue evaluation is safely deferred to the end of the event loop.
- [x] **SponsorBlock A/V desync**: SponsorBlock video cuts now use `--force-keyframes-at-cuts` so the remaining video and audio streams do not drift out of sync when a cut falls between keyframes.
- [x] **SponsorBlock hardware cut speed**: Preflight SponsorBlock segment availability before starting yt-dlp so no-segment videos skip the expensive accurate-cut encoder path, and tune built-in hardware presets for faster single-pass encoding (`NVENC p1`, Quick Sync `veryfast`, AMF `speed`) when cuts are actually needed.
- [x] **yt-dlp user-config isolation**: Added `--ignore-config` to app-built yt-dlp commands so user-level yt-dlp config files cannot unexpectedly change output paths or post-processing during GUI/headless downloads.
- [x] **Discord bot exit-after handling**: Fixed false "Connection lost" errors in the Discord bot when LzyDownloader successfully closes due to the "Exit after all downloads complete" setting.
- [x] **Discord webhook silent failures**: Fixed webhook POST requests silently failing or leaking memory by migrating `QNetworkAccessManager` instantiation to the main GUI thread, guaranteeing an active Qt event loop for background network execution.
- [x] **Premature completion notification**: Fixed a bug where a "Downloads Complete" desktop notification fired prematurely when enqueueing the first item due to config-save triggers racing with the queue.
- [x] **Pause-triggered completion**: Fixed a bug where pausing the last active download incorrectly triggered the queue completion and auto-exit sequence.
- [x] **No pip installs in External Binaries GUI**: Removed `pip` as an in-app install method for `yt-dlp` and `gallery-dl`, keeping Python-managed installs as an advanced user path the app can detect but no longer provisions. Windows install choices now lead with standalone executable downloads to reduce Python runtime issues for standard users.
- [x] **Guided missing-binary setup dialog**: Replaced missing-binary dead-end prompts with a welcome-style checklist dialog that lets users install or browse for required tools and refresh detection in place.
- [x] **Source-tree naming/layout cleanup**: Moved misplaced Start tab helper files into `src/ui/start_tab/`, moved the aria2 download pipeline into `src/core/download_pipeline/`, removed dead top-level/duplicate naming artifacts, and updated docs/build references so the codebase layout is clearer.
- [x] **FFmpeg version string display**: Cleaned up the version string extraction for FFmpeg and ffprobe so the UI displays a concise version number or build date instead of the verbose build configuration string.
- [x] **External Binaries version/update consolidation**: Moved yt-dlp/gallery-dl version display and update actions into the External Binaries page, added per-binary version probing, and stopped the in-app updater from overwriting package-managed installs.
- [x] **External Binaries inline descriptions**: Added brief explanations next to each binary in the Advanced Settings page so users understand what tools like `deno`, `ffprobe`, or `aria2c` actually do.
- [x] **Windows debug-console toggle**: Added a runtime `Show Debug Console` setting that only appears when the app owns its console window, letting release builds stay `WIN32` while still supporting opt-in debugging.
- [x] **App update repo fallback**: Hardened release checks to try the lowercase `lzy-downloader` GitHub repo first and then fall back to legacy repository API URLs so repo renames do not silently break update discovery.
- [x] **Single-Item Playlist Double Download**: The URL `https://music.youtube.com/watch?v=Bkh2BJ49DmQ&list=OLAK5uy_n3IQt8nfMSp3Xlma2hMsvKAyHBmBwk5Is` (a playlist with 1 item) downloads the audio file twice and sorts it to two different places. It should only download once and correctly trigger the "Audio Playlist Downloads" rule instead of the single "Audio Downloads" rule.
- [x] **1-Item Playlist JSON Cleanup**: Fixed an issue where 1-item playlists left behind orphaned `info.json` files in the temporary directory by ensuring cleanup runs regardless of whether `playlist_index` is valid.
- [x] **Single-item queue stalling**: Fixed single videos and 1-item playlists getting stuck permanently in the "Queued" state after UI placeholder replacement by ensuring the download queue is properly signaled to start.
- [x] **Section clip remux regression**: Fixed section downloads forcing an intermediate MKV remux plus extra FFmpeg merger args, which could leave MP4 clips with the original full-length duration metadata and glitchy playback near the real clip end.
- [x] **Section filename labeling**: Added filename-safe section/chapter labels to clipped downloads so the saved file name reflects which part of the source video it contains.
- [x] **Section clip container normalization**: Fixed section clips in VLC by adding an asynchronous ffprobe+ffmpeg normalization pass that probes the clipped duration and hard-limits embedded subtitle streams to the clip timeline before finalization. Confirmed with ffprobe/VLC after the hard `-t <clip_duration>` remux step.
- [x] **yt-dlp native progress stage/size parsing**: Fixed downloads jumping to 100% on auxiliary transfers by ignoring thumbnail/subtitle/info-json progress for the main bar, widening native progress parsing for fragment-style lines, and surfacing destination-aware download stages.
- [x] **Download stage label audit**: Removed UI-side phase guessing, promoted worker/finalizer lifecycle text to the source of truth, and added explicit extracting/resuming/segment-transfer stage updates so labels track the real pipeline.
- [x] **Audio-stage handoff detection**: Fixed video -> audio stage switching by classifying full temp target paths (including `.part` names) and falling back to `requested_downloads` metadata when yt-dlp restarts progress on the next primary stream without a fresh destination line.
- [x] **Per-stream byte display preserved**: Kept video and audio byte counters separate so the active stream restarts from its own emitted size instead of inheriting an aggregated overall total.
- [x] **Small overall job progress bar**: Added a slim secondary overall-progress indicator for multi-stream downloads while preserving the main bar as the active-stream progress display.
- [x] **Progress parser regression hardening**: Fixed the follow-up regression where the UI could remain visually stuck at 0% by making extraction stages indeterminate again, broadening aria2 progress matching, and consuming aria2 `FILE:` lines to keep stream-target tracking in sync with active transfers.
- [x] **App-exit process cleanup**: Closing the app now runs an explicit shutdown path that terminates active descendant process trees so `yt-dlp`-spawned `aria2c`, metadata-normalization `ffmpeg`, and other helper processes do not survive after exit. This includes a catch-all sweep for transient utility processes like auto-updaters, cookie checkers, and URL validators.
- [x] **Dynamic Max Concurrent and Start Tab persistence**: Wired operational controls on the Start Tab (Max Concurrent, Playlist Logic, Rate Limit) to save instantly, properly registered them in defaults so they persist across application restarts, and enforced a startup concurrency cap of 4 to prevent aggressive spam.
- [x] **Deferred first-run directory setup**: Moved the initial completed-downloads directory prompt onto a post-show timer so the main window paints first and startup no longer blocks on an early modal dialog.
- [x] **Audio-only WebM label detection**: Fixed stream-stage labeling for temp files like `.f251.webm.part` by matching yt-dlp `format_id` values from `requested_downloads` before falling back to ambiguous container extensions.
- [x] **Audio-stage size fallback**: Added a second-stage matcher that uses the active stream's emitted total size when yt-dlp delays or omits a fresh target filename during the video-to-audio handoff.
- [x] **Empty `requested_downloads` stage fallback**: Fixed runs where `info.json` omits `requested_downloads` by seeding stream order from yt-dlp's announced format list and aria2 command-line `itag`/`mime` values, so audio-only transfers like `f251-13.webm.part` no longer stay labeled as video.
- [x] **Clear Temp Files bracket bug**: Fixed "Clear Temp Files" failing to delete leftover `.part` files when the video title contains brackets (like `[youtube_id]`) by bypassing Qt's wildcard globbing and using literal string matching.
- [x] **Manual regression: portrait audio thumbnail crop**: Verify an audio download with a portrait thumbnail and `crop_artwork_to_square=true` finalizes successfully with the safe square crop expression.
- [x] **Manual regression: bracketed filenames**: Verify a download whose title contains `[` and `]` finalizes and cleans temp sidecars without invalid `QRegularExpression` warnings.
- [x] **Manual regression: yt-dlp thumbnail/postprocessor warning**: Verify a download that emits `LZY_FINAL_PATH` and then fails thumbnail conversion still moves the media to the completed directory, cleans temp sidecars, and reports a completion warning instead of becoming a stranded failed job.
- [x] **Clear Temp Files state persistence**: Saved active download file paths to `downloads_backup.json` so "Clear Temp Files" correctly sweeps multi-stream fragments (even stripping format IDs like `.f299`) after the application is restarted.
- [x] **Late info.json label overwrite**: Fixed a follow-up regression where a delayed `info.json` parse could clear the already-correct stderr-derived stream mapping and flip the GUI back from audio to video mid-handoff.
- [x] **Advanced Settings codec/format wiring audit**: Fixed the downloader path so saved codec labels map to real yt-dlp codec aliases (`avc1`, `hev1`, `mp4a`, etc.), direct runtime `format` selections become concrete `-f` overrides, runtime-selected audio tracks are merged into video downloads, and the `Restrict filenames` toggle now reaches yt-dlp instead of being ignored.
- [x] **Rich yt-dlp error popups**: Error dialogs now preserve the active download URL and render it as a clickable link in the popup so users can open the failing source page directly from the warning.
- [x] **Immediate action feedback**: Added immediate UI feedback ("Cancelling...", "Pausing...") to download items when clicking Cancel or Pause so the UI doesn't feel unresponsive while waiting for the background process to acknowledge the state change.
- [x] **View Formats clarity**: Renamed the "View Formats" download type to "View Video/Audio Formats" and added tooltips to clarify it does not work for gallery-dl URLs.
- [x] **Sorting UI to Backend translation**: Fixed mismatch between human-readable sorting rule labels and internal lowercase keys.
- [x] **Playlist sorting tags**: Fixed playlist downloads falling back to standard video/audio rules.
- [x] **Resumed download info.json deletion**: Fixed an issue where resumed downloads wiped out `info.json`, causing sorting metadata to fall back to `Unknown`.
- [x] **Exit-after startup bug**: Fixed application instantly closing on launch if the queue was empty and "Exit after" was enabled.
- [x] **Silent failure on restricted/scheduled videos**: Fixed an issue where private videos, geo-restricted videos, and scheduled livestreams would fail silently during the `PlaylistExpander` phase before reaching the main queue.
- [x] **Livestream pre-wait thumbnails**: Fixed thumbnail fetching during livestream `[wait]` states failing due to unhandled HTTP redirects or unexpected `.webp` formatting.

## Completed

### Phase 17: Download Sections
- [x] **Download Sections Support**: Added an option to download specific sections of a video (by time or chapter). When enabled in Advanced Settings, a dialog appears before downloading, allowing you to define multiple sections. Supports time ranges (e.g., `HH:MM:SS-HH:MM:SS`) and chapter names.

### Core Features (Phases 1-10)
- UI, download logic, settings, archive, playlists, sorting, advanced settings
- Global download speed indicator, video quality warning, clean display title
- Retry/resume signals, single instance enforcement

### Stability & Deployment (Phases 11-12)
- NSIS installer, UI enhancements, advanced settings reorganization
- Restore defaults safety, cancel downloads in queue, download stats display
- Qt plugin deployment reliability, thumbnail plugin deployment
- Unicode filename move reliability, canonical `settings.ini` cleanup
- Extractor domain list maintenance, thumbnail previews, Qt SDK auto-discovery

### Future Enhancements (Phase 13)
- Cancel/retry buttons, interface languages, runtime format selection
- Backup download queue, clear inactive downloads, cancel all downloads
- Pause/resume downloads, download queue ordering
- Subtitle selection improvements
- Settings storage location, split output templates
- Expand output template fields, `--split-chapters` support, geo-verification proxy

### Settings Architecture (Phase 16)
- Simplified `ConfigManager`, removed canonicalization loops and legacy fallbacks
- Streamlined `get()`/`set()`, preserved factory defaults and safe reset

### Binary Discovery (Phase 14 partial)
- `BinaryFinder` utility, binary path caching, gallery-dl PATH detection
- Dynamic UI adaptation, external binaries settings tab
- Flexible progress parsing (native yt-dlp + aria2c), optional dependencies

### UI/UX Enhancements (Phase 15)
- Toggle switch visual fix, smart URL download type detection
- **Force Playlist as Single Album**: Added a toggle to force audio playlists to be treated as a single album with uniform metadata.
- gallery-dl output template fixes, progress bar updates, move logic fixes
- **Metadata Options**: Added options to crop audio thumbnails to a square and to generate `folder.jpg` for audio playlists.
- **Clear Inactive Downloads**: Consolidated the redundant "Clear Completed" button into a single "Clear Inactive" button that clears all finished, stopped, and failed downloads.
- **Resume All toolbar action**: Added a bulk resume control on the Active Downloads tab for stopped and failed items.
- **Open folder buttons on Active Downloads tab**: **IMPLEMENTED**: Added "Open Temporary Folder" and "Open Downloads Folder" buttons to the Active Downloads tab toolbar, duplicating the functionality from the Start tab for easier access during downloads.
- **External Downloader dropdown in Advanced Settings**: **IMPLEMENTED**: Changed "Download Options -> External Downloader (aria2)" from a ToggleSwitch to a QComboBox with two options: "yt-dlp (default)" and "aria2c". The setting is automatically hidden if aria2c is not installed/discovered. Default changed to yt-dlp for consistency with unbundled binary model.
