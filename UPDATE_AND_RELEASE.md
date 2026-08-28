# LzyDownloader C++ Update & Release Guide

The main-window footer keeps status counters and current speed on its first row;
the exit-after-downloads control remains the rightmost item.

The External Binaries install dialog keeps long package-manager and PowerShell
command previews wrapped inside a bounded text area so narrow windows do not
grow wider than the screen.

Startup missing-tool and outdated-binary notices are consolidated into the
theme-aware **Set Up Required Tools** checklist. It distinguishes new
app-managed installs from updates in existing locations and provides a single
**Update All** action for supported automatic operations.

Windows standalone FFmpeg/FFprobe updates stage both executables before bounded
replacement retries, then persist the local pair as explicit overrides so a
system-first preference cannot silently switch back to an older package copy.
WinGet-managed paths are updated through `winget upgrade`, except that a stale
WinGet Deno catalog falls back to the official stable app-managed installer.
Standalone yt-dlp, gallery-dl, and Deno use their own updater where supported;
standalone FFmpeg/FFprobe and aria2c remain manual-replacement cases.

The Discord bridge cancellation route is a runtime Local API feature and does not change release packaging or installer requirements.

Active downloads use native OS sleep inhibition in the GUI and
headless/server/background builds. Linux builds link Qt D-Bus (enabled by
qtbase's Linux-only `dbus`
feature in the vcpkg manifest) for the logind/freedesktop inhibition services;
this is a runtime platform integration, not a newly bundled downloader
dependency.

The Local API cancellation endpoint, terminal webhook diagnostics, and
stopped/failed duplicate recovery are runtime contracts only. They require no
additional installer payloads, ports, or release-time configuration beyond the
existing localhost API/webhook integration.

This document describes how to build, package, and release the C++ version of LzyDownloader with auto-update support.

For coding-task documentation routing, start with `AGENTS.md`; load the
release details here only when changing build or packaging behavior.

Release smoke tests should also verify that a stale manually managed yt-dlp
executable produces an exact installed/latest update prompt.

## Public repository discoverability

The repository README is the primary public landing page. Keep its first
paragraph, release link, feature descriptions, platform support, and FAQ
accurate because they explain the project to both prospective users and search
engines. Repository settings should also use the following concise description:

> Free desktop video, audio, playlist, and gallery downloader powered by yt-dlp and gallery-dl.

Recommended GitHub topics are `video-downloader`, `audio-downloader`,
`playlist-downloader`, `gallery-downloader`, `yt-dlp`, `yt-dlp-gui`, `ffmpeg`,
`qt`, `cpp`, `windows`, `linux`, and `macos`. Apply these in the repository's
GitHub Settings; topics are repository metadata and cannot be set by a commit
in this checkout. Avoid keyword stuffing or claiming support that the current
yt-dlp/gallery-dl extractors do not provide.

Release titles and notes should state the user-visible value of each build and
link to the latest installer. Keep the Windows, Linux, and macOS asset names
descriptive and architecture-specific so search visitors can identify the
correct download quickly.

The approved branded preview is stored at `docs/assets/social-preview.png`.
Upload it manually in GitHub repository Settings -> Social preview; GitHub does
not automatically use an image merely because it exists in the repository.

## Prerequisites

1. **NSIS (Nullsoft Scriptable Install System)**
   - Download from: https://nsis.sourceforge.io/Download
   - Install to default location (e.g., `C:\Program Files (x86)\NSIS`)
   - Verify: `makensis /version` in PowerShell
   - GitHub Actions installs NSIS automatically on the Windows runner before invoking `build_release.py`.

2. **CMake & MSVC**
   - Required to build the C++ application. GitHub Actions uses the hosted Windows MSVC environment; local Windows builds should use an MSVC toolchain compatible with Qt 6.

3. **Qt 6**
   - Required for building the application. Release CI installs the pinned
     prebuilt Qt 6.10.2 desktop SDK with `jurplel/install-qt-action`, using
     `win64_msvc2022_64` on Windows, `linux_gcc_64` on Linux, and `clang_64` on macOS.
   - Qt setup runs before Python dependency setup because the Qt action manages
     its own interpreter internally. The workflow restores Python 3.11 and
     installs `yt-dlp` and `gallery-dl` with `python -m pip` afterward.
   - Local/source builds may use the vcpkg manifest; keep `vcpkg.json`
     synchronized with the app version before release and keep its
     `builtin-baseline` pinned for reproducible dependency resolution.

