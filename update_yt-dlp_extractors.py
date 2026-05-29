"""
update_yt-dlp_extractors.py
───────────────────────────
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
from extractor_utils import extract_domains_from_pattern, extract_domain_from_url


def domains_from_tests(cls) -> set[str]:
    """Pull domains from the real example URLs stored in _TESTS / _TEST."""
    found: set[str] = set()
    tests = getattr(cls, '_TESTS', None) or []
    single = getattr(cls, '_TEST', None)
    if single:
        tests = [single, *tests]

    for test in tests:
        url = test.get('url', '') if isinstance(test, dict) else ''
        domain = extract_domain_from_url(url)
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
            domains.update(extract_domains_from_pattern(valid_url))

        # ── source 2: example URLs from test cases ────────────────────────────
        domains.update(domains_from_tests(cls))

        # ── source 3: _NETRC_MACHINE ──────────────────────────────────────────
        domains.update(domains_from_netrc(cls))

        # ── source 4: _WORKING_URL_RE (newer yt-dlp versions) ────────────────
        working_re = getattr(cls, '_WORKING_URL_RE', None)
        if working_re:
            pattern = working_re.pattern if hasattr(working_re, 'pattern') else str(working_re)
            domains.update(extract_domains_from_pattern(pattern))

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