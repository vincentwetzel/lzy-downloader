# LzyDownloader C++ Update & Release Guide

The main-window footer keeps status counters and current speed on its first row;
the exit-after-downloads control remains the rightmost item.

This document describes how to build, package, and release the C++ version of LzyDownloader with auto-update support.

## Prerequisites

1. **NSIS (Nullsoft Scriptable Install System)**
   - Download from: https://nsis.sourceforge.io/Download
   - Install to default location (e.g., `C:\Program Files (x86)\NSIS`)
   - Verify: `makensis /version` in PowerShell
   - GitHub Actions installs NSIS automatically on the Windows runner before invoking `build_release.py`.

2. **CMake & MSVC**
   - Required to build the C++ application. GitHub Actions uses the hosted Windows MSVC environment; local Windows builds should use an MSVC toolchain compatible with Qt 6.

3. **Qt 6**
   - Required for building the application.
   - The manifest build path uses vcpkg; keep `vcpkg.json` synchronized with the app version before release and keep its `builtin-baseline` pinned for reproducible dependency resolution.

4. **Linux Qt/XCB Development Packages (Linux release builds)**
   - When vcpkg builds Qt Base on Ubuntu, install `autoconf`, `automake`, `autoconf-archive`, `bison`, `curl`, `flex`, `libtool`, `tar`, `unzip`, `zip`, `'^libxcb.*-dev'`, `libx11-xcb-dev`, `libxkbcommon-dev`, `libxkbcommon-x11-dev`, `libxi-dev`, `libxrender-dev`, `libegl1-mesa-dev`, `libgl1-mesa-dev`, and `libglu1-mesa-dev`.
   - The release workflow installs these prerequisites before manifest resolution; runtime-only XCB packages do not provide the headers or pkg-config data Qt's XCB backend requires.

5. **macOS Qt deployment (macOS release builds)**
   - GitHub Actions uses Qt 6's `clang_64` package on `macos-13` for Intel and `clang_arm64` on `macos-14` for Apple Silicon.
   - `build_release.py` creates a native `.app` bundle, runs `macdeployqt`, embeds an ICNS icon, and packages a separate architecture-labelled DMG. These are unsigned unless a future signing/notarization workflow is configured.

6. **yt-dlp Nightly (release validation)**
   - Release automation installs the latest yt-dlp prerelease with `python -m pip install --pre --upgrade yt-dlp`; this keeps packaging validation aligned with current extractor/runtime changes. Verify low-quality warnings show title and source-link context in GUI smoke checks.
   - This is a build-validation dependency only. yt-dlp is not bundled by the release job; runtime executable provisioning remains governed by External Tools settings.

7. **Git & GitHub**
   - Repo: https://github.com/vincentwetzel/lzy-downloader
   - Must have access to create Releases

## Build Process

Before release, review the `[Unreleased]` section of `CHANGELOG.md` and verify that the maintained documentation set (`README.md`, `AGENTS.md`, `TODO.md`, and the active files under `docs/`) matches the current implementation. In particular, ordinary URLs must recover from transient playlist-probe failures, browser-cookie format downgrades must retry once from an uncapped or higher-capped `bestvideo` request when the selected combined stream is below 480p, video-only quality warnings must not inspect audio-job height metadata, audio extraction labels must remain audio-oriented when aria2c transfers a combined source, the worker's degraded-format regression test must remain covered, aria2c transport or missing-output failures must use the bounded native-downloader fallback while preserving partials, accurate cuts must use timestamp-normalized audio and bounded/background-priority FFmpeg work, playlist audio filename prefixes must agree with the settings default, narrow Active Downloads rows must keep actions visible, tracked thumbnail sidecars must be remuxed as attached artwork before cleanup, and Windows FFmpeg/FFprobe replacement must tolerate transient locks.

### Step 1: Update Extractor Lists

