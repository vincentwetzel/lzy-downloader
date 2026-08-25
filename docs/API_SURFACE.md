# LzyDownloader API Surface

This document defines the public API boundaries and interface contracts for the primary components of the **LzyDownloader** C++ codebase. It serves as a guide for developer integration, interface compliance, and test authoring.

## Placement & Design Philosophy
Documentation outlining system specifications, interfaces, and architecture standards is placed in the `/docs` directory. This isolates technical documentation from source code while keeping it accessible within the codebase.

The API surface adheres to **Qt best practices**:
- Use of the pointer-to-member-function syntax for all `connect()` calls to ensure compile-time checkability.
- No blocking operations on the main thread; asynchronous logic via `QProcess` or workers.
- Proper thread-affinity and memory safety (e.g., using `deleteLater()` on workers).

Qt test authorship is kept separate from the production API: test sources and
fixtures live in the top-level `tests/` directory and link against the
`LzyAppLib` target through CMake. `lzy_add_test(...)` is the single registration
path; it adds the shared `BaseTest.cpp`, enables the offscreen Qt platform, and
links the Qt Concurrent module needed by asynchronous test coverage.

The canonical headless runner builds before invoking CTest, timestamps live
output, reports final totals, and persists failed test names for `--suspects`
reruns in the selected build directory.

Release automation is a CI integration contract rather than a runtime API:
`v*` tags build Windows, Linux, and separate Intel/Apple-Silicon macOS artifacts, Linux CI provisions the vcpkg
Qt/XCB development prerequisites before configuration, and validation installs
yt-dlp from the prerelease channel. These dependencies are not bundled into
the application or written to user settings. Linux packaging also binds
linuxdeploy to the vcpkg Qt installation that built the executable and
generates a minimal release body when a tag has no checked-in notes file.
macOS CI uses its hosted Qt SDK and `macdeployqt` to create architecture-labelled
DMGs; the updater only accepts the DMG matching the current CPU architecture and
opens it through Finder. Manual workflow dispatches build the matrix without
publishing release assets. Linux packaging detects static versus dynamic Qt:
static Qt builds skip the Qt linuxdeploy plugin, while dynamic builds deploy the
Qt and SQLite runtime plugins.

Published release jobs also generate a platform-specific `SHA256SUMS-*.txt`
manifest for the packaged installer, AppImage, or DMG. This is a distribution
artifact and is not consumed by the runtime API. Project-authored source and
assets are licensed under GPL-3.0-or-later; external tools retain their own
licenses.

---

## 1. Core Classes

### Current behavioral contracts

- `MainWindowUiBuilder` places footer download counters and current speed on the first row and keeps the exit-after-downloads control as its rightmost item.
- `MainWindow::nonInteractiveRequestFailed(jobId, url, error)` reports non-interactive validation, runtime-extraction, and missing-binary failures to the Discord webhook bridge before the rejected request is discarded.
- `DownloadManager::nonInteractiveRequestFailed(jobId, url, error)` forwards duplicate rejection diagnostics from the non-interactive enqueue path to `MainWindow` and the Discord webhook bridge.
- `ActiveDownloadsTab` and `DownloadItemWidget` keep their scroll content and
  flexible text columns shrinkable to the viewport. Horizontal scrolling is
  disabled, so Cancel/Retry/folder actions remain reachable at narrow widths.
- `BinariesPage` installs standalone Windows FFmpeg and FFprobe files through a
  staged destination-side file and bounded replacement retries. A persistent
  sharing violation leaves the existing executable in place and reports the
  failure.
- `BinariesPage::installRecommendedBinary(const QString&)` runs the preferred
  unattended first-launch install path. `tryAutomaticUpdate(const QString&)`
  only updates copies inside the managed application-data `bin` folder.
- `InitialBinarySetupDialog` records a fresh install's system-first or
  app-managed-first preference and selects gallery-dl and aria2c by default.
- `YtDlpArgsBuilder` adds the item-level artist fallback expression only for
  audio playlist downloads with metadata embedding enabled. Playlist-level
  ownership fields are intentionally excluded, and metadata-only expansion
  remains unaffected.
