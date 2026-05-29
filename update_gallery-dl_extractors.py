"""
update_gallery-dl_extractors.py
───────────────────────────────
Builds a comprehensive JSON map of every gallery-dl extractor → its domains.

Domain discovery uses three independent sources:
  1. pattern   – compiled regex on the extractor class (like _VALID_URL in yt-dlp)
  2. root      – plain domain/URL string set on many extractor classes
  3. url / test_url / example – example URL attributes present on some classes

No values are hardcoded.
"""

import gallery_dl.extractor as extractor_pkg
import json
from extractor_utils import extract_domains_from_pattern, extract_domain_from_url


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
        d = extract_domain_from_url(root)
        if d:
            found.add(d)
    return found


def domains_from_example_urls(cls) -> set[str]:
    """Extract domains from example/test URL attributes if present."""
    found: set[str] = set()
    for attr in ('url', 'test_url', 'example'):
        val = getattr(cls, attr, None)
        if isinstance(val, str):
            d = extract_domain_from_url(val)
            if d:
                found.add(d)
        elif isinstance(val, (list, tuple)):
            for item in val:
                if isinstance(item, str):
                    d = extract_domain_from_url(item)
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
            domains.update(extract_domains_from_pattern(pat_str))

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