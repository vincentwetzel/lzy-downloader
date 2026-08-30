# LzyDownloader C++ TODO

Only unfinished work belongs here; completed work is recorded in
`CHANGELOG.md` or the historical archive.

## Planned

- [ ] Split the remaining oversized C++ files, starting with
  `src/ui/advanced_settings/BinariesPage.cpp`, `src/core/ProcessUtils.cpp`,
  and `src/core/YtDlpWorkerOutput.cpp`; keep each production source file below
  500 lines.

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
