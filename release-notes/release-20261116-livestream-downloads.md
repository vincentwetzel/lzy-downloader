# LzyDownloader 1.2.63

**More reliable livestream downloads:** Live format selection now falls back
from separate video/audio streams to a combined stream when needed. Active
livestream subtitle selection skips live chat, which yt-dlp otherwise follows
until the broadcast ends. Active streams also skip the `all` selector to avoid
including live chat; other explicitly selected subtitle languages continue to
download. Progress status identifies both media streams.

The bundled yt-dlp and gallery-dl extractor catalogs are refreshed for this
release.

[View the latest release and installers](https://github.com/vincentwetzel/lzy-downloader/releases/latest)

SHA-256 checksum manifests are published alongside the platform assets.
