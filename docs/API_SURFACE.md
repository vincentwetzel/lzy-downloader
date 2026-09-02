# LzyDownloader API Surface

This is the callable-interface reference for public classes, signals, and
integration boundaries. Functional requirements belong in [SPEC](SPEC.md),
ownership in [ARCHITECTURE](ARCHITECTURE.md), and settings in [SETTINGS](SETTINGS.md).
Read this file for API changes; avoid loading it for unrelated behavior work.

## Cross-cutting boundary

Public calls must remain asynchronous and use safe Qt object lifetimes. The
normalized identity, finalization, progress, and Local API behavior described
in `SPEC.md` is authoritative; this page records only the callable entry
points and their API-specific constraints.

## Core classes

### [DownloadManager](../src/core/DownloadManager.h)

Coordinates queueing, playlist validation, format selection, workers, and
finalization.

Public methods:

- `void enqueueDownload(const QString &url, const QVariantMap &options)` — queue
  a URL and, when configured, start asynchronous playlist expansion. The
  optional `playlist_logic` request value (`Ask`, `Download All (no prompt)`,
  or `Download Single (ignore playlist)`) overrides `General/playlist_logic`;
  invalid values use `Ask`, while non-interactive requests force all items.
- `void cancelDownload(const QString &id)`, `pauseDownload(const QString &id)`,
  `unpauseDownload(const QString &id)` — control a queued or active item.
- `void restartDownloadWithOptions(const QVariantMap &itemData)`,
  `retryDownload(const QVariantMap &itemData)`,
  `resumeDownload(const QVariantMap &itemData)` — requeue through identity and
  active-snapshot duplicate protection.
- `void finishDownload(const QString &id)` — start post-processing and final
  destination handling.
- `void moveDownloadUp(const QString &id)`,
  `moveDownloadDown(const QString &id)` — reorder.
- `void processPlaylistSelection(const QString &url, const QString &action,
  const QVariantMap &options, const QList<QVariantMap> &expandedItems)` — queue
  tracks selected by the playlist dialog.
- `void resumeDownloadWithFormat(const QString &url, const QVariantMap &options,
  const QString &formatId)` — queue a concrete format.
- `void shutdown()` — stop workers, flush queue state, and terminate pools.

Worker admission is coordinated by `GlobalDownloadLimiter` across separate
GUI and server/headless/background processes. A manager retries admission when
the shared limit is full and releases its reservations on worker completion or
shutdown; this does not merge the processes' queue state.

yt-dlp/gallery-dl workers own their `QProcess` in dedicated `QThread` event
loops. Metadata embedding and finalization likewise run off the GUI thread;
their signals marshal only immutable progress/state back to the manager. GUI
rows coalesce high-frequency progress to a bounded repaint rate, while API and
webhook consumers continue to receive backend progress independently.

Key signals:

- `void downloadAddedToQueue(const QVariantMap &itemData)` fires before
  playlist expansion completes.
- `void downloadStarted(const QString &id)` fires when the downloader launches.
- `void downloadProgress(const QString &id, const QVariantMap &progressData)`
  carries progress, speed, ETA, sizes, and (when available) the aggregate
  multi-stream `overall_progress` value used by desktop and webhook consumers.
- `void formatSelectionRequested(const QString &url, const QVariantMap &options,
  const QVariantMap &infoDict)` requests a runtime picker.
- `void downloadFinished(const QString &id, bool success, const QString &message)`
  reports worker completion.
- `void nonInteractiveRequestFailed(const QString &jobId, const QString &url,
  const QString &error)` reports validation, duplicate, missing-binary, runtime,
  and terminal download failures for non-interactive requests without opening
  a modal dialog.
- `void queueFinished()` fires when active and queued work is complete.

### [PowerInhibitor](../src/core/PowerInhibitor.h)

`[[nodiscard]] bool acquire() noexcept` and `void release() noexcept` are
idempotent and prevent idle sleep without forcing the display on. Unsupported
platform services are best-effort.

### [ConfigManager](../src/core/ConfigManager.h)

Wraps Qt `QSettings` for `settings.ini`.

