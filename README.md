# LzyDownloader (C++ Port)

A lightweight, high-performance desktop application for downloading media (video and audio) from online platforms using **yt-dlp**.

**This is a C++ port of the original Python application.** It is designed as a faster native replacement with lower memory usage and a Qt-based settings format. Download history remains portable through the shared `download_archive.db`.

## Features

- 🎬 **Download Video & Audio** — Support for YouTube, TikTok, Instagram, and 1000+ other sites via yt-dlp
- 🎵 **Audio Extraction** — Extract audio as MP3, M4A, opus, or other formats
- 📋 **Playlist Support** — Download entire playlists with configurable behavior
- 🖼️ **Gallery Support** — Download image galleries from supported sites (e.g., Instagram, Twitter) via `gallery-dl`
- 🎨 **Advanced Settings** — Quality selection, format filtering, SponsorBlock integration, metadata embedding
- 🎛️ **Runtime Format Selection** — Optionally prompt for specific video/audio qualities on every download, supporting multiple simultaneous format selections for the same media
- 🔄 **App Updates** — Checks GitHub Releases for newer installers and prompts before downloading
- 🔌 **Local API** — Optional localhost API for trusted local integrations such as Discord bots
- 📊 **Concurrent Downloads** — Queue and manage multiple downloads simultaneously
- ⏸️ **Pause & Resume** — Safely stop downloads, preserve partial `.part` files, and resume them across application restarts
- 🧰 **External Binaries Manager** — Detect, version-check, install, and update `yt-dlp`, `gallery-dl`, `ffmpeg`, `ffprobe`, `aria2c`, and `deno` from inside the app
- 🖼️ **Thumbnail Embedding** — Automatic thumbnail download and embedding for videos and audio
- 🌐 **Browser Cookies** — Use saved cookies from Firefox, Chrome, Edge, or other browsers for age-restricted content
- 📂 **Smart Sorting** — Automatically organize downloads into subfolders based on uploader, playlist, date, or custom patterns

## Installation

### Windows (Recommended)

