# LzyDownloader C++ TODO

Only unfinished work belongs here; completed work is recorded in
`CHANGELOG.md` or the historical archive.

## In progress

- [ ] Release smoke-test ordinary-download fallback with a slow playlist probe
  and an explicit playlist URL.
- [ ] Split C++ files at or above the 500-line context limit, currently
  `BinariesPage.cpp`, `DownloadItemWidget.cpp`, `MainWindowConnections.cpp`,
  `YtDlpWorkerProcess.cpp`, and `ProcessUtils.cpp` (move version parsing/probing
  into a focused helper).
- [ ] Evaluate a reusable capped diagnostic-tail helper for `YtDlpWorker` and
  other workers.

## Planned

- [ ] Finalize Chrome browser-companion registration: pass the published
  extension ID to each platform release configuration and verify first-launch
  registration on Windows, Linux, and macOS.
- [ ] Apply the GitHub repository description and recommended topics after
  re-authentication.
- [ ] Submit verified releases to package directories such as WinGet or Scoop
  after licensing and installer metadata are finalized.
- [ ] Upload `docs/assets/social-preview.png` as the GitHub social preview.
- [ ] Add translations listed in `docs/LANGUAGES.md` and integrate Qt Linguist
  (`.ts`/`.qm`) compilation into `CMakeLists.txt`.