4. **Linux source-build packages (optional)**
   - The tag workflow uses the prebuilt Qt SDK and does not compile Qt or
     require the vcpkg Qt/XCB host packages. If vcpkg is used for a local
     Linux source build, install the compiler, Qt/XCB development, and archive
     packages required by that vcpkg revision.

5. **macOS Qt deployment (macOS release builds)**
   - GitHub Actions uses Qt 6's universal `clang_64` desktop package on both macOS runners. The archive contains x86_64 and arm64 support; each native runner produces its architecture-specific build and DMG.
   - `build_release.py` creates a native `.app` bundle, runs `macdeployqt`, embeds an ICNS icon, and packages a separate architecture-labelled DMG. These are unsigned unless a future signing/notarization workflow is configured.
   - The helper is native-only. Use `python build_release.py --target macos` inside the macOS VM to select the macOS path explicitly; `--target auto` remains the default and cross-OS targets are rejected.

6. **yt-dlp Nightly (release validation)**
   - Release automation installs the latest yt-dlp prerelease with `python -m pip install --pre --upgrade yt-dlp`; this keeps packaging validation aligned with current extractor/runtime changes. Verify low-quality warnings show title and source-link context in GUI smoke checks.
   - This is a build-validation dependency only. yt-dlp is not bundled by the release job; runtime executable provisioning remains governed by External Tools settings.

7. **Git & GitHub**
   - Repo: https://github.com/vincentwetzel/lzy-downloader
   - Must have access to create Releases

## Build Process

This project uses GitHub Actions as the normal release build environment. A
release-preparation task updates metadata, refreshes extractor lists, writes
the changelog and matching release notes, and prepares the commit/tag commands.
Do not run `build_release.py` locally as part of the normal release workflow;
the pushed `vX.Y.Z` tag starts the matrix build on GitHub Actions. Local builds
and headless tests are optional diagnostics only and should be run only when
explicitly requested.

Before preparing a release, review `CHANGELOG.md` and the maintained docs against the
implementation. Use `docs/SPEC.md` as the smoke-test contract, especially for
playlist fallback, cookie/aria2c recovery, media diagnostics, normalized
duplicate identity, replacement safety, audio-aware progress, thumbnail
remuxing, and compact one-bar rows. Also verify quiet native transfers recover
sizes from `formats`/`.part` data and Discord aggregate progress survives
video/audio handoff.

### Step 1: Update Extractor Lists

**IMPORTANT:** Before building a new release, you must refresh the extractor lists to ensure the application can handle the latest website changes.

Run the following Python scripts from the project root:
```powershell
python ./tools/update_yt-dlp_extractors.py
python ./tools/update_gallery-dl_extractors.py
```
This will update `extractors_yt-dlp.json` and `extractors_gallery-dl.json`. Both scripts share `tools/extractor_utils.py` for domain parsing with precompiled regexes, are intentionally non-interactive, and should return directly to the shell when they finish.

### Step 2: Update Version Number

Update the version in `CMakeLists.txt` (`project(VERSION x.y.z)`) to a value newer than the latest `vX.Y.Z` Git tag. This is the single source of truth for the release version. The app version is generated from there into `version.h`, used by the Windows resources, and passed into the NSIS installer build by `build_release.py`.

Also update `vcpkg.json` `version-string` to the same version, keep its `builtin-baseline` pinned to the intended vcpkg commit, and ensure `CHANGELOG.md` has the release notes under the matching dated version heading.
The matching GitHub release body belongs in `release-notes/vX.Y.Z.md`; create
that file before tagging because the tag-triggered workflow attaches it
automatically.

`build_release.py` rejects a local release version that is not newer than the
newest semantic `v*` tag and rejects tag builds whose tag does not exactly
match CMake. Manual `workflow_dispatch` validation skips the monotonicity
check. To intentionally rebuild an existing release, set
`LZY_ALLOW_VERSION_REBUILD=1` and document why the rebuild is needed.

**Release rule:** Do not manually rename the installer `.exe` to fix a version mismatch. If the setup filename version is wrong, fix the release inputs/scripts and rebuild so the installer filename, Windows app version, and uninstall `DisplayVersion` all match the same `CMakeLists.txt` version.

### Step 3: GitHub Actions builds the release