- `explicit ConfigManager(const QString &fileName, QObject *parent = nullptr)`;
  the testing constructor also accepts `(const QString &, bool, QObject *)`.
- `QVariant get(const QString &section, const QString &key,
  const QVariant &defaultValue = QVariant()) const`, `bool set(const QString &section,
  const QString &key, const QVariant &value)`, `remove(const QString &section,
  const QString &key)`, and `save()` read/write settings.
- `void resetToDefaults()` restores defaults while preserving required user paths.
- `settingChanged(section, key, value)` and `settingsReset()` report changes.
- Reads, writes, synchronization, and config-directory lookup are serialized so
  workers may safely consume shared settings without concurrent `QSettings` use.
- `DownloadOptions/prefix_playlist_indices` defaults to `true`; invalid values
  are replaced with the canonical default.

### [ArchiveManager](../src/core/ArchiveManager.h)

Owns SQLite `download_archive.db` access and archive duplicate checks.

- `bool isInArchive(const QString &url)` checks normalized URL/provider identity.
- `bool sameMediaIdentity(const QString &leftUrl, const QString &rightUrl) const`
  compares the shared identity.
- `void addToArchive(const QString &url)` records successful media.
- `void closeDatabase()` closes only the calling thread's Qt SQL connection.

### [FileReplacement](../src/core/FileReplacement.h)

`bool moveReplacing(const QString &sourcePath, const QString &destinationPath)`
moves/copies verified output while retaining the previous destination until
success, then restores it on failure. Empty, missing, or identical paths must
not delete unrelated files.

### [DownloadFinalizer](../src/core/DownloadFinalizer.h)

- `void finalize(const QString &id, DownloadItem item)` verifies stable output,
  applies sorting and playlist filename policy, performs destination
  replacement, updates the archive, and emits asynchronous progress/completion
  signals. Filesystem work is kept off the GUI thread.

### [MetadataEmbedder](../src/core/MetadataEmbedder.h)

- `void processFile(const QString &filePath, int trackNumber,
  bool normalizeContainerTimestamps)` performs the FFmpeg metadata/artwork
  rewrite asynchronously and emits `finished(bool, error)`.
- `void cancel()` terminates its active FFmpeg process and cancels the current
  stage; callers must invoke it through the embedder's worker-thread event loop.

### [LocalApiServer](../src/core/LocalApiServer.h)

Serves authenticated automation on `127.0.0.1:8765`.

- `void start()`, `void stop()`, `bool isRunning() const`, and
  `QString getApiKey() const` manage the server and
  token (`api_token.txt`; server/headless/background mode uses `Server/`).
- `void enqueueRequested(const QString &url, const QString &type,
  const QString &jobId, bool overrideArchive)` fires for an authorized
  valid enqueue. `jobId` is generated when omitted; the override is explicit.
- `void enqueueWithCookieFileRequested(const QString &url, const QString &type,
  const QString &jobId, bool overrideArchive, const QString &cookieFile)` is
  the browser-companion variant; it carries an owned temporary cookie-file path
  that is never included in public job snapshots.
- `void cancelRequested(const QString &jobId)` fires for an authorized tracked
  `POST /cancel`.
- `void onDownloadCancelled(const QString &id)` retains `Cancelled` for
  status/webhook consumers
  until normal removal.
- `POST /cancel` accepts `job_id` or compatibility key `id`; it returns 200 for
  an accepted tracked request, 400 for malformed/missing IDs, and 404 for an
  unknown ID. `/enqueue` and `/status` share its authentication and bounds.
  Browser-companion requests may include a bounded `client_id`; status and
  cancellation are then restricted to jobs enqueued by that client.
  Browser-companion enqueue requests may also include a validated temporary
  `cookie_file`; it is passed to yt-dlp as `--cookies` and removed when the
  job reaches a terminal/removal path.
  Webhook payloads may include `overall_progress` for multi-stream jobs and
  retain terminal completion/cancellation state for bridge consumers.

### Browser native-messaging host

