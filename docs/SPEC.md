# LzyDownloader C++ Specification

This document is the canonical behavior reference. Use
`docs/ARCHITECTURE.md` for ownership/data flow, `docs/SETTINGS.md` for the
configuration schema, and `docs/API_SURFACE.md` for callable interfaces. Read
only the sections relevant to the change.

## 1. Project rules

- LzyDownloader is a Qt 6 Widgets/C++20 port using `yt-dlp`, `gallery-dl`,
  FFmpeg, and SQLite. Project-authored code/assets are GPL-3.0-or-later;
  third-party tools retain their own licenses.
- `CMakeLists.txt` and `vcpkg.json` keep synchronized description, homepage,
  and version metadata. Published artifacts include SHA-256 manifests from
  `tools/generate_release_checksums.py`; this helper is not a runtime
  dependency.
- Functional changes update affected maintained docs and user-facing release
  notes as appropriate. Do not rewrite `docs/CHANGELOG_ARCHIVE.md`.
- Keep every file below 500 lines and Markdown below 100 KB. Valid HTTP/HTTPS
  Download History URLs are escaped, keyboard-accessible links; incomplete or
  invalid values remain plain text.
- Behavior must be generic across extractors. No hostname-specific downloader,
  header, referer, format, retry, or livestream branches; use extractor
  metadata, documented tool behavior, user settings, or hostname-independent
  URL-shape hints.

## 2. Runtime and data contracts

### 2.1 Responsiveness and lifecycle

- Network, filesystem scans, long database work, and external processes run
  asynchronously/off the GUI thread. All QWidget access stays on the GUI thread.
- Download flow is: per-download temporary directory -> stable-file check ->
  completed destination. Terminal failure removes only the guarded owned UUID
  folder; stopped downloads retain partials for resume. Startup asynchronously
  removes only unprotected direct-child UUID folders after queue restoration;
  shared roots, symlinks, non-UUID folders, and stopped/failed IDs are kept.
- `downloads_backup.json` is written atomically and terminal state is flushed
  before non-interactive `QCoreApplication::quit()`. GUI and
  server/headless/background modes use shared preferences but isolate runtime
  state under `Server/`.

### 2.2 Instances, settings, and archive

- One GUI instance and one independent `--server`/`--headless`/`--background`
  instance may run. Startup clears stale shared memory before creating the
  marker and releases the startup semaphore on every path.
- Settings are Qt-native `QSettings` in the app-local `settings.ini`. Python
  `configparser` compatibility is not required. Invalid/legacy values are
  discarded and replaced with documented defaults; see `docs/SETTINGS.md`.
- `download_archive.db` remains schema-compatible with the Python version.
  SQLite connections are Qt thread-local. Queue, active, paused, retry, and
  archive checks use shared normalized media identity, not raw URL equality.
- Startup concurrency is 4; users may increase it to 8 during a session.
  Playlist audio filenames use zero-padded indices by default and can be
  disabled with `DownloadOptions/prefix_playlist_indices`.

### 2.3 Queue, retry, and replacement

- Queue rows are emitted immediately. Video/audio rows start as
  `Checking for playlist...`; gallery rows start as `Queued`; single items
  become `Queued` and playlists replace the placeholder with their entries.
  Queue persistence is deferred with `Qt::QueuedConnection`.
- Retry/resume compares the candidate with the current active snapshot before
  enqueueing. `override_archive=true` may replace only a matching restored
  stopped/failed entry; genuinely paused entries remain protected. Discord/API
  rejection paths preserve the caller job ID and terminal diagnostic.
- Intentional re-download replacement is centralized in
  `FileReplacement::moveReplacing()`: retain the old destination until a
  verified new output moves/copies successfully, and restore it on failure.
- Power inhibition prevents idle sleep, not display power-off, while any GUI or
  server/headless/background download is active. Release it on completion,
  cancellation, and shutdown; unsupported platform services are best-effort.

## 3. User interface

- The Qt Widgets UI uses in-app tabs/buttons/sidebars, not a native top-level
  menu bar. The Start tab provides URL/type controls, Supported Sites, playlist
  handling, concurrency, global rate limit, duplicate override, and runtime
  format selection through Advanced Settings. `playlist_logic=Ask` supports all,
  selected one-based ranges, first item, or cancel.
- Active Downloads provides stop/resume/clear/folder actions and one row per ID.
  Rows shrink to the viewport, wrap long titles, keep actions visible, and
  disable horizontal scrolling. Queued thumbnail URLs start bounded async
  requests immediately; playlist transitions preserve them.
- Every row has one detailed `ProgressLabelBar` for the active transfer or
  processing stage. Use default palette styling for queued/indeterminate,
  light blue for transfer, teal for processing, and green for completed. Paint
  centered percentage, sizes, speed, and ETA. Aggregate fields may remain for
  Discord but are not rendered as a second desktop bar.
- Status text comes from worker/finalizer stages, including extraction,
  segments, merging, verification, sorting, destination movement, embedding,
  and livestream wait countdowns. Controls and descriptive labels have
  tooltips; light/dark themes use palette-aware colors.