- `MetadataEmbedder::setThumbnailPath(const QString&)` supplies the existing
  abandoned-thumbnail remux path with a local sidecar. The setter trims the
  path; the rewrite adds a second FFmpeg input only when that file exists,
  maps it as an `attached_pic` stream, and leaves the normal metadata rewrite
  unchanged when no usable sidecar is available. Finalizer cleanup runs after
  this rewrite stage.

- `DownloadManager` treats playlist probing as recoverable for ordinary media URLs when the failure is transient; explicit playlist-shaped URLs and missing tools still fail visibly.
- `PlaylistExpansionWorker` preserves an explicit yt-dlp premiere/upcoming diagnostic as `live_status=is_upcoming` and `is_live=true` on a single-item fallback, ensuring native yt-dlp downloader selection before the worker starts.
- `DownloadManager` emits the initial queue row before playlist expansion completes. A supplied `thumbnail_url` is carried into that placeholder and playlist entry metadata so the UI can load a preview immediately.
- `DownloadFinalizer` removes a temporary directory only when its final directory name matches the download ID; stopped downloads retain their temporary data for resume. `DownloadTempCleanup` supplies the configured/completed-downloads/OS-temp fallback chain and refuses shared-root, non-ID, and symlink removal.
- `DownloadQueueManager::cleanupOrphanedTempDirectories()` runs the protected startup reconciliation asynchronously after queue restoration, deleting only unprotected direct-child UUID folders.
- `YtDlpWorker` may retry one transient aria2c transport failure (exit codes 2, 5, 6, or 29), or a narrowly classified missing expected media `.part` output after aria2c returns, with the native downloader, deleting stale metadata sidecars but retaining media partials.
- `YtDlpWorker` may retry once without browser-cookie arguments when a cookie-backed uncapped or higher-capped `bestvideo` request selects a combined stream below 480p. This also handles manifests that omit the adaptive pair needed for direct comparison. Explicit/direct selections, caps at the selected resolution, and active livestreams are excluded.
- `TestYtDlpWorker::testCookieBackedDegradedFormatRecoveryDetection()` covers the adaptive-format and manifest-without-adaptive-pair cases, plus exclusions for explicit caps and missing cookie arguments.
- `YtDlpWorker` rejects a captured final path when retained output contains missing-fragment, empty-data-block, invalid-header, invalid-container, or critical extractor diagnostics; these failures do not enter metadata embedding or finalization.
- `YtDlpWorker` backfills missing `requested_downloads` sizes from matching `formats` metadata and emits bounded temporary-file progress when a native downloader is transferring data without a fresh progress line.
- `YtDlpArgsBuilder` generates synchronization-safe accurate-cut arguments, including audio timestamp normalization and bounded FFmpeg thread settings.
- `ConfigManager` and `DownloadOptionsPage` use `DownloadOptions/prefix_playlist_indices=true` as the canonical default, so playlist audio filenames are consistently prefixed unless the user opts out.
- `ProcessUtils::setBackgroundProcessPriority(QProcess&)` lowers supported Windows post-processing processes to below-normal priority.
- `LocalApiServer::enqueueRequested` carries an explicit `overrideArchive` flag so automation clients can confirm intentional re-downloads without a GUI dialog. Its authenticated `POST /cancel` route emits `cancelRequested(jobId)` for a tracked job and mirrors `DownloadManager::downloadCancelled` into `/status` as `Cancelled`.
- `DownloadManager::videoQualityWarning` is emitted only for successful downloads whose queued type is `video` and whose selected video height is below 480p. Audio-only metadata may contain a source video height but must not trigger this signal.
- `DownloadManager::videoQualityWarning(title, url, message)` includes the best available completed-item title. The main window renders valid HTTP/HTTPS URLs as escaped links and leaves incomplete values as plain text.
- `DownloadManager` acquires `PowerInhibitor` when its active-download count becomes non-zero and releases it at zero or during shutdown. This applies equally to GUI and headless/server workers; acquisition is best-effort when the host exposes no usable power service.
- `ArchiveManager::sameMediaIdentity()` supplies the shared normalized identity used by queue and archive duplicate checks. `DownloadQueueManager` applies it across queued, active, paused, retried, and archived items, including equivalent source URLs with different tracking/share parameters.
- `YtDlpWorker` treats disk-full diagnostics (`No space left on device`, errno/ENOSPC 28, and FFmpeg `-28`) as fatal even when yt-dlp printed a final media path. `FileReplacement::moveReplacing()` preserves an existing destination until replacement succeeds.
- `YtDlpWorker` keeps transfer-stage progress audio-oriented when `-x`/`--extract-audio` is active, including when aria2c reports a combined `video/*` transport MIME type.
- `DownloadQueueState` serializes active items as queued, preserves paused versus stopped/failed resume states, filters non-object backup entries on load, and removes the backup when the terminal queue is empty. `DownloadTempCleanup` resolves configured/completed-downloads/OS-temp roots and refuses shared-root or mismatched-directory removal.
- `DownloadQueueManager::removeTerminalPausedDuplicate(candidate, removedId)` removes only a matching restored item marked stopped/failed for an explicit re-download; ordinary paused entries remain duplicate-protected.
- Queue retry/resume receives the current active-item snapshot so an equivalent active job cannot be re-enqueued during a concurrent state change.
- `FileReplacement::moveReplacing()` is the finalizer's replacement boundary: it reserves an existing destination under a unique backup name, moves or copies the verified source into place, and restores the backup if the replacement fails. Callers must pass a verified temporary output and must not delete the existing destination themselves.

