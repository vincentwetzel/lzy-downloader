# LzyDownloader C++ Port TODO

## In Progress
- *(All active milestones are complete. See planned features below or check the issue tracker.)*

## Planned / Future Enhancements
- [ ] Implement translations for supported interface languages (see `LANGUAGES.md`).

## Completed

### Phase 19: Comprehensive Testing Suite
- Static Library Architecture (`LzyAppLib`)
- Test Environment Isolation (mocking settings and archive database)
- Unit Tests (ArgsBuilder, Progress Parsing, Sorting, URL Normalization)
- Integration & GUI Tests (Headless widgets, End-to-End downloads)

### Phase 18: Local API Server & Discord Bridge
- Local API Server implementation (`QTcpServer` on port 8765)
- Headless Environment Isolation (`Server/` app-data subfolder for logs, state, and tokens)
- Discord Bridge Webhook (HTTP POST payloads to local port 8766)
- Discord queue-position payloads for queued download ordering

### Phase 17: Download Sections
- Download Sections Support (time range and chapter-based downloading)

### Phase 16: Settings Architecture
- Simplified `ConfigManager`, removed canonicalization loops and legacy fallbacks
- Streamlined `get()`/`set()`, preserved factory defaults and safe reset

### Phase 15: UI/UX Enhancements
- Toggle switch visual fix, smart URL download type detection
- Force Playlist as Single Album, Metadata Options (square crop, folder.jpg)
- Clear Inactive Downloads, Resume All toolbar action, Open folder buttons
- External Downloader dropdown

### Phase 14: Unbundled Binaries & Dependency Management
- Fully transitioned to unbundled binaries (`yt-dlp`, `ffmpeg`, `ffprobe`, `gallery-dl`, `aria2c`, `deno`)
- Removed `bin/` directory and bundled executable fallbacks
- Binary path caching and flexible progress parsing

### Phase 13: Future Enhancements (Implemented)
- Cancel/retry buttons, runtime format selection
- Backup download queue, clear inactive downloads, cancel all downloads
- Pause/resume downloads, download queue ordering
- Subtitle selection improvements, split output templates
- `--split-chapters` support, geo-verification proxy

### Phase 11-12: Stability & Deployment
- NSIS installer, UI enhancements, advanced settings reorganization
- Restore defaults safety, cancel downloads in queue, download stats display
- Qt plugin deployment reliability, thumbnail plugin deployment
- Unicode filename move reliability, canonical `settings.ini` cleanup
- Extractor domain list maintenance, thumbnail previews, Qt SDK auto-discovery

### Core Features (Phases 1-10)
- UI, download logic, settings, archive, playlists, sorting, advanced settings
- Global download speed indicator, video quality warning, clean display title
- Retry/resume signals, single instance enforcement

### Error Handling, Prompts & Validation
- Private, Unavailable, Geo-restricted, Members-only, and Age-restricted video error popups
- Supported Sites UI searchable dialog
- Interactive Livestream wait prompts and settings (`live-from-start`, `wait-for-video`, etc.)
- Live Chat transcript downloading

*(Note: Hundreds of specific bug fixes and minor improvements have been completed and are documented in `CHANGELOG.md`.)*