- The footer keeps links, counters, and current speed on its first row, with
  exit-after-downloads rightmost. Closing the window exits rather than hiding
  to the tray.

## 4. Download engine

### 4.1 Probing, arguments, and tools

- `yt-dlp` handles video/audio and `gallery-dl` handles galleries through
  asynchronous `QProcess`. Metadata-only playlist probes are read-only: they
  omit download forcing, cookies, temp-directory creation, and item limits.
  Transient probe/JSON errors fall back to the normal worker for ordinary URLs;
  explicit playlist-shaped URLs and missing yt-dlp remain terminal failures.
- Generic positive item-index hints (`img_index`, `slide`, `item`, `index`,
  `playlist_index`) are stripped for probing and applied as one-based
  `--playlist-items` only to the real download. Preserve thumbnails, playlist
  metadata, and `live_status` while replacing placeholders.
- Resolve binaries through the shared resolver: explicit overrides, selected
  app-managed/system preference, app/user paths, `PATH`, and package candidates.
  Required tools are `yt-dlp`, `ffmpeg`, `ffprobe`, and `deno`; `gallery-dl`
  and `aria2c` are optional. Invalid/missing tools produce actionable terminal
  errors, not a stuck download.
- Build arguments from settings for formats/codecs, templates, subtitles,
  chapters, SponsorBlock, sections, metadata/thumbnails, cookies, rate limits,
  JS runtime, and archive behavior. Use logical binary names; only resolver
  code adds Windows `.exe`. Preserve explicit runtime format IDs and requested
  section containers/filename labels.
- Aria2c is for ordinary non-livestream transfers. Use bounded retry/backoff,
  conservative per-server connections, and a referer only when the URL has a
  scheme and host. For exit codes 2, 5, 6, or 29, or the narrowly classified
  expected-media `.part` file-not-found diagnostic, retry once with native
  yt-dlp after removing stale `.info.json` sidecars; preserve media partials.
- Browser-cookie extraction and livestream-wait failures may each use one
  generic fallback retry only with explicit cookie/browser/sign-in or
  pre-wait evidence. Words such as `locked`, URL text, and ambiguous titles are
  not evidence. `live_status`/`is_live` select livestream behavior:
  `post_live`/`was_live`/`not_live` are completed replays and stay non-live.

### 4.2 Progress and diagnostics

- Parse native yt-dlp, HLS `(frag X/Y)`, `--progress-template`, aria2c, and
  livestream indeterminate output. Emit speed, ETA, downloaded/total sizes,
  status, and active-stream labels. Prefer format IDs, announced formats,
  `FILE:`/URL metadata, and matching `formats` sizes over ambiguous extensions.
- If `info.json` lacks `requested_downloads`, recover the active stream total
  from matching formats and bounded polling of its owned `.part` file. Do not
  invent totals for auxiliary files or unknown-size livestreams; auxiliary
  thumbnails/subtitles/metadata must not replace main-media progress.
- Buffer process bytes until complete UTF-8 lines and retain a bounded
  diagnostic tail. A final path is not proof of valid media. Missing fragments,
  empty data blocks, invalid headers/containers, invalid-input FFmpeg errors,
  critical unavailable/private/removed/policy diagnostics, `No space left on
  device`, errno/ENOSPC 28, and FFmpeg `-28` are terminal before metadata
  embedding. Recoverable non-zero exits retain a visible “Completed with
  warnings” state; optional impersonation warnings are recommendations.
- Low-quality warnings apply only to video jobs below 480p, include the title,
  and render a complete HTTP/HTTPS source URL as an escaped link. Audio jobs
  remain audio-oriented even when a combined transport reports `video/*` or
  retains `height` metadata.

## 5. Post-processing and metadata

- Preserve metadata, thumbnails, subtitles, chapters, and audio playlist tags.
  Track-level `artist` wins; fallback only to item `artists`, `creator`,
  `channel`, or `uploader`, never `playlist_uploader`/`playlist_owner`.
  Playlist audio prefixes indices by default and generates `folder.jpg` only
  for explicit full batches, not single or partial selections.
- If yt-dlp leaves a tracked thumbnail sidecar, the existing FFmpeg rewrite
  adds it as a second input mapped as `attached_pic` before cleanup. Missing
  sidecars do not block ordinary metadata embedding.
- SponsorBlock and section cuts normalize audio timestamps rather than copying
  packets from the pre-cut timeline. Bound FFmpeg resources and use background
  priority where supported. Verify stability before final move; use Qt file APIs,
  with copy/remove fallback across filesystem boundaries and short lock retries.
- Manual `Clear Temp` uses tracked candidates and literal stem matching for
  media, fragments, `.aria2`/`.ytdl`, metadata, thumbnails, subtitles, and
  other sidecars. Hide the action when no owned temp files exist.

## 6. API, updates, and deployment

