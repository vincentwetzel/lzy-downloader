# LzyDownloader C++ Port TODO

## In Progress
- [ ] Refactor and split large `.cpp` files above or approaching 500 lines (e.g., `DownloadItemWidget.cpp`, `MainWindowConnections.cpp`, and `YtDlpWorkerProcess.cpp`) to preserve optimal AI context limits.
- [ ] Split `ProcessUtils.cpp` after the external-binary resolver expansion; the file is currently above the 500-line guidance and should move version parsing/probing into a focused helper.

## Planned / Future Enhancements
- [ ] CI/CD: Automate GitHub Actions to package the release artifact and compile the NSIS installer on push events.
- [ ] Implement translations for supported interface languages (see `docs/LANGUAGES.md`).
- [ ] Integrate Qt Linguist (`.ts`/`.qm` compiler steps) into `CMakeLists.txt` build automation.
- [ ] Performance: Optimize hot-path stdout line parsing in `YtDlpWorkerProgress.cpp` using regex-free string parsing.

## Completed
- [x] Documented v1.1.69 behavior for auto-detected binary path ownership, completed-with-warning downloads, retry-based cleanup, livestream wait metadata safety, and Discord bridge progress refreshes.
- [x] Refactored external binary resolution through `SmartBinaryResolver` so manual overrides win, the app-local `bin` folder is searched first, stale native-settings ghosts are purged, and multiple candidates can be selected by newest usable version.
- [x] Simplified External Binaries update handling so version probes, update availability checks, and package-manager/tool-native update commands share bounded process handling and clear status refreshes.
- [x] Aligned startup, runtime, and External Binaries page resolution so auto-detected best paths are persisted, version labels use compact tool-specific parsers, and update checks re-probe the active executable before comparing remote releases.
- [x] Complete codebase-wide performance optimizations, string hygiene, and SQLite database connection/lock safety.
- All completed phases and milestones are permanently archived in [CHANGELOG.md](CHANGELOG.md).
