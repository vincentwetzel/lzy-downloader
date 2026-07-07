# LzyDownloader File Manifest

This document is a quick file-to-responsibility index for the C++ port. It is intentionally narrow in scope:
- `docs/FILE_MANIFEST.md` answers "where does this live?"
- `docs/ARCHITECTURE.md` answers "how does it work?"
- `docs/SPEC.md` answers "what must it do?"

## Root Files
- `README.md`: Project overview, build, usage, configuration, and release checklist.
- `AGENTS.md`: Canonical operating instructions for AI contributors.
- `CHANGELOG.md`: User-facing release history and notable completed changes.
- `TODO.md`: Active maintenance items, planned work, and known gaps.
- `UPDATE_AND_RELEASE.md`: Release/operator workflow reference.
- `CMakeLists.txt`: Primary build graph and executable/library registration.
- `build_release.py`: Release build and packaging orchestration.
- `main.cpp`: Application entry point and single-instance/bootstrap wiring.

## Documentation
- `docs/ARCHITECTURE.md`: System design, component responsibilities, and data flow.
- `docs/SPEC.md`: Functional requirements, behavior guarantees, and product rules.
- `docs/SETTINGS.md`: Settings schema, keys, groups, and validation rules.
- `docs/CODING_STANDARDS.md`: Contribution and implementation standards.
- `docs/LANGUAGES.md`: Planned translation coverage.
- `docs/CHANGELOG_ARCHIVE.md`: Historical changelog entries preserved for reference.

## Source Layout
- `src/core/`: Download pipeline, queueing, archive handling, configuration, binary resolution, and supporting logic.
- `src/ui/`: Qt Widgets UI, tabs, dialogs, and presentation-layer builders.
- `src/utils/`: Shared helpers for logging, discovery, parsing, and platform utilities.
- `src/tests/`: Test fixtures and Qt test coverage.

## High-Value Entry Points
- `src/core/DownloadManager.*`: Queue orchestration, download lifecycle, and finalization flow.
- `src/core/YtDlpWorker.*`: yt-dlp process handling, output parsing, and progress classification.
- `src/core/DownloadQueueState.*`: Queue persistence to `downloads_backup.json`.
- `src/core/ConfigManager.*`: `settings.ini` persistence and validation.
- `src/core/ProcessUtils.*`: Process helpers, binary discovery, and termination utilities.
- `src/core/SmartBinaryResolver.*`: External binary path resolution and auto-detected path ownership.
- `src/core/PlaylistExpansionWorker.*`: Playlist validation and expansion.
- `src/core/PlaylistExpansionParser.*`: yt-dlp JSON expansion parsing and playlist item selection.
- `src/core/YtDlpArgsBuilder.*`: Command-line argument construction for yt-dlp and aria2c.
- `src/core/AppUpdater.*`: Application update lookup and artifact handling.
- `src/core/LocalApiServer.*`: Localhost API server and auth handling.
- `src/ui/MainWindow.*`: Main application shell, tab wiring, and global UI actions.
- `src/ui/StartTab.*`: Queue entry point and download submission controls.
- `src/ui/advanced_settings/`: Advanced Settings pages, including template and binary management.
- `src/ui/widgets/`: Reusable widgets such as custom progress bars and row controls.

## Working Rule of Thumb
- If you need to change behavior, start in `docs/SPEC.md`.
- If you need to find code, use this manifest first, then jump to the relevant `src/` entry point.
- If you add or move major files, update this manifest and `docs/ARCHITECTURE.md` together.