**IMPORTANT:** Before building a new release, you must refresh the extractor lists to ensure the application can handle the latest website changes.

Run the following Python scripts from the project root:
```powershell
python ./update_yt-dlp_extractors.py
python ./update_gallery-dl_extractors.py
```
This will update `extractors_yt-dlp.json` and `extractors_gallery-dl.json`. Both scripts share `extractor_utils.py` for domain parsing with precompiled regexes, are intentionally non-interactive, and should return directly to the shell when they finish.

### Step 2: Update Version Number

Update the version in `CMakeLists.txt` (`project(VERSION x.y.z)`). This is the single source of truth for the release version. The app version is generated from there into `version.h`, used by the Windows resources, and passed into the NSIS installer build by `build_release.py`.

Also update `vcpkg.json` `version-string` to the same version, keep its `builtin-baseline` pinned to the intended vcpkg commit, and ensure `CHANGELOG.md` has the release notes under the matching dated version heading.
The matching GitHub release body belongs in `release-notes/vX.Y.Z.md`; create
that file before tagging because the tag-triggered workflow attaches it
automatically.

**Release rule:** Do not manually rename the installer `.exe` to fix a version mismatch. If the setup filename version is wrong, fix the release inputs/scripts and rebuild so the installer filename, Windows app version, and uninstall `DisplayVersion` all match the same `CMakeLists.txt` version.

### Step 3: Run the Release Builder

The preferred release path is the helper script:
```powershell
python .\build_release.py
```

This script:
- Deletes the existing `build-release/` directory to avoid stale DLL mismatches
- Refreshes both extractor JSON files
- Configures a Release build with CMake
- Builds the platform-native `LzyDownloader` executable (and `LzyDownloader.app` on macOS)
- On Windows, runs `makensis` from `PATH` when available, otherwise the standard NSIS installation path, against `LzyDownloader.nsi` with `/DAPP_VERSION=<version from CMakeLists.txt>` and `/DRELEASE_BUILD_DIR=build-release\Release`
- The Windows installer finish page offers a checked-by-default option to launch `LzyDownloader.exe` after installation
- On Linux, stages a clean `build-release/AppDir`, generates a linuxdeploy desktop file whose `Icon` matches the resized release PNG, and packages `LzyDownloader-<version>-x86_64.AppImage` with `linuxdeploy`
- On Linux, selects qmake from `build-release/vcpkg_installed/*/tools/Qt*/bin` when available so linuxdeploy discovers the same Qt modules used by the executable; the SQLite driver remains included through the explicit QtSql module.
- On Linux, detects whether the vcpkg-built executable uses static Qt. Static-Qt builds skip linuxdeploy-plugin-qt because vcpkg's `.a`/`.prl` SQL driver files are not deployable ELF plugins; dynamic-Qt builds retain the Qt/SQLite plugin deployment. vcpkg's dbus runtime is excluded from linuxdeploy's ELF scan.
- On macOS, runs `macdeployqt`, converts the release PNG into the bundle's ICNS icon, and emits `LzyDownloader-<version>-macos-x86_64.dmg` or `LzyDownloader-<version>-macos-arm64.dmg` according to the runner architecture.

### Step 3b: Run Headless Tests

Before packaging or publishing, run the Qt test suite through the headless helper:

```powershell
python .\run_headless_tests.py --build-dir build --config Release
```

The helper builds the selected configuration and runs `ctest` with `QT_QPA_PLATFORM=offscreen` and parallel jobs based on the host CPU count. Test sources and fixtures are kept in the top-level `tests/` directory. Coverage includes yt-dlp/gallery-dl argument building, playlist parsing and transient-probe fallback, download-manager behavior, worker progress and recovery diagnostics, archive/config/API/process utilities, URL validation, sorting, playlist-range selection, UI progress widgets, and the local end-to-end fixture.

### Step 4: Manual Build Steps