Download the latest installer from [Releases](https://github.com/vincentwetzel/LzyDownloader/releases):

1. Download `LzyDownloader-Setup-X.X.X.exe`
2. Run the installer
3. Launch from Start Menu or desktop shortcut

**Note for existing users:** The installer can replace the Python version of LzyDownloader. Download history is preserved through `download_archive.db`; settings use the C++ app's Qt-native `settings.ini` layout and may need to be regenerated.

### From Source

Requires CMake, a C++20 compatible compiler (MSVC recommended on Windows), and Qt 6.

The repository now includes a `vcpkg.json` manifest for source builds. On Windows, the checked-in `CMakePresets.json` expects the vcpkg toolchain at `E:/vcpkg/scripts/buildsystems/vcpkg.cmake`. If your local vcpkg checkout lives somewhere else, either adjust the preset or pass your own `-DCMAKE_TOOLCHAIN_FILE=...` path when configuring.

```bash
# Clone the repo
git clone https://github.com/vincentwetzel/LzyDownloader.git
cd LzyDownloader

# Configure and build with the checked-in preset
cmake --preset release
cmake --build build --config Release
```

Example manual configure command:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=E:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

### Release Checklist

Before building a release, keep all release metadata in sync:

- `CMakeLists.txt` `project(VERSION x.y.z)` is the app version source of truth.
- `vcpkg.json` `version-string` must be updated to the same version.
- `LzyDownloader.nsi` must not contain stale hardcoded version examples or installer metadata; pass the release version with `makensis /DAPP_VERSION=x.y.z LzyDownloader.nsi`.
- `CHANGELOG.md` must move `[Unreleased]` notes under the dated release version.

## Usage

1. **Launch the app** (`LzyDownloader.exe`)
2. **Enter a URL** in the "Start Download" tab
3. **Configure options** (quality, format, SponsorBlock, etc.)
4. **Click Download** to start
5. **Monitor progress** in the "Active Downloads" tab
6. **Find completed files** in your configured output folder

## Configuration

All settings are saved to `%LOCALAPPDATA%\LzyDownloader\settings.ini` on Windows and persist between sessions. GUI and `--server`/`--headless` launches share this same preferences file, so folders, binary paths, templates, cookies, codecs, and related choices stay in sync. The C++ port uses a Qt-native `QSettings` INI layout rather than matching Python `configparser` quirks, so existing users may regenerate settings as needed. Download history remains shared through `download_archive.db`.

- **Output folder** — Where completed downloads are saved
- **Temporary folder** — Where downloads are cached during progress
- **Quality/Format** — Video/audio codec and quality preferences
- **Metadata** — Embed titles, artists, and thumbnails
- **SponsorBlock** — Automatically skip sponsored segments
- **Browser Cookies** — Select a browser to use for authentication
- **Local API** — Enable a localhost-only API server from Advanced Settings -> Configuration

### Local API

When enabled in the GUI, or when launched with `--server`/`--headless`, LzyDownloader listens only on `127.0.0.1:8765`. The API token is stored in the app-local data directory as `api_token.txt`; server/headless mode keeps its runtime token under `Server/api_token.txt`. Requests must send the token as a Bearer token.

- `POST /enqueue` with JSON body `{"url":"https://...","type":"video"}` queues a download using non-interactive defaults. `type` is optional and may be `video`, `audio`, or `gallery`; omitted requests default to `video`.
- `GET /status` returns current tracked jobs, including progress fields when available.

Automation can also launch `LzyDownloader.exe --background <url>` or `LzyDownloader.exe --server <url>` to enqueue a direct URL without showing blocking prompt dialogs. Server/headless queue backups, API tokens, and logs are isolated under `Server/`, but user preferences still come from the main `settings.ini`.

## Architecture

The application is built using **C++20** and the **Qt 6** framework.

```
LzyDownloader/
├── CMakeLists.txt              # Build System Configuration
├── main.cpp                    # Application Entry Point
├── src/
│   ├── core/                   # Core Business Logic
│   │   ├── ConfigManager.h/cpp   # Settings persistence (INI)
│   │   ├── ArchiveManager.h/cpp  # Download history (SQLite)
│   │   ├── DownloadQueueState.h/cpp # Manages persistence of download queue state
│   │   ├── DownloadManager.h/cpp # Queue & Lifecycle Management
│   │   ├── LocalApiServer.h/cpp  # localhost API for local integrations
│   │   ├── DownloadFinalizer.h/cpp # File Verification & Moving
│   │   ├── YtDlpWorker.h/cpp     # QProcess Wrapper & Parsing
│   │   └── ...
│   ├── ui/                     # User Interface (Qt Widgets)
│   │   ├── MainWindow.h/cpp      # Main Window & Signal Hub
│   │   ├── MainWindowUiBuilder.h/cpp # Builds UI for MainWindow
│   │   ├── StartTab.h/cpp        # Input Tab (Orchestrates helper classes for URL handling, download actions, and command preview)
│   │   ├── StartTabUiBuilder.h/cpp # Builds UI for StartTab
│   │   ├── start_tab/
│   │   │   ├── StartTabDownloadActions.h/cpp # Handles download actions and format checking
│   │   │   ├── StartTabUrlHandler.h/cpp # Manages URL input and clipboard
│   │   │   └── StartTabCommandPreviewUpdater.h/cpp # Updates command preview
│   │   ├── ActiveDownloadsTab.h/cpp # Progress Tab
│   │   ├── advanced_settings/
│   │   │   ├── MetadataPage.h/cpp    # Metadata & Thumbnail configuration
│   │   │   └── ...
│   │   └── ...
│   └── utils/                  # Helper Modules
│       └── StringUtils.h/cpp   # String/URL utilities
```

**Note:** External binaries (yt-dlp, ffmpeg, ffprobe, gallery-dl, aria2c, deno) are not bundled with the application. Users must install them separately or configure paths in Advanced Settings.

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/your-feature`)
3. Commit your changes (`git commit -am 'Add feature'`)
4. Push to the branch (`git push origin feature/your-feature`)
5. Open a Pull Request

## License

This project is
