"""
extractor_utils.py
──────────────────
Shared utilities for extracting domains from downloader extractor regex patterns
and test URLs.
"""

import re
from urllib.parse import urlparse

def extract_domains_from_pattern(pattern: str) -> set[str]:
    """
    Extract real domain names from a regex pattern string.

    Strategy:
      • Find every contiguous run that looks like  word(.word)+
      • Keep only strings whose final segment is a plausible TLD (2-13 chars,
        letters only) and whose parts contain no raw regex syntax.
    """
    if not isinstance(pattern, str) or not pattern:
        return set()

    found: set[str] = set()

    # ── pass 1: pull raw candidates with a liberal pattern ───────────────────
    candidates = re.findall(
        r'(?:[a-zA-Z0-9](?:[a-zA-Z0-9\-]*[a-zA-Z0-9])?\.)+[a-zA-Z]{2,13}',
        pattern,
    )

    bad_chars = re.compile(r'[\\^$*+?{}|()[\]<>]')
    bad_labels = re.compile(
        r'^(?:www|s|m|com|net|org|io|tv|co|[0-9]+|'
        r'a-z|0-9|[a-z]|[0-9]|w\+|d\+|s\+)$'
    )

    for raw in candidates:
        clean = re.sub(r'^www\.', '', raw.lower())
        parts = clean.split('.')
        tld = parts[-1]

        if bad_chars.search(clean):
            continue
        if any(bad_labels.match(p) for p in parts[:-1]):
            continue
        if not re.fullmatch(r'[a-z]{2,13}', tld):
            continue
        if len(parts) < 2:
            continue

        found.add(clean)

    # ── pass 2: expand simple alternation groups ──────────────────────────────
    for m in re.finditer(r'\(\?:([^)]+)\)([a-z0-9.\-]+)', pattern, re.I):
        alts = m.group(1).split('|')
        suffix = m.group(2).lstrip('\\')
        for alt in alts:
            candidate = alt.strip('\\') + suffix
            found.update(extract_domains_from_pattern(candidate))

    return found

def extract_domain_from_url(url: str) -> str | None:
    """Parse a plain URL or bare domain string into a normalised domain."""
    if not url or not isinstance(url, str):
        return None
    if url.startswith(('http://', 'https://', '//')):
        try:
            host = urlparse(url).hostname or ''
        except Exception:
            return None
    else:
        host = url.split('/')[0]
    host = re.sub(r'^www\.', '', host.lower()).strip()
    if '.' in host and re.search(r'\.[a-z]{2,13}$', host):
        return host
    return None