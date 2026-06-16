# LzyDownloader C++ Update & Release Guide

This document describes how to build, package, and release the C++ version of LzyDownloader with auto-update support.

## Prerequisites

1. **NSIS (Nullsoft Scriptable Install System)**
   - Download from: https://nsis.sourceforge.io/Download
   - Install to default location (e.g., `C:\Program Files (x86)\NSIS`)
   - Verify: `makensis /version` in PowerShell

2. **CMake & MSVC**
   - Required to build the C++ application. GitHub Actions uses the hosted Windows MSVC environment; local Windows builds should use an MSVC toolchain compatible with Qt 6.

3. **Qt 6**
   - Required for building the application.
   - The manifest build path uses vcpkg; keep `vcpkg.json` synchronized with the app version before release and keep its `builtin-baseline` pinned for reproducible dependency resolution.

4. **Git & GitHub**
   - Repo: https://github.com/vincentwetzel/lzy-downloader
   - Must have access to create Releases

## Build Process

### Step 1: Update Extractor Lists

**IMPORTANT:** Before building a new release, you must refresh the extractor lists to ensure the application can handle the latest website changes.

Run the following Python scripts from the project root:
```powershell
python ./update_yt-dlp_extractors.py
python ./update_gallery-dl_extractors.py
```
This will update `extractors_yt-dlp.json` and `extractors_gallery-dl.json`. Both scripts share `extractor_utils.py` for domain parsing with precompiled regexes, are intentionally non-interactive, and should return directly to the shell when they finish.

### Step 2: Update Version Number

Update the version in `CMakeLists.txt` (`project(VERSION x.y.z)`). This is the single source of truth for the release version. The app version is generated from there into `version.h`, used by the Windows resources, and passed into the NSIS installer build by `build_release.ps1`.

Also update `vcpkg.json` `version-string` to the same version, keep its `builtin-baseline` pinned to the intended vcpkg commit, and ensure `CHANGELOG.md` has the release notes under the matching dated version heading.

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
- Builds `LzyDownloader.exe`
- On Windows, runs `makensis` against `LzyDownloader.nsi` with `/DAPP_VERSION=<version from CMakeLists.txt>` and `/DRELEASE_BUILD_DIR=build-release\Release`
- On Linux, packages `LzyDownloader-<version>-x86_64.AppImage` with `linuxdeploy`

### Step 3b: Run Headless Tests

Before packaging or publishing, run the Qt test suite through the headless helper:

```powershell
python .\run_headless_tests.py --build-dir build --config Release
```

The helper builds the selected configuration and runs `ctest` with `QT_QPA_PLATFORM=offscreen` and parallel jobs based on the host CPU count, covering core argument building, archive/config/API/process utilities, URL validation, sorting, UI progress widgets, and the local end-to-end fixture.

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

GitHub Actions automatically builds release assets when a `v*` tag is pushed. The workflow at `.github/workflows/release.yml` runs `python build_release.py` on `windows-latest` and `ubuntu-22.04`, then uploads `LzyDownloader-Setup-*.exe` and `LzyDownloader-*-x86_64.AppImage` to the GitHub Release for that tag.

### Step 1: Commit Release Inputs

Before tagging, commit the synchronized release inputs:

```powershell
git add CMakeLists.txt vcpkg.json CHANGELOG.md UPDATE_AND_RELEASE.md docs/SPEC.md docs/ARCHITECTURE.md AGENTS.md TODO.md .github/workflows/release.yml build_release.py LzyDownloader.nsi src/ui/LzyDownloader.desktop extractors_yt-dlp.json extractors_gallery-dl.json
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

### Step 3: Manual GitHub Release Fallback

If the workflow is unavailable, navigate to https://github.com/vincentwetzel/lzy-downloader/releases and:

1. Click "Create a new release"
2. **Tag version:** `vX.X.X` (must match Git tag)
3. **Release title:** `LzyDownloader X.X.X`
4. **Description:** Add release notes.
5. **Attach Assets:** Upload `LzyDownloader-Setup-X.X.X.exe`
   - Also attach `LzyDownloader-X.X.X-x86_64.AppImage` for Linux systems.
6. Click "Publish release"

## Release Checklist

- [ ] Extractor lists updated (`extractors_yt-dlp.json`, `extractors_gallery-dl.json`)
- [ ] Extractor refresh scripts completed without prompts or manual keypresses
- [ ] Version number updated in `CMakeLists.txt`
- [ ] `vcpkg.json` `version-string` matches `CMakeLists.txt`
- [ ] `vcpkg.json` `builtin-baseline` is pinned to the intended vcpkg commit
- [ ] `CHANGELOG.md` has the release notes under the matching dated version
- [ ] Installer was rebuilt from the current `CMakeLists.txt` version (`python build_release.py` or `makensis /DAPP_VERSION=...`), not manually renamed afterward
- [ ] Release build completed successfully (`python build_release.py`)
- [ ] Headless Qt tests passed (`python .\run_headless_tests.py --build-dir build --config Release`)
- [ ] NSIS installer tested (install/uninstall preserves `%LOCALAPPDATA%\LzyDownloader\settings.ini`, `download_archive.db`, `downloads_backup.json`, and log files)
- [ ] Clean Windows install tested for HTTPS update checks (Qt TLS backend loads with `libcrypto-3-x64.dll` and `libssl-3-x64.dll` beside `LzyDownloader.exe`)
- [ ] Timestamped logging verified (`%LOCALAPPDATA%\LzyDownloader\LzyDownloader_YYYY-MM-dd_HH-mm-ss.log`)
- [ ] Log retention verified (startup cleanup keeps only the 5 most recent logs)
- [ ] GitHub release published with installer asset
- [ ] Tag `vX.X.X` pushed and the `Build and Release` GitHub Actions workflow attached Windows and Linux assets

## Application Data Locations (Windows)

The application stores user data in standard Windows directories:

| File | Location |
|------|----------|
| Settings | `%LOCALAPPDATA%\LzyDownloader\settings.ini` |
| Archive | `%LOCALAPPDATA%\LzyDownloader\download_archive.db` |
| Queue Backup | `%LOCALAPPDATA%\LzyDownloader\downloads_backup.json` |
| Local API token | `%LOCALAPPDATA%\LzyDownloader\api_token.txt` (`Server\api_token.txt` for server/headless/background runtime) |
| Logs | `%LOCALAPPDATA%\LzyDownloader\LzyDownloader_YYYY-MM-dd_HH-mm-ss.log` (one new file per run; oldest logs deleted after the most recent 5) |

Server/headless mode still reads user preferences from `%LOCALAPPDATA%\LzyDownloader\settings.ini`, but isolates runtime queue backups, API tokens, and logs under `%LOCALAPPDATA%\LzyDownloader\Server\`.

**Important:** The NSIS installer must NOT overwrite `settings.ini`, `download_archive.db`, `downloads_backup.json`, `api_token.txt`, or log files. These are stored in user data directories, not the installation directory.

### Application Data Locations (Linux)

On Linux, user configuration, databases, and downloads follow the XDG Base Directory specification:

| File | Location |
|------|----------|
| Settings | `~/.config/LzyDownloader/settings.ini` |
| Archive | `~/.local/share/LzyDownloader/download_archive.db` |
| Queue Backup | `~/.local/share/LzyDownloader/downloads_backup.json` |
| Logs | `~/.local/share/LzyDownloader/LzyDownloader_YYYY-MM-dd_HH-mm-ss.log` |