### [DownloadManager](../src/core/DownloadManager.h)
The central manager coordinating download queues, playlist validation, format metadata selection, and the finalization flow.

### [PowerInhibitor](../src/core/PowerInhibitor.h)
Platform integration helper used by `DownloadManager`. `acquire()` and
`release()` are idempotent; the helper prevents system idle sleep without
requesting that the display remain on. Unsupported or unavailable platform
services return a best-effort failure while downloads continue.

#### Public Methods
- `explicit DownloadManager(ConfigManager *configManager, QObject *parent = nullptr)`: Constructor.
- `void enqueueDownload(const QString &url, const QVariantMap &options)`: Adds a new download item. If playlist logic is set to `Ask`, playlist expansion is initiated.
- `void cancelDownload(const QString &id)`: Cancels an active or queued download.
- `void pauseDownload(const QString &id)`: Pauses a download.
- `void unpauseDownload(const QString &id)`: Resumes a paused download.
- `void restartDownloadWithOptions(const QVariantMap &itemData)`: Restarts a download with fresh parameters.
- `void retryDownload(const QVariantMap &itemData)`: Retries a failed download after checking normalized media identity against active, queued, paused, and archived items.
- `void resumeDownload(const QVariantMap &itemData)`: Restarts an interrupted download through the same duplicate-protected path.
- `void finishDownload(const QString &id)`: Triggers post-processing and final file moves.
- `void moveDownloadUp(const QString &id)` / `void moveDownloadDown(const QString &id)`: Reorders item positions in the queue.
- `void processPlaylistSelection(const QString &url, const QString &action, const QVariantMap &options, const QList<QVariantMap> &expandedItems)`: Submits chosen tracks from a playlist.
- `void resumeDownloadWithFormat(const QString &url, const QVariantMap &options, const QString &formatId)`: Enqueues format-specific downloads.
- `void shutdown()`: Cleans up running worker processes, flushes queue state to disk, and terminates thread pools.

#### Key Signals
- `void downloadAddedToQueue(const QVariantMap &itemData)`: Emitted immediately when a URL is registered, before playlist expansion completes.
- `void downloadStarted(const QString &id)`: Emitted when the downloader process launches.
- `void downloadProgress(const QString &id, const QVariantMap &progressData)`: Emitted periodically containing parsed download metrics (speed, ETA, sizes, progress %).
- `void formatSelectionRequested(const QString &url, const QVariantMap &options, const QVariantMap &infoDict)`: Prompts the UI to display the format/quality picker.
- `void downloadFinished(const QString &id, bool success, const QString &message)`: Emitted upon download worker exit.
- `void queueFinished()`: Emitted when all active and queued items have completed.