If you are not using `build_release.py`, run the equivalent Windows commands manually:
```powershell
cmake -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release
& 'C:\Program Files (x86)\NSIS\makensis.exe' "/DAPP_VERSION=X.X.X" "/DRELEASE_BUILD_DIR=build-release\Release" LzyDownloader.nsi
```

Replace `X.X.X` with the exact version from `CMakeLists.txt`.

`CMakeLists.txt` already runs `windeployqt`, re-copies the resolved Qt runtime DLLs from the configured Qt installation, and deploys the OpenSSL runtime DLLs (`libcrypto-3-x64.dll`, `libssl-3-x64.dll`) when available. Keep the deployed compression/runtime dependencies that Qt ships with, including `zlib1.dll`, because `Qt6Network.dll` depends on them on Windows.

## Release to GitHub

GitHub Actions automatically builds release assets when a `v*` tag is pushed. The workflow at `.github/workflows/release.yml` runs `python build_release.py` on `windows-latest`, `ubuntu-22.04`, `macos-13` (Intel), and `macos-14` (Apple Silicon), then uploads the Windows installer, Linux AppImage, and both architecture-labelled macOS DMGs to the GitHub Release for that tag. If the matching release-notes file is absent, CI creates a minimal fallback body before publication. Use `workflow_dispatch` to run the matrix as a non-publishing validation; uploads are tag-only.

### Step 1: Commit Release Inputs

Before tagging, commit the synchronized release inputs:

```powershell
git add CMakeLists.txt vcpkg.json CHANGELOG.md README.md UPDATE_AND_RELEASE.md docs/ AGENTS.md TODO.md .github/workflows/release.yml build_release.py LzyDownloader.nsi src/ui/LzyDownloader.desktop extractors_yt-dlp.json extractors_gallery-dl.json
git commit -m "Release vX.X.X"
git push origin HEAD
```

### Step 2: Create and Push a Git Tag

```powershell
git tag -a vX.X.X -m "Release version X.X.X"
git push origin vX.X.X
```

Pushing the tag starts the `Build and Release` workflow. Watch the Actions run until both matrix jobs complete, then verify the GitHub Release contains:

- `LzyDownloader-Setup-X.X.X.exe`
- `LzyDownloader-X.X.X-x86_64.AppImage`
- `LzyDownloader-X.X.X-macos-x86_64.dmg`
- `LzyDownloader-X.X.X-macos-arm64.dmg`

### Step 3: Manual GitHub Release Fallback

If the workflow is unavailable, navigate to https://github.com/vincentwetzel/lzy-downloader/releases and:

1. Click "Create a new release"
2. **Tag version:** `vX.X.X` (must match Git tag)
3. **Release title:** `LzyDownloader X.X.X`
4. **Description:** Add release notes.
5. **Attach Assets:** Upload `LzyDownloader-Setup-X.X.X.exe`
   - Also attach `LzyDownloader-X.X.X-x86_64.AppImage` for Linux systems.
   - Also attach both architecture-labelled macOS DMGs.
6. Click "Publish release"

## Release Checklist

