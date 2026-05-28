"""
extract_yt_dlp_domains.py
─────────────────────────
Builds a comprehensive JSON map of every yt-dlp extractor and its domains.

Domain discovery uses four independent sources (in priority order):
  1. _VALID_URL regex – parsed with a robust multi-pass approach
  2. _TESTS / _TEST – real example URLs embedded in each extractor class
  3. _NETRC_MACHINE – login-hint string often contains the bare domain name
  4. _WORKING_URL_RE – a newer regex pattern used in some extractors

No values are hardcoded.
"""

import yt_dlp
import json
import re
from urllib.parse import urlparse


# ── helpers ───────────────────────────────────────────────────────────────────

def domains_from_valid_url(pattern: str) -> set[str]:
    """
    Extract real domain names from a _VALID_URL regex pattern.

    Strategy:
      • Strip regex metacharacters that corrupt domain detection
      • Find every contiguous run that looks like  word(.word)+
      • Keep only strings whose final segment is a plausible TLD (2-13 chars,
        letters only) and whose parts contain no raw regex syntax
    """
    if not isinstance(pattern, str) or not pattern:
        return set()

    found: set[str] = set()

    # ── pass 1: pull raw candidates with a liberal pattern ───────────────────
    # We deliberately allow optional-group markers so we catch e.g.
    #   (?:www\.)?youtube\.com
    candidates = re.findall(
        r'(?:[a-zA-Z0-9](?:[a-zA-Z0-9\-]*[a-zA-Z0-9])?\.)+[a-zA-Z]{2,13}',
        pattern,
    )

    bad_chars = re.compile(r'[\\^$*+?{}|()[\]<>]')
    # Parts that are pure regex tokens rather than real labels
    bad_labels = re.compile(
        r'^(?:www|s|m|com|net|org|io|tv|co|[0-9]+|'
        r'a-z|0-9|[a-z]|[0-9]|w\+|d\+|s\+)$'
    )

    for raw in candidates:
        # Strip a leading "www." so we normalise to bare domain
        clean = re.sub(r'^www\.', '', raw.lower())
        parts = clean.split('.')
        tld = parts[-1]

        # Reject if any part contains regex metacharacters
        if bad_chars.search(clean):
            continue
        # Reject pure-regex artifacts ('a-z.0-9', etc.)
        if any(bad_labels.match(p) for p in parts[:-1]):
            continue
        # TLD must be letters only, 2-13 chars
        if not re.fullmatch(r'[a-z]{2,13}', tld):
            continue
        # Need at least one non-TLD label
        if len(parts) < 2:
            continue

        found.add(clean)

    # ── pass 2: look for alternation groups like  (?:you|vimeo)tube\.com ─────
    # Expand simple (a|b|c) alternations immediately before a shared suffix
    for m in re.finditer(r'\(\?:([^)]+)\)([a-z0-9.\-]+)', pattern, re.I):
        alts = m.group(1).split('|')
        suffix = m.group(2).lstrip('\\')
        for alt in alts:
            candidate = alt.strip('\\') + suffix
            # Recurse with the assembled string (one level deep is enough)
            found.update(domains_from_valid_url(candidate))

    return found


def domain_from_url_string(url: str) -> str | None:
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


def domains_from_tests(cls) -> set[str]:
    """Pull domains from the real example URLs stored in _TESTS / _TEST."""
    found: set[str] = set()
    tests = getattr(cls, '_TESTS', None) or []
    single = getattr(cls, '_TEST', None)
    if single:
        tests = [single, *tests]

    for test in tests:
        url = test.get('url', '') if isinstance(test, dict) else ''
        domain = domain_from_url_string(url)
        if domain:
            found.add(domain)

    return found


def domains_from_netrc(cls) -> set[str]:
    """
    _NETRC_MACHINE is a string like 'youtube' or 'twitch.tv'.
    When it contains a dot it is directly usable as a domain.
    """
    found: set[str] = set()
    machine = getattr(cls, '_NETRC_MACHINE', None)
    if isinstance(machine, str) and '.' in machine:
        found.add(machine.lower().lstrip('www.'))
    return found


# ── main ──────────────────────────────────────────────────────────────────────

def get_extractors() -> dict:
    extractor_classes = yt_dlp.extractor.gen_extractor_classes()
    data: dict[str, set] = {}

    for cls in extractor_classes:
        ie_name: str = getattr(cls, 'IE_NAME', '') or ''
        name = ie_name.lower()

        # Skip generic / base / search pseudo-extractors
        if not name or name in {'generic', 'base', 'default'}:
            continue
        if name.endswith(':search') or name.endswith('search'):
            continue

        domains: set[str] = set()

        # ── source 1: _VALID_URL ──────────────────────────────────────────────
        valid_url = getattr(cls, '_VALID_URL', None)
        if valid_url and isinstance(valid_url, str):
            domains.update(domains_from_valid_url(valid_url))

        # ── source 2: example URLs from test cases ────────────────────────────
        domains.update(domains_from_tests(cls))

        # ── source 3: _NETRC_MACHINE ──────────────────────────────────────────
        domains.update(domains_from_netrc(cls))

        # ── source 4: _WORKING_URL_RE (newer yt-dlp versions) ────────────────
        working_re = getattr(cls, '_WORKING_URL_RE', None)
        if working_re:
            pattern = working_re.pattern if hasattr(working_re, 'pattern') else str(working_re)
            domains.update(domains_from_valid_url(pattern))

        # Merge sub-extractors (e.g. 'youtube:playlist' → 'youtube')
        base_name = name.split(':')[0]

        if base_name not in data:
            data[base_name] = set()
        data[base_name].update(domains)

    # Convert sets to sorted lists; drop entries with no domains
    result = {
        k: {'domains': sorted(v)}
        for k, v in sorted(data.items())
        if v
    }
    return result


def main():
    print('Scanning yt-dlp extractors…')
    result = get_extractors()

    out_path = 'extractors_yt-dlp.json'
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(result, f, indent=4, ensure_ascii=False)

    total_domains = sum(len(v['domains']) for v in result.values())
    print(f'Done! {out_path} — {len(result)} extractors, {total_domains} domains.')


if __name__ == '__main__':
    main()