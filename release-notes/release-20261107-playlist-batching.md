# LzyDownloader 1.2.58

This release improves playlist handling and keeps the interface responsive
while large playlists are expanded. Playlist probes use flat extraction,
queue items are added in bounded batches, and remote thumbnails are decoded
off the GUI thread. YouTube Music playlist probes can use configured cookies,
and pending or retried playlist placeholders are re-probed before downloads
start.

The shared transfer-rate limit now applies to active HTTP and HTTPS downloads,
with Current Speed showing measured aggregate throughput. Startup and shutdown
also do less work on the GUI thread, while queue restoration and inactive-item
clearing provide safer, more responsive handling.

The bundled yt-dlp and gallery-dl extractor catalogs are refreshed.

GitHub Actions publishes the Windows installer, Linux AppImage, macOS DMGs,
and SHA-256 manifests for the generated version.
