# LzyDownloader C++ Update & Release Guide

This document describes how to build, package, and release the C++ version of LzyDownloader with auto-update support.

## Prerequisites

1. **NSIS (Nullsoft Scriptable Install System)**
   - Download from: https://nsis.sourceforge.io/Download
   - Install to default location (e.g., `C:\Program Files (x86)\NSIS`)
   - Verify: `makensis /version` in PowerShell

2. **CMake & MSVC**
   - Required to build the C++ application. The checked-in CMake presets currently target the Visual Studio 18 2026 generator.

3. **Qt 6**
   - Required for building the application.
   - The manifest build path uses vcpkg; keep `vcpkg.json` synchronized with the app version before release.

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
This will update `extractors_yt-dlp.json` and `extractors_gallery-dl.json`.

### Step 2: Update Version Number

Update the version in `CMakeLists.txt` (`project(VERSION x.y.z)`). This is the single source of truth for the release version. The app version is generated from there into `version.h`, used by the Windows resources, and passed into the NSIS installer build by `build_release.ps1`.

Also update `vcpkg.json` `version-string` to the same version and ensure `CHANGELOG.md` has the release notes under the matching dated version heading.

**Release rule:** Do not manually rename the installer `.exe` to fix a version mismatch. If the setup filename version is wrong, fix the release inputs/scripts and rebuild so the installer filename, Windows app version, and uninstall `DisplayVersion` all match the same `CMakeLists.txt` version.

### Step 3: Run the Release Builder

The preferred release path is the helper script:
```powershell
.\build_release.ps1
```

This script:
- Deletes the existing `build/` directory to avoid stale DLL mismatches
- Refreshes both extractor JSON files
- Configures a Release build with CMake
- Builds `LzyDownloader.exe`
- Runs `makensis` against `LzyDownloader.nsi` with `/DAPP_VERSION=<version from CMakeLists.txt>`

### Step 4: Manual Build Steps

If you are not using `build_release.ps1`, run the equivalent commands manually:
```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
& 'C:\Program Files (x86)\NSIS\makensis.exe' "/DAPP_VERSION=X.X.X" LzyDownloader.nsi
```

Replace `X.X.X` with the exact version from `CMakeLists.txt`.

`CMakeLists.txt` already runs `windeployqt`, re-copies the resolved Qt runtime DLLs from the configured Qt installation, and deploys the OpenSSL runtime DLLs (`libcrypto-3-x64.dll`, `libssl-3-x64.dll`) when available. Keep the deployed compression/runtime dependencies that Qt ships with, including `zlib1.dll`, because `Qt6Network.dll` depends on them on Windows.

## Release to GitHub

### Step 1: Create a Git Tag

```powershell
git tag -a vX.X.X -m "Release version X.X.X"
git push origin vX.X.X
```

### Step 2: Create GitHub Release

Navigate to https://github.com/vincentwetzel/lzy-downloader/releases and:

1. Click "Create a new release"
2. **Tag version:** `vX.X.X` (must match Git tag)
3. **Release title:** `LzyDownloader X.X.X`
4. **Description:** Add release notes.
5. **Attach Assets:** Upload `LzyDownloader-Setup-X.X.X.exe`
6. Click "Publish release"

## Release Checklist

- [ ] Extractor lists updated (`extractors_yt-dlp.json`, `extractors_gallery-dl.json`)
- [ ] Version number updated in `CMakeLists.txt`
- [ ] `vcpkg.json` `version-string` matches `CMakeLists.txt`
- [ ] `CHANGELOG.md` has the release notes under the matching dated version
- [ ] Installer was rebuilt from the current `CMakeLists.txt` version (`build_release.ps1` or `makensis /DAPP_VERSION=...`), not manually renamed afterward
- [ ] Release build completed (`build_release.ps1` or equivalent manual steps)
- [ ] NSIS installer tested (install/uninstall preserves `%LOCALAPPDATA%\LzyDownloader\settings.ini`, `download_archive.db`, `downloads_backup.json`, and log files)
- [ ] Clean Windows install tested for HTTPS update checks (Qt TLS backend loads with `libcrypto-3-x64.dll` and `libssl-3-x64.dll` beside `LzyDownloader.exe`)
- [ ] Timestamped logging verified (`%LOCALAPPDATA%\LzyDownloader\LzyDownloader_YYYY-MM-dd_HH-mm-ss.log`)
- [ ] Log retention verified (startup cleanup keeps only the 5 most recent logs)
- [ ] GitHub release published with installer asset

## Application Data Locations (Windows)

The application stores user data in standard Windows directories:

| File | Location |
|------|----------|
| Settings | `%LOCALAPPDATA%\LzyDownloader\settings.ini` |
| Archive | `%LOCALAPPDATA%\LzyDownloader\download_archive.db` |
| Queue Backup | `%LOCALAPPDATA%\LzyDownloader\downloads_backup.json` |
| Local API token | `%LOCALAPPDATA%\LzyDownloader\api_token.txt` |
| Logs | `%LOCALAPPDATA%\LzyDownloader\LzyDownloader_YYYY-MM-dd_HH-mm-ss.log` (one new file per run; oldest logs deleted after the most recent 5) |

Server/headless mode still reads user preferences from `%LOCALAPPDATA%\LzyDownloader\settings.ini`, but isolates runtime queue backups, API tokens, and logs under `%LOCALAPPDATA%\LzyDownloader\Server\`.

**Important:** The NSIS installer must NOT overwrite `settings.ini`, `download_archive.db`, `downloads_backup.json`, `api_token.txt`, or log files. These are stored in user data directories, not the installation directory.
