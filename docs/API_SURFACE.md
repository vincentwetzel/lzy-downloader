# LzyDownloader API Surface

This document defines the public API boundaries and interface contracts for the primary components of the **LzyDownloader** C++ codebase. It serves as a guide for developer integration, interface compliance, and test authoring.

## Placement & Design Philosophy
Documentation outlining system specifications, interfaces, and architecture standards is placed in the `/docs` directory. This isolates technical documentation from source code while keeping it accessible within the codebase.

The API surface adheres to **Qt best practices**:
- Use of the pointer-to-member-function syntax for all `connect()` calls to ensure compile-time checkability.
- No blocking operations on the main thread; asynchronous logic via `QProcess` or workers.
- Proper thread-affinity and memory safety (e.g., using `deleteLater()` on workers).

---

## 1. Core Classes

### [DownloadManager](file:///E:/coding_workspaces/CPP/lzy-downloader/src/core/DownloadManager.h)
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

### [ConfigManager](file:///E:/coding_workspaces/CPP/lzy-downloader/src/core/ConfigManager.h)
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

---

### [ArchiveManager](file:///E:/coding_workspaces/CPP/lzy-downloader/src/core/ArchiveManager.h)
Manages the SQLite-based download history database (`download_archive.db`) to enforce duplicate prevention.

#### Public Methods
- `explicit ArchiveManager(ConfigManager *configManager, QObject *parent = nullptr)`: Constructor.
- `[[nodiscard]] bool isInArchive(const QString &url)`: Normalizes the input URL and queries the database for matches by URL or metadata ID (e.g., YouTube Video ID).
- `void addToArchive(const QString &url)`: Commits a successfully downloaded media item to history.
- `void closeDatabase()`: Safely closes the database connection allocated to the current calling thread.

---

### [LocalApiServer](file:///E:/coding_workspaces/CPP/lzy-downloader/src/core/LocalApiServer.h)
A localhost-bound HTTP daemon (`127.0.0.1:8765`) providing local API automation for enqueuing jobs.

#### Public Methods
- `void start()` / `void stop()`: Starts or stops the listening `QTcpServer`.
- `bool isRunning() const`: Returns server active status.
- `QString getApiKey() const`: Returns the Bearer token read/generated on startup (`api_token.txt`).

#### Signals
- `void enqueueRequested(const QString &url, const QString &type, const QString &jobId)`: Emitted when an external API call passes bearer authentication and submits a valid payload.

---

### [AppUpdater](file:///E:/coding_workspaces/CPP/lzy-downloader/src/core/AppUpdater.h)
Handles remote update lookups and triggers installer downloads asynchronously.

#### Public Methods
- `void checkForUpdates()`: Connects via HTTPS to the repository releases endpoint.
- `void downloadAndInstall(const QUrl &downloadUrl)`: Fetches the update package and initiates silent/interactive OS install steps.

#### Key Signals
- `void updateAvailable(const QString &latestVersion, const QString &releaseNotes, const QUrl &downloadUrl)`: Emitted if a newer version is discovered.
- `void downloadProgress(qint64 bytesReceived, qint64 bytesTotal)`: Emitted during installer acquisition.
- `void downloadFinished()`: Emitted when the installer file is complete.

---

## 2. Utility Namespaces & Helper APIs

### [ProcessUtils](file:///E:/coding_workspaces/CPP/lzy-downloader/src/core/ProcessUtils.h)
Shared utilities for process configuration, binary location, and termination.

#### Key Functions
- `FoundBinary resolveBinary(const QString& name, ConfigManager* configManager)`: Resolves an external tool path, prioritizing manual overrides, then cache checks.
- `void setProcessEnvironment(QProcess &process)`: Injects default environmental variables (e.g., `PYTHONUTF8`, `PYTHONIOENCODING`).
- `void terminateProcessTree(QProcess *process, int gracefulTimeoutMs = 2000)`: Cleanly interrupts (or kills) a process and all spawned sub-processes.
- `void clearCache()`: Invalidates resolved binary path mappings.

### [SmartBinaryResolver](file:///E:/coding_workspaces/CPP/lzy-downloader/src/core/SmartBinaryResolver.h)
Implements version-aware discovery logic for external binary dependencies (`yt-dlp`, `ffmpeg`, `gallery-dl`, `aria2c`, `deno`).

#### Key Functions
- `ProcessUtils::FoundBinary resolve(const QString& binaryName, ConfigManager* configManager)`: Dynamically searches system folders, package manager installs (Scoop, WinGet, Chocolatey), and resolves the highest-version executable.
- `QString readIniKeyDirect(const QString &filePath, const QString &section, const QString &key)`: Bypass registry/QSettings caches to read direct config files.

### [SortingManager](file:///E:/coding_workspaces/CPP/lzy-downloader/src/core/SortingManager.h)
Applies custom naming rules and tags to structure download targets automatically.

#### Key Functions
- `QString getSortedDirectory(const QVariantMap &videoMetadata, const QVariantMap &downloadOptions)`: Evaluates uploader, title, dates, and token variables to construct output paths.