---

### [ConfigManager](../src/core/ConfigManager.h)
A wrapper around `QSettings` managing read/write operations on the application's configuration file (`settings.ini`).

#### Public Methods
- `explicit ConfigManager(const QString &fileName, QObject *parent = nullptr)`: Primary constructor.
- `explicit ConfigManager(const QString &customPath, bool forTesting, QObject *parent = nullptr)`: Custom path or in-memory config for unit testing.
- `[[nodiscard]] QVariant get(const QString &section, const QString &key, const QVariant &defaultValue = QVariant()) const`: Retrieves a setting.
- `bool set(const QString &section, const QString &key, const QVariant &value)`: Saves a setting and emits a change signal.
- `void remove(const QString &section, const QString &key)`: Deletes a key from the configuration.
- `void save()`: Forces a sync/flush to the physical INI file.
- `void resetToDefaults()`: Clears configuration and applies default settings, preserving essential user paths.

#### Signals
- `void settingChanged(const QString &section, const QString &key, const QVariant &value)`: Emitted immediately on value modification.
- `void settingsReset()`: Emitted after standard preferences are restored.

#### Relevant defaults
- `DownloadOptions/prefix_playlist_indices` defaults to `true`. Invalid or missing values are replaced with the canonical setting default.

---

### [ArchiveManager](../src/core/ArchiveManager.h)
Manages the SQLite-based download history database (`download_archive.db`) to enforce duplicate prevention.

#### Public Methods
- `explicit ArchiveManager(ConfigManager *configManager, QObject *parent = nullptr)`: Constructor.
- `[[nodiscard]] bool isInArchive(const QString &url)`: Normalizes the input URL and queries the database for matches by URL or metadata ID (e.g., YouTube Video ID).
- `[[nodiscard]] bool sameMediaIdentity(const QString &leftUrl, const QString &rightUrl) const`: Compares two source URLs using the same provider-identity or generic normalized-URL rules used by duplicate prevention.
- `void addToArchive(const QString &url)`: Commits a successfully downloaded media item to history.
- `void closeDatabase()`: Safely closes the database connection allocated to the current calling thread.

### [FileReplacement](../src/core/FileReplacement.h)
Small, Qt-only helper used by terminal finalization when an intentional
re-download targets an existing destination.

#### Public Functions
- `[[nodiscard]] bool moveReplacing(const QString &sourcePath, const QString &destinationPath)`: Moves or copies an existing verified source into the destination while preserving the previous destination until the new output succeeds. On a failed replacement, the helper removes only the new partial output and restores the previous file when possible. Empty paths, missing sources, and identical source/destination paths are handled without deleting unrelated files.

---

### [LocalApiServer](../src/core/LocalApiServer.h)
A localhost-bound HTTP daemon (`127.0.0.1:8765`) providing local API automation for enqueuing jobs.

#### Public Methods
- `void start()` / `void stop()`: Starts or stops the listening `QTcpServer`.
- `bool isRunning() const`: Returns server active status.
- `QString getApiKey() const`: Returns the Bearer token read/generated on startup (`api_token.txt`).

#### Signals
- `void enqueueRequested(const QString &url, const QString &type, const QString &jobId, bool overrideArchive)`: Emitted when an external API call passes bearer authentication and submits a valid payload. The final flag is true only when the caller explicitly confirms an intentional archive re-download.
- `void cancelRequested(const QString &jobId)`: Emitted for an authenticated `POST /cancel` request whose `job_id` is currently tracked by the server. The request is forwarded to `DownloadManager::cancelDownload`; it does not terminate a process directly.

#### Slots
- `void onDownloadCancelled(const QString &id)`: Retains the tracked job as `Cancelled` with indeterminate progress until the normal removal notification, allowing `/status` and webhook consumers to observe the terminal state.