Push the synchronized release commit and then its matching annotated tag as
described in [Release to GitHub](#release-to-github). The tag-triggered
workflow invokes `build_release.py` on GitHub-hosted Windows, Linux, Intel
macOS, and Apple Silicon macOS runners.

Windows and Linux release jobs install the same pinned prebuilt Qt 6.10.2 SDK
model used by macOS. This removes the source Qt build that previously consumed
most of the Windows job, avoids duplicate Debug dependency builds, and keeps
the release toolchain independent of vcpkg's removed `x-gha` cache backend.
Linux uses aqtinstall's explicit `linux_gcc_64` host architecture. This matches
the current Qt Linux repository metadata and avoids the obsolete `gcc_64` alias
being resolved as the unavailable `qt_base` package.

On each runner, the workflow:
- Deletes the existing `build-release/` directory to avoid stale DLL mismatches
- Refreshes both extractor JSON files
- Configures a Release build with CMake
- Builds the platform-native `LzyDownloader` executable and the Windows
  `LzyDownloaderBrowserHost` native-messaging host (and `LzyDownloader.app` on
  macOS)
- On Windows, runs `makensis` from `PATH` when available, otherwise the standard NSIS installation path, against `LzyDownloader.nsi` with `/DAPP_VERSION=<version from CMakeLists.txt>` and `/DRELEASE_BUILD_DIR=build-release\Release`. If the repository variable `LZY_BROWSER_EXTENSION_ID` is configured with the final 32-character Store ID, the release builder passes it through for exact native-host registration; if unset, registration remains disabled.
- The Windows installer finish page offers a checked-by-default option to launch `LzyDownloader.exe` after installation
- On Linux, stages a clean `build-release/AppDir`, caches linuxdeploy and its Qt plugin under `build-release/tooling/`, generates a linuxdeploy desktop file whose `Icon` matches the resized release PNG, and packages `build-release/LzyDownloader-<version>-x86_64.AppImage`
- On Linux, selects qmake from the Qt SDK that built the executable so
  linuxdeploy discovers the same Qt modules; the QtSql SQLite driver is
  included through the explicit QtSql module.
- On Linux, deploys the dynamic Qt libraries and SQLite plugin with
  linuxdeploy-plugin-qt; unused Qt SQL drivers are temporarily moved outside
  the scan so optional drivers cannot break the SQLite-only bundle.
  Platform-service libraries such as D-Bus are excluded from linuxdeploy's ELF
  scan.
- On macOS, runs `macdeployqt`, converts the release PNG into the bundle's ICNS icon, and emits `LzyDownloader-<version>-macos-x86_64.dmg` or `LzyDownloader-<version>-macos-arm64.dmg` according to the runner architecture.
- Windows and Linux package only the `LzyDownloader` application; the browser
  host and separate headless test targets remain part of the build graph or
  optional local/test workflows as applicable.
- The browser host is included in Windows build output but is not registered by
  the installer until the production Chrome Web Store extension ID is supplied
  as release configuration. It must never be registered with a wildcard origin.
- Verify browser-companion protocol and registration changes against the
  separate browser-extension checkout before publishing. Local registration is
  for development only; the installer must receive the final 32-character
  extension ID through `LZY_BROWSER_EXTENSION_ID`.
- Qt SDK installation uses the action cache, and Linux release compilation uses
  a persistent ccache keyed by runner/toolchain inputs. Linux also selects Ninja
  when available to reduce build-graph overhead.

### Optional local validation

Local packaging is not required for a release. If a local build or test run is
specifically requested, use the repository tools below; their results do not
replace the tag-triggered GitHub Actions build.

To run the Qt suite locally:

```powershell
python .\tests\run_headless_tests.py --build-dir build --config Release
```

The helper builds before CTest and stops on compilation failure, then runs
headless tests with Qt's `minimal` platform plugin in parallel with timestamped
output and a final summary. Windows test targets receive the platform and Qt
runtime DLLs through the same guarded deployment helper used by the application.
It stores failed names in `build/.lzy-test-suspects.json`; use `--suspects` to rerun
that cache. Test locations and coverage are indexed in
`docs/FILE_MANIFEST.md` and `docs/SPEC.md`.

To package locally for troubleshooting only:

Use the canonical helper:
```powershell
python .\build_release.py
```

Do not manually rename generated installers. Manual CMake/NSIS commands are
only for diagnosing a packaging problem and are not the release procedure.

`CMakeLists.txt` already runs `windeployqt`, re-copies the resolved Qt runtime
DLLs from the configured Qt installation, deploys the OpenSSL runtime DLLs
(`libcrypto-3-x64.dll`, `libssl-3-x64.dll`) when available, and copies the
MinGW runtime DLLs when using the MinGW toolchain. Keep the deployed
compression/runtime dependencies that Qt ships with, including `zlib1.dll`,
because `Qt6Network.dll` depends on them on Windows.

For Windows development builds, the checked-in `release` and `debug` presets
explicitly select the repository's expected Qt MinGW and Ninja locations.
Update those preset paths when using another Qt installation. Qt plugin copying is performed by
`cmake/deploy_openssl_runtime.cmake` under a per-runtime-directory lock so
parallel builds do not partially overwrite the deployed plugin tree. The
vcpkg manifest enables the specific Qt modules required by the application and
tests rather than Qt's default feature bundle.

## Release to GitHub

GitHub Actions automatically builds release assets when a `v*` tag is pushed. The workflow at `.github/workflows/release.yml` runs `python build_release.py` on `windows-latest`, `ubuntu-22.04`, `macos-15-intel` (Intel), and `macos-15` (Apple Silicon), then uploads the Windows installer, Linux AppImage, and both architecture-labelled macOS DMGs to the GitHub Release for that tag. If the matching release-notes file is absent, CI creates a minimal fallback body before publication. Use `workflow_dispatch` to run the matrix as a non-publishing validation; uploads are tag-only.

### Repository directory guard

Run every release command from the C++ checkout, not the separate Discord-bot
checkout. Do not copy a local absolute path into this guide or into release
documentation. Before staging files, confirm the directory and repository
identity using repository-relative checks:

```powershell
git rev-parse --show-toplevel
Test-Path .\CMakeLists.txt
Test-Path .\.github\workflows\release.yml
```

The first command must print the current repository root, and both `Test-Path`
commands must return `True`. If `CMakeLists.txt` is missing, stop; do not run
`git add`, commit, or tag from that directory. The bot has its own Git history
and may already contain a tag with the same version number; tags must be
created in this C++ repository.

### Step 1: Commit Release Inputs

Before tagging, commit the synchronized release inputs:

```powershell
git add CMakeLists.txt vcpkg.json CHANGELOG.md README.md UPDATE_AND_RELEASE.md docs/ AGENTS.md TODO.md .github/workflows/release.yml build_release.py tools/ triplets/ LzyDownloader.nsi src/ui/LzyDownloader.desktop extractors_yt-dlp.json extractors_gallery-dl.json release-notes/vX.Y.Z.md
git commit -m "Release vX.X.X"
git push origin HEAD
```

The release-preparation handoff should stop after presenting these commands
unless the user explicitly asks for repository mutation. Do not create a tag
before the synchronized release commit is available on the remote.

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
- `SHA256SUMS-<platform>-<architecture>.txt`

### Step 3: Manual GitHub Release Fallback

If the workflow is unavailable, navigate to https://github.com/vincentwetzel/lzy-downloader/releases and:

1. Click "Create a new release"
2. **Tag version:** `vX.X.X` (must match Git tag)
3. **Release title:** `LzyDownloader X.X.X`
4. **Description:** Add release notes.
5. **Attach Assets:** Upload `LzyDownloader-Setup-X.X.X.exe`
   - Also attach `LzyDownloader-X.X.X-x86_64.AppImage` for Linux systems.
   - Attach the generated platform-specific `SHA256SUMS-*.txt` manifest and keep it alongside the matching release assets.
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
- [ ] SHA-256 manifest is attached for each release build job
- [ ] `release-notes/` exists in the checkout and the file name matches the pushed tag
- [ ] Active documentation matches the release behavior, including the README, API, architecture, settings, specification, manifest, coding standards, and release guides
- [ ] GitHub Actions rebuilt the installer from the current `CMakeLists.txt` version (the tag workflow runs `python build_release.py`); artifacts were not manually renamed
- [ ] Tag-triggered GitHub Actions release matrix completed successfully
- [ ] Headless Qt tests passed when run as an explicitly requested validation (local test execution is not required for the tag workflow)
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

Server/headless/background mode still reads user preferences from `%LOCALAPPDATA%\LzyDownloader\settings.ini`, but isolates runtime queue backups, API tokens, and logs under `%LOCALAPPDATA%\LzyDownloader\Server\`.

**Important:** The NSIS installer must NOT overwrite `settings.ini`, `download_archive.db`, `downloads_backup.json`, `api_token.txt`, or log files. These are stored in user data directories, not the installation directory.

### Application Data Locations (Linux)

On Linux, user configuration, databases, and downloads follow the XDG Base Directory specification:

| File | Location |
|------|----------|
| Settings | `~/.local/share/LzyDownloader/settings.ini` |
| Archive | `~/.local/share/LzyDownloader/download_archive.db` |
| Queue Backup | `~/.local/share/LzyDownloader/downloads_backup.json` |
| Logs | `~/.local/share/LzyDownloader/LzyDownloader_YYYY-MM-dd_HH-mm-ss.log` |
