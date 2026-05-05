# LzyDownloader 1.1.30

## Highlights
- Fixed duplicate rows in the Active Downloads tab when an existing download item is refreshed or replaced.
- Improved Discord bridge webhook reliability by sanitizing long status text, preserving progress during partial queue refreshes, and keeping completion/cancellation states visible to bridge clients.
- Updated the bundled yt-dlp extractor domain list for Nitter support.

## Fixes
- Active Downloads now replaces an existing row with the same internal download ID instead of rendering a duplicate widget.
- Discord webhook payloads flatten multi-line status values and cap long status text before posting to `127.0.0.1:8766/webhook`.
- Completion and cancellation webhooks retain the final tracked state long enough for downstream integrations to observe terminal status.
- Queue refresh events without a progress field keep the previous known progress instead of resetting local bridge clients to `0%`.
- Playlist child webhook payloads now use explicit playlist placeholder IDs for parent mapping.

## Release Checklist
- Version: `1.1.30`
- Tag: `v1.1.30`
- Installer asset: `LzyDownloader-Setup-1.1.30.exe`