- The Local API binds only to `127.0.0.1:8765`, requires a Bearer token, bounds
  payloads, validates Host/Origin, and grants CORS only to localhost/trusted
  extension origins. `POST /enqueue` accepts URL, type, optional ID, and
  explicit `override_archive`; `GET /status` returns snapshots; authenticated
  `POST /cancel` accepts `job_id` (or `id`) and routes through the manager.
  Direct/API requests and `--background`/`--server`/`--headless` launches are
  non-interactive. Validation, duplicate, missing-binary, runtime, and
  terminal failures emit the `nonInteractiveRequestFailed` signal for bridge
  consumers instead of opening modal dialogs. Webhooks use the main-thread
  network manager, bounded
  requests, sanitized status, queue positions, parent IDs, aggregate
  `overall_progress` when available, and observable terminal
  completion/cancellation states.
- Browser-companion enqueue requests may carry a validated, request-scoped
  cookie-file path and client identity. The path is passed only to the worker
  as `--cookies`, is excluded from queue backups and API status payloads, and
  is removed on terminal/removal cleanup; status and cancellation are scoped
  to the client identity. Native messages are length-prefixed and capped at
  1 MiB; cookie bundles are capped at 500 entries/900 KiB and each cookie must
  match the requested host and path scope.
- `AppUpdater` checks HTTPS releases asynchronously, validates JSON/assets,
  matches OS and macOS CPU architecture, and opens macOS DMGs through Finder.
  Before install launch, save resumable state and terminate child processes.
  Silent Windows `/S` installs relaunch the installed `LzyDownloader.exe`.
- Runtime binaries are not required to be bundled: users may configure or
  install them through External Binaries. Mark **(Recommended)** only install
  options that write to the platform app-data `bin` folder; system/package
  manager alternatives remain explicit and are detected without relocation.
  App-managed copies can update by cadence; detected existing tools are updated
  in their current locations before a new app-managed copy is considered.
  WinGet paths use `winget upgrade`; standalone FFmpeg and aria2c are never
  silently replaced. Standalone yt-dlp, gallery-dl, and Deno use their own
  updater when supported. Startup consolidates missing-tool and available-update
  notices into one **Set Up Required Tools** checklist. It labels each row as a
  new installation or existing-binary upgrade and offers **Update All** for the
  supported automatic actions; manual-only updates remain visible and actionable
  in that same checklist. Startup completion must not depend on a successful
  binary version probe: bundled extractor metadata is initialized after every
  terminal updater result, including missing-tool and probe-failure results, so
  the checklist can be shown and the Start tab can recover from its waiting state.
  Windows FFmpeg/FFprobe replacements stage beside the destination and retry
  transient locks while preserving the old executable on failure.
- Windows deployment includes required Qt image plugins, SQLite, OpenSSL, and
  Qt runtime DLLs. MinGW builds also deploy the compiler runtime DLLs needed
  outside the developer shell. CMake presets, the shared deployment helper,
  and vcpkg keep builds
  reproducible; no new runtime dependency may be introduced without approval.

## 7. Release and test requirements

- `build_release.py --target auto|windows|linux|macos` is native-only. It
  refreshes extractor data, checks semantic CMake/tag versions, rejects reused
  versions unless `LZY_ALLOW_VERSION_REBUILD=1`, and packages the platform.
  CI publishes only for matching `v*` tags; manual dispatch validates without
  publishing. Windows and Linux use the pinned prebuilt Qt 6.10.2 SDK and build
  only the application target in parallel. Linux selects qmake from that SDK,
  deploys dynamic Qt/SQLite with linuxdeploy, caches linuxdeploy under
  `build-release/tooling/`, and emits the AppImage under `build-release/`.
  macOS emits x86_64 and arm64 DMGs.
- Release CI no longer resolves or compiles Qt through vcpkg. Local/source
  builds may still use the pinned vcpkg manifest and optional triplets. CI
  validates with `python -m pip install --pre --upgrade yt-dlp`; these tools are
  not bundled runtime dependencies. Windows CI installs NSIS; child commands
  retain the invoking terminal. The final executable is `LzyDownloader.exe`.
- Register tests with `lzy_add_test(...)`; keep them isolated from user files
  and use `QT_QPA_PLATFORM=minimal`. Required coverage includes argument
  builders, playlist/probe fallback, progress and recovery boundaries,
  persistence/cleanup, archive identity, API/power/binary behavior, sorting,
  UI row layout, and the end-to-end fixture.
- `tests/run_headless_tests.py` builds before CTest and stops on build failure;
  it timestamps output, reports pass/fail/not-run totals, keeps CTest exit-code
  failures failed, and stores failed names for `--suspects`. Visual Studio
  vcpkg-toolchain caches enable manifest mode; direct-Qt caches disable MSBuild
  integration. Windows test deployment includes `qminimal.dll` and the Qt
  runtime DLLs required by deployed test executables.

## 8. Logging and stack

- Each run creates a timestamped log in the user data directory and startup
  retains only the five newest files. Never log tokens, cookies, credentials,
  or credential-bearing URLs.
- Stack: C++20, Qt 6 Widgets/Core/Network/Sql (D-Bus on Linux for power
  inhibition), CMake, SQLite via Qt SQL, and asynchronous `QProcess`.
