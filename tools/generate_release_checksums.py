"""Write SHA-256 checksums for the release asset produced by one CI job."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


ASSET_PATTERNS = (
    "LzyDownloader-Setup-*.exe",
    "build-release/LzyDownloader-*-x86_64.AppImage",
    "LzyDownloader-*-macos-*.dmg",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def find_assets(root: Path) -> list[Path]:
    assets: set[Path] = set()
    for pattern in ASSET_PATTERNS:
        assets.update(path for path in root.glob(pattern) if path.is_file())
    return sorted(assets)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate a SHA-256 manifest for release assets."
    )
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    assets = find_assets(root)
    if not assets:
        parser.error("no packaged LzyDownloader release asset was found")

    lines = [
        "# SHA-256 checksums for LzyDownloader release assets",
        "# Verify the checksum before installing or launching the downloaded file.",
        "",
    ]
    lines.extend(
        f"{sha256(asset)}  {asset.relative_to(root).as_posix()}" for asset in assets
    )
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