`LzyDownloaderBrowserHost` is a console target built from
`src/integration/BrowserNativeMessagingHost.cpp`. Chrome invokes it through
the `com.lzydownloader.browser` native-messaging manifest. It accepts only the
protocol-1 `ping`, `enqueue`, `status`, and `cancel` operations, validates
bounded HTTP(S) URLs and job IDs, and keeps the Local API bearer token inside
the host. Every response contains protocol `1`, the request ID, and either a
bounded success payload or a stable error code. An unsupported request version
returns `UNSUPPORTED_PROTOCOL`; the extension must treat any response with a
different or missing protocol as a protocol failure. The host returns
sanitized per-request status rather than forwarding the desktop API response
wholesale. The host starts the platform-native LzyDownloader executable with
`--server --exit-after` only from its own validated application directory.

The host manifest must allowlist the exact extension origin. When the desktop
build receives the final `LZY_BROWSER_EXTENSION_ID`, the application registers
the host for the current user on Windows (Chrome/Chromium registry), macOS
(Chrome/Chromium application support), and Linux (Chrome/Chromium XDG
configuration). Linux AppImage launches create a persistent wrapper so the
manifest never points at a temporary AppImage mount. Builds without the exact
production ID leave registration disabled; local development uses the browser-
extension project's cross-platform registration helper.

The host accepts at most a 1 MiB length-prefixed message. `enqueue` accepts only
HTTP(S) URLs, `video` or `audio` type, a bounded client ID, and an optional JSON
cookie array. The cookie helper accepts at most 500 entries, limits each field
to 4096 bytes, rejects control characters, and creates a Netscape cookie file
no larger than 900 KiB. Each cookie must match the target host and URL path
boundary; secure cookies require HTTPS and numeric expirations must be whole
seconds from zero through 2100-01-01 UTC. Generated files are omitted from
queue backups and status snapshots, then removed on rejection, terminal
completion/cancellation/removal, or expiry cleanup.

### [AppUpdater](../src/core/AppUpdater.h)

- `void checkForUpdates()` performs an asynchronous HTTPS release lookup.
- `void downloadAndInstall(const QUrl &downloadUrl)` downloads the package and
  starts install.
- `updateAvailable(const QString &latestVersion, const QString &releaseNotes,
  const QUrl &downloadUrl)`, `downloadProgress(qint64 bytesReceived,
  qint64 bytesTotal)`, `downloadFinished()`, and `installingUpdate()` report
  lookup/download/handoff stages.

Before installer launch, the owner must synchronously save resumable queue state
and stop child processes. Windows silent `/S` installs relaunch the installed
`LzyDownloader.exe`; macOS DMGs are opened through Finder and must match CPU
architecture.

### [YtDlpLiveStatus](../src/core/YtDlpLiveStatus.h)

Maps explicit yt-dlp premiere/upcoming diagnostics to fallback metadata
(`live_status=is_upcoming`, `is_live=true`). It does not infer live state from
URL text or ambiguous titles.

## Utility APIs

### [ProcessUtils](../src/core/ProcessUtils.h)

- `FoundBinary resolveBinary(const QString &name, ConfigManager *configManager)`
  preserves explicit overrides, considers
  app/user/system/package candidates, and reports bounded WinGet discovery.
- `void setProcessEnvironment(QProcess &process)` sets the managed child
  environment.
- `void terminateProcessTree(QProcess *process, int gracefulTimeoutMs = 2000)`
  stops a process tree.
- `void clearCache()` invalidates binary mappings.

### [SmartBinaryResolver](../src/core/SmartBinaryResolver.h)

`ProcessUtils::FoundBinary resolve(const QString &binaryName,
ConfigManager *configManager)` performs version-aware discovery while
preserving explicit overrides. `QString readIniKeyDirect(const QString &filePath,
const QString &section, const QString &key)` reads the file without
registry/QSettings cache effects.

### [SortingManager](../src/core/SortingManager.h)

`QString getSortedDirectory(const QVariantMap &videoMetadata,
const QVariantMap &downloadOptions)` evaluates metadata tokens,
aliases, and sanitization rules to produce a destination directory.

## Release boundary

Release tooling is not runtime API. `build_release.py` and the CI workflow own
tag/version checks, platform packaging, checksums, Linux matching-qmake and
static/dynamic Qt handling, and architecture-labelled macOS artifacts. See
`UPDATE_AND_RELEASE.md` for commands.
