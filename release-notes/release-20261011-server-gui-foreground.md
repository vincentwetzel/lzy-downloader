# LzyDownloader 1.2.56

Opening the desktop app during Discord downloads now brings the existing
coordinator window forward on Windows while the shared queue and workers
continue running. The launcher grants the coordinator foreground permission
before forwarding the window request.

This release also includes headless shutdown improvements and refreshed yt-dlp
and gallery-dl extractor catalogs.

GitHub Actions publishes the Windows installer, Linux AppImage, macOS DMGs,
and SHA-256 manifests for the generated version.
