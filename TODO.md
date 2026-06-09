# LzyDownloader C++ Port TODO

## In Progress
- [ ] Refactor and split large `.cpp` files above or approaching 500 lines (e.g., `DownloadItemWidget.cpp`, `MainWindowConnections.cpp`, and `YtDlpWorkerProcess.cpp`) to preserve optimal AI context limits.

## Planned / Future Enhancements
- [ ] Security: Implement code signature verification (SHA-256 hash checks) for retrieved external binaries inside `YtDlpUpdater` and `GalleryDlUpdater`.
- [ ] CI/CD: Automate GitHub Actions to package the release artifact and compile the NSIS installer on push events.
- [ ] Implement translations for supported interface languages (see `docs/LANGUAGES.md`).
- [ ] Integrate Qt Linguist (`.ts`/`.qm` compiler steps) into `CMakeLists.txt` build automation.
- [ ] Performance: Optimize hot-path stdout line parsing in `YtDlpWorkerProgress.cpp` using regex-free string parsing.

## Completed
- [x] Complete codebase-wide performance optimizations, string hygiene, and SQLite database connection/lock safety.
- All completed phases and milestones are permanently archived in [CHANGELOG.md](CHANGELOG.md).
