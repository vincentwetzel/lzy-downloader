# LzyDownloader C++ Port TODO

## In Progress
- [x] Upgrade the headless test runner with fail-fast builds, timestamped output, summaries, and a previous-failure suspects mode.
- [ ] Verify direct-download fallback behavior with a slow playlist probe and an explicit playlist URL.
- [x] Add an automated manager-level test for transient playlist-probe fallback and explicit playlist failure classification.
- [x] Expand Qt coverage for queue-backup persistence, temporary-directory ownership rules, and aria2c recovery boundaries.
- [ ] Refactor and split large `.cpp` files above or approaching 500 lines (e.g., `BinariesPage.cpp`, `DownloadItemWidget.cpp`, `MainWindowConnections.cpp`, and `YtDlpWorkerProcess.cpp`) to preserve optimal AI context limits.
- [ ] Split `ProcessUtils.cpp` after the external-binary resolver expansion; the file is currently above the 500-line guidance and should move version parsing/probing into a focused helper.
- [ ] Evaluate whether `YtDlpWorker` should expose a reusable capped diagnostic tail helper so warning/error retention stays consistent across workers.

## Planned / Future Enhancements
- [ ] Implement translations for supported interface languages (see `docs/LANGUAGES.md`).
- [ ] Integrate Qt Linguist (`.ts`/`.qm` compiler steps) into `CMakeLists.txt` build automation.
