"""
extract_gallerydl_domains.py
────────────────────────────
Builds a comprehensive JSON map of every gallery-dl extractor → its domains.

Domain discovery uses three independent sources:
  1. pattern   – compiled regex on the extractor class (like _VALID_URL in yt-dlp)
  2. root      – plain domain/URL string set on many extractor classes
  3. url / test_url / example – example URL attributes present on some classes

No values are hardcoded.
"""

import gallery_dl.extractor as extractor_pkg
import json
import re
from urllib.parse import urlparse


# ── helpers ───────────────────────────────────────────────────────────────────

def domains_from_pattern(pattern: str) -> set[str]:
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
    # e.g. (?:foo|bar)\.com  →  foo.com, bar.com
    for m in re.finditer(r'\(\?:([^)]+)\)([a-z0-9.\-]+)', pattern, re.I):
        alts = m.group(1).split('|')
        suffix = m.group(2).lstrip('\\')
        for alt in alts:
            candidate = alt.strip('\\') + suffix
            found.update(domains_from_pattern(candidate))

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


# ── gallery-dl extractor sources ──────────────────────────────────────────────

def pattern_string(cls) -> str | None:
    """Return the raw pattern string from a gallery-dl extractor class."""
    pat = getattr(cls, 'pattern', None)
    if pat is None:
        return None
    if hasattr(pat, 'pattern'):   # compiled re.Pattern
        return pat.pattern
    if isinstance(pat, str):
        return pat
    return None


def domains_from_root(cls) -> set[str]:
    """Extract domain from the `root` class attribute if present."""
    found: set[str] = set()
    root = getattr(cls, 'root', None)
    if isinstance(root, str):
        d = domain_from_url_string(root)
        if d:
            found.add(d)
    return found


def domains_from_example_urls(cls) -> set[str]:
    """Extract domains from example/test URL attributes if present."""
    found: set[str] = set()
    for attr in ('url', 'test_url', 'example'):
        val = getattr(cls, attr, None)
        if isinstance(val, str):
            d = domain_from_url_string(val)
            if d:
                found.add(d)
        elif isinstance(val, (list, tuple)):
            for item in val:
                if isinstance(item, str):
                    d = domain_from_url_string(item)
                    if d:
                        found.add(d)
    return found


# ── main ──────────────────────────────────────────────────────────────────────

def get_extractors() -> dict:
    try:
        all_classes = list(extractor_pkg.extractors())
    except AttributeError:
        # Fallback for older gallery-dl versions: iterate modules manually.
        import importlib
        import pkgutil
        all_classes = []
        for _finder, mod_name, _ispkg in pkgutil.iter_modules(extractor_pkg.__path__):
            try:
                mod = importlib.import_module(f'gallery_dl.extractor.{mod_name}')
                for attr in dir(mod):
                    obj = getattr(mod, attr)
                    if (
                            isinstance(obj, type)
                            and hasattr(obj, 'pattern')
                            and hasattr(obj, 'category')
                            and obj.__module__ == mod.__name__
                    ):
                        all_classes.append(obj)
            except Exception:
                continue

    data: dict[str, set] = {}

    for cls in all_classes:
        category: str = getattr(cls, 'category', '') or ''
        name = category.lower().strip()

        if not name or name in {'', 'common', 'test'}:
            continue

        domains: set[str] = set()

        # Source 1: pattern regex
        pat_str = pattern_string(cls)
        if pat_str:
            domains.update(domains_from_pattern(pat_str))

        # Source 2: root attribute
        domains.update(domains_from_root(cls))

        # Source 3: example / test URL attributes
        domains.update(domains_from_example_urls(cls))

        # Merge sub-extractors sharing the same category into one entry
        data.setdefault(name, set()).update(domains)

    return {
        k: {'domains': sorted(v)}
        for k, v in sorted(data.items())
        if v
    }


def main():
    print('Scanning gallery-dl extractors…')
    result = get_extractors()

    out_path = 'extractors_gallery-dl.json'
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(result, f, indent=4, ensure_ascii=False)

    total_domains = sum(len(v['domains']) for v in result.values())
    print(f'Done! {out_path} — {len(result)} extractors, {total_domains} domains.')


if __name__ == '__main__':
    main()