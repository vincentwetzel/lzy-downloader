# LzyDownloader C++ Port TODO

## In Progress
- [x] Prevent system idle sleep during active downloads in GUI and headless/server modes with platform-native power inhibitors.
- [x] Add authenticated Local API and Discord bridge cancellation for tracked downloads.
- [x] Upgrade the headless test runner with fail-fast builds, timestamped output, summaries, and a previous-failure suspects mode.
- [ ] Verify direct-download fallback behavior with a slow playlist probe and an explicit playlist URL.
- [x] Add an automated manager-level test for transient playlist-probe fallback and explicit playlist failure classification.
- [x] Expand Qt coverage for queue-backup persistence, temporary-directory ownership rules, and aria2c recovery boundaries.
- [x] Deduplicate equivalent source URLs across queue, retry, active, paused, and archive states using shared normalized media identity.
- [x] Suppress duplicate failed/stopped recovery entries in the Discord bridge before `/retry_failed` enqueue requests.
- [x] Replace matching restored stopped/failed jobs for explicit non-interactive re-downloads while preserving genuinely paused duplicate protection.
- [x] Protect retry/resume requests with the current active-item snapshot and shared normalized media identity.
- [x] Recover native progress when `info.json` omits `requested_downloads`, using matching format sizes and bounded temporary-file polling.
- [x] Treat disk-full diagnostics as terminal failures and preserve existing destination files during replacement.
- [x] Add a native-only `build_release.py --target` selector for explicit macOS VM packaging tests.
- [ ] Refactor and split large `.cpp` files above or approaching 500 lines (e.g., `BinariesPage.cpp`, `DownloadItemWidget.cpp`, `MainWindowConnections.cpp`, and `YtDlpWorkerProcess.cpp`) to preserve optimal AI context limits.
- [ ] Split `ProcessUtils.cpp` after the external-binary resolver expansion; the file is currently above the 500-line guidance and should move version parsing/probing into a focused helper.
- [ ] Evaluate whether `YtDlpWorker` should expose a reusable capped diagnostic tail helper so warning/error retention stays consistent across workers.

## Planned / Future Enhancements
- [ ] Implement translations for supported interface languages (see `docs/LANGUAGES.md`).
- [ ] Integrate Qt Linguist (`.ts`/`.qm` compiler steps) into `CMakeLists.txt` build automation.
