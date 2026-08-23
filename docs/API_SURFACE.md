# LzyDownloader API Surface

This document defines the public API boundaries and interface contracts for the primary components of the **LzyDownloader** C++ codebase. It serves as a guide for developer integration, interface compliance, and test authoring.

## Placement & Design Philosophy
Documentation outlining system specifications, interfaces, and architecture standards is placed in the `/docs` directory. This isolates technical documentation from source code while keeping it accessible within the codebase.

The API surface adheres to **Qt best practices**:
- Use of the pointer-to-member-function syntax for all `connect()` calls to ensure compile-time checkability.
- No blocking operations on the main thread; asynchronous logic via `QProcess` or workers.
- Proper thread-affinity and memory safety (e.g., using `deleteLater()` on workers).

Release automation is a CI integration contract rather than a runtime API:
`v*` tags build Windows and Linux artifacts, Linux CI provisions the vcpkg
Qt/XCB development prerequisites before configuration, and validation installs
yt-dlp from the prerelease channel. These dependencies are not bundled into
the application or written to user settings.

---

## 1. Core Classes

### Current behavioral contracts

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
- `YtDlpArgsBuilder` generates synchronization-safe accurate-cut arguments, including audio timestamp normalization and bounded FFmpeg thread settings.
- `ConfigManager` and `DownloadOptionsPage` use `DownloadOptions/prefix_playlist_indices=true` as the canonical default, so playlist audio filenames are consistently prefixed unless the user opts out.
- `ProcessUtils::setBackgroundProcessPriority(QProcess&)` lowers supported Windows post-processing processes to below-normal priority.
- `LocalApiServer::enqueueRequested` carries an explicit `overrideArchive` flag so automation clients can confirm intentional re-downloads without a GUI dialog.
- `DownloadManager::videoQualityWarning` is emitted only for successful downloads whose queued type is `video` and whose selected video height is below 480p. Audio-only metadata may contain a source video height but must not trigger this signal.
- `YtDlpWorker` keeps transfer-stage progress audio-oriented when `-x`/`--extract-audio` is active, including when aria2c reports a combined `video/*` transport MIME type.

### [DownloadManager](../src/core/DownloadManager.h)
The central manager coordinating download queues, playlist validation, format metadata selection, and the finalization flow.

#### Public Methods
- `explicit DownloadManager(ConfigManager *configManager, QObject *parent = nullptr)`: Constructor.
- `void enqueueDownload(const QString &url, const QVariantMap &options)`: Adds a new download item. If playlist logic is set to `Ask`, playlist expansion is initiated.
- `void cancelDownload(const QString &id)`: Cancels an active or queued download.
- `void pauseDownload(const QString &id)`: Pauses a download.
- `void unpauseDownload(const QString &id)`: Resumes a paused download.
- `void restartDownloadWithOptions(const QVariantMap &itemData)`: Restarts a download with fresh parameters.
- `void retryDownload(const QVariantMap &itemData)`: Retries a failed download.
- `void resumeDownload(const QVariantMap &itemData)`: Restarts an interrupted download.
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
- `void addToArchive(const QString &url)`: Commits a successfully downloaded media item to history.
- `void closeDatabase()`: Safely closes the database connection allocated to the current calling thread.

---

### [LocalApiServer](../src/core/LocalApiServer.h)
A localhost-bound HTTP daemon (`127.0.0.1:8765`) providing local API automation for enqueuing jobs.

#### Public Methods
- `void start()` / `void stop()`: Starts or stops the listening `QTcpServer`.
- `bool isRunning() const`: Returns server active status.
- `QString getApiKey() const`: Returns the Bearer token read/generated on startup (`api_token.txt`).

#### Signals
- `void enqueueRequested(const QString &url, const QString &type, const QString &jobId, bool overrideArchive)`: Emitted when an external API call passes bearer authentication and submits a valid payload. The final flag is true only when the caller explicitly confirms an intentional archive re-download.

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
