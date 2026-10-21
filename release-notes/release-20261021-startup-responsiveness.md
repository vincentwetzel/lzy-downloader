# LzyDownloader 1.2.57

This release makes desktop startup more responsive while extractor metadata
loads, restores saved queue rows in batches, and moves archive identity
backfill and download history loading off the GUI thread. It also asks for
confirmation before clearing inactive queue entries and makes temporary-file
deletion a separate opt-in action.

Opening the desktop app during Discord downloads now reveals the running
coordinator and its queue without interrupting downloads. Headless shutdown
keeps history current without copying thumbnails, and the yt-dlp and
gallery-dl extractor catalogs are refreshed.

GitHub Actions publishes the Windows installer, Linux AppImage, macOS DMGs,
and SHA-256 manifests for the generated version.