- [ ] Extractor lists updated (`extractors_yt-dlp.json`, `extractors_gallery-dl.json`)
- [ ] Extractor refresh scripts completed without prompts or manual keypresses
- [ ] Version number updated in `CMakeLists.txt`
- [ ] `vcpkg.json` `version-string` matches `CMakeLists.txt`
- [ ] `vcpkg.json` `builtin-baseline` is pinned to the intended vcpkg commit
- [ ] `CHANGELOG.md` has the release notes under the matching dated version
- [ ] `release-notes/vX.Y.Z.md` contains the GitHub release description
- [ ] `release-notes/` exists in the checkout and the file name matches the pushed tag
- [ ] Active documentation matches the release behavior, including the README, API, architecture, settings, specification, manifest, coding standards, and release guides
- [ ] Installer was rebuilt from the current `CMakeLists.txt` version (`python build_release.py` or `makensis /DAPP_VERSION=...`), not manually renamed afterward
- [ ] Release build completed successfully (`python build_release.py`)
- [ ] Headless Qt tests passed (`python .\run_headless_tests.py --build-dir build --config Release`)
- [ ] NSIS installer tested (install/uninstall preserves `%LOCALAPPDATA%\LzyDownloader\settings.ini`, `download_archive.db`, `downloads_backup.json`, and log files)
- [ ] NSIS installer finish-page launch option starts `LzyDownloader.exe` when left checked and does not start it when cleared
- [ ] Clean Windows install tested for HTTPS update checks (Qt TLS backend loads with `libcrypto-3-x64.dll` and `libssl-3-x64.dll` beside `LzyDownloader.exe`)
- [ ] Application update tested with active and queued downloads; queue state is saved and downloader/helper processes are stopped before installer launch
- [ ] Silent application update verified to relaunch the freshly installed `LzyDownloader.exe` after NSIS completes
- [ ] Intel and Apple Silicon DMGs mount and launch with deployed Qt plugins and the SQLite driver
- [ ] macOS updater selects only the matching architecture DMG and opens it in Finder for installation
- [ ] Timestamped logging verified (`%LOCALAPPDATA%\LzyDownloader\LzyDownloader_YYYY-MM-dd_HH-mm-ss.log`)
- [ ] Log retention verified (startup cleanup keeps only the 5 most recent logs)
- [ ] Temporary-root reconciliation verified (orphan UUID folders are removed asynchronously while stopped/failed IDs, symlinks, non-UUID folders, and the shared root are preserved)
- [ ] aria2c recovery verified (transient exit codes or a missing expected temporary media output fall back once to native yt-dlp, stale `.info.json` sidecars are removed, and `.part` files remain)
- [ ] GitHub release published with installer asset
- [ ] Tag `vX.X.X` pushed and the `Build and Release` GitHub Actions workflow attached Windows, Linux, Intel macOS, and Apple Silicon macOS assets

## Application Data Locations (Windows)

The application stores user data in standard Windows directories:

| File | Location |
|------|----------|
| Settings | `%LOCALAPPDATA%\LzyDownloader\settings.ini` |
| Archive | `%LOCALAPPDATA%\LzyDownloader\download_archive.db` |
| Queue Backup | `%LOCALAPPDATA%\LzyDownloader\downloads_backup.json` |
| Local API token | `%LOCALAPPDATA%\LzyDownloader\api_token.txt` (`Server\api_token.txt` for server/headless/background runtime) |

The Local API also accepts `override_archive: true` on intentional automation re-downloads; verify the Discord bridge and C++ executable are updated together so this confirmation reaches the queue manager.
| Logs | `%LOCALAPPDATA%\LzyDownloader\LzyDownloader_YYYY-MM-dd_HH-mm-ss.log` (one new file per run; oldest logs deleted after the most recent 5) |

Server/headless mode still reads user preferences from `%LOCALAPPDATA%\LzyDownloader\settings.ini`, but isolates runtime queue backups, API tokens, and logs under `%LOCALAPPDATA%\LzyDownloader\Server\`.

**Important:** The NSIS installer must NOT overwrite `settings.ini`, `download_archive.db`, `downloads_backup.json`, `api_token.txt`, or log files. These are stored in user data directories, not the installation directory.

### Application Data Locations (Linux)

On Linux, user configuration, databases, and downloads follow the XDG Base Directory specification:

| File | Location |
|------|----------|
| Settings | `~/.local/share/LzyDownloader/settings.ini` |
| Archive | `~/.local/share/LzyDownloader/download_archive.db` |
| Queue Backup | `~/.local/share/LzyDownloader/downloads_backup.json` |
| Logs | `~/.local/share/LzyDownloader/LzyDownloader_YYYY-MM-dd_HH-mm-ss.log` |
