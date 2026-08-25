"""
extractor_utils.py
──────────────────
Shared utilities for extracting domains from downloader extractor regex patterns
and test URLs.
"""

import re
from urllib.parse import urlparse

# Pre-compile regexes for performance
_DOMAIN_PATTERN_RE = re.compile(r'(?:[a-zA-Z0-9](?:[a-zA-Z0-9\-]*[a-zA-Z0-9])?\.)+[a-zA-Z]{2,13}')
_BAD_CHARS_RE = re.compile(r'[\\^$*+?{}|()[\]<>]')
_BAD_LABELS_RE = re.compile(
    r'^(?:www|s|m|com|net|org|io|tv|co|[0-9]+|'
    r'a-z|0-9|[a-z]|[0-9]|w\+|d\+|s\+)$'
)
_ALT_GROUP_RE = re.compile(r'\(\?:([^)]+)\)([a-z0-9.\-]+)', re.I)
_CLEAN_WWW_RE = re.compile(r'^www\.')
_TLD_RE = re.compile(r'[a-z]{2,13}')
_URL_TLD_RE = re.compile(r'\.[a-z]{2,13}$')

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
    candidates = _DOMAIN_PATTERN_RE.findall(pattern)

    for raw in candidates:
        clean = _CLEAN_WWW_RE.sub('', raw.lower())
        parts = clean.split('.')
        tld = parts[-1]

        if _BAD_CHARS_RE.search(clean):
            continue
        if any(_BAD_LABELS_RE.match(p) for p in parts[:-1]):
            continue
        if not _TLD_RE.fullmatch(tld):
            continue
        if len(parts) < 2:
            continue

        found.add(clean)

    # ── pass 2: expand simple alternation groups ──────────────────────────────
    for m in _ALT_GROUP_RE.finditer(pattern):
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
    host = _CLEAN_WWW_RE.sub('', host.lower()).strip()
    if '.' in host and _URL_TLD_RE.search(host):
        return host
    return None