#### HTTP contract
- `POST /cancel` accepts `{"job_id":"..."}` (the compatibility key `id` is also accepted), returns `200` when cancellation is queued, `400` for malformed or missing IDs, and `404` for IDs that are not currently tracked. Authentication, localhost binding, and request bounds are the same as for `/enqueue` and `/status`.

---

### [AppUpdater](../src/core/AppUpdater.h)
Handles remote update lookups and triggers installer downloads asynchronously.

#### Public Methods
- `void checkForUpdates()`: Connects via HTTPS to the repository releases endpoint.
- `void downloadAndInstall(const QUrl &downloadUrl)`: Fetches the update package and initiates silent/interactive OS install steps.

#### Key Signals
- `void updateAvailable(const QString &latestVersion, const QString &releaseNotes, const QUrl &downloadUrl)`: Emitted if a newer version is discovered.
- `void downloadProgress(qint64 bytesReceived, qint64 bytesTotal)`: Emitted during installer acquisition.
- `void downloadFinished()`: Emitted when the installer file is complete.
- `void installingUpdate()`: Emitted after the installer is saved and before launch so the owning window can synchronously persist the queue and stop downloader child processes.

The Windows installer is invoked with `/S` by `AppUpdater`; NSIS relaunches the newly installed executable after the silent install completes.

### [YtDlpLiveStatus](../src/core/YtDlpLiveStatus.h)
Small metadata helper used at the playlist-probe boundary. It recognizes only
explicit yt-dlp premiere/upcoming diagnostics and marks a fallback item with
`live_status=is_upcoming` and `is_live=true`. It deliberately does not infer
livestream state from URL text or ambiguous title phrases.

### YtDlpWorker diagnostic boundary
`YtDlpWorkerDiagnostics.cpp` owns the reusable fatal, incomplete-media, and
bounded aria2c missing-output recovery classification used by output parsing and process completion. A printed final
path is not sufficient evidence of a valid media file: missing fragments,
empty data blocks, invalid headers, invalid-input FFmpeg diagnostics, and
critical extractor failures remain terminal and bypass metadata embedding.

---

## 2. Utility Namespaces & Helper APIs

### [ProcessUtils](../src/core/ProcessUtils.h)
Shared utilities for process configuration, binary location, and termination.

#### Key Functions
- `FoundBinary resolveBinary(const QString& name, ConfigManager* configManager)`: Resolves an external tool path, prioritizing manual overrides, then cache checks.
- `void setProcessEnvironment(QProcess &process)`: Injects default environmental variables (e.g., `PYTHONUTF8`, `PYTHONIOENCODING`).
- `void terminateProcessTree(QProcess *process, int gracefulTimeoutMs = 2000)`: Cleanly interrupts (or kills) a process and all spawned sub-processes.
- `void clearCache()`: Invalidates resolved binary path mappings.

### [SmartBinaryResolver](../src/core/SmartBinaryResolver.h)
Implements version-aware discovery logic for external binary dependencies (`yt-dlp`, `ffmpeg`, `gallery-dl`, `aria2c`, `deno`).

#### Key Functions
- `ProcessUtils::FoundBinary resolve(const QString& binaryName, ConfigManager* configManager)`: Dynamically searches system folders, package manager installs (Scoop, WinGet, Chocolatey), and resolves the highest-version executable.
- `QString readIniKeyDirect(const QString &filePath, const QString &section, const QString &key)`: Bypass registry/QSettings caches to read direct config files.

### [SortingManager](../src/core/SortingManager.h)
Applies custom naming rules and tags to structure download targets automatically.

#### Key Functions
- `QString getSortedDirectory(const QVariantMap &videoMetadata, const QVariantMap &downloadOptions)`: Evaluates uploader, title, dates, and token variables to construct output paths.
Linux AppImage release tooling excludes non-deployable host libraries from
linuxdeploy's ELF scan; this packaging workaround is not part of the runtime API.
The release helper keeps linuxdeploy binaries under `build-release/tooling/` and
writes the resulting AppImage under `build-release/`.
