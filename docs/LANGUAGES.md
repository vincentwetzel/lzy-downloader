# Supported Languages

The application currently ships with an English interface. The languages
below are planned translation targets; they are not selectable UI languages
until Qt Linguist catalogs and the corresponding CMake build steps are added.
The implementation work remains tracked in `TODO.md`.

The footer status indicators and exit-after-downloads control remain on the
same translated UI surface; their compact first-row layout does not change
translation coverage.

Release and CI documentation is also maintained in English. The Linux
development packages and prerelease yt-dlp installation used by release CI
are build-time concerns, not translatable runtime UI or bundled dependencies.
The Intel and Apple-Silicon macOS DMG packaging instructions are likewise
English-only build metadata and do not require a translation catalog.
The release workflow's fallback GitHub body and manual validation-run metadata
are also English-only build metadata and do not require a translation catalog.

Repository metadata, contributor/security guidance, license text, release asset
names, and SHA-256 manifests are likewise English-only distribution material;
they are not runtime strings and must not be added to Qt translation catalogs.

The test runner's timestamps, summaries, and suspects-cache diagnostics are
developer-facing English output and are not part of the translated UI.

Power-inhibition diagnostics are likewise developer-facing platform logs; the
feature has no user-visible setting or untranslated UI text.

Duplicate-identity and disk-space diagnostics use existing translated worker
and queue messages; this implementation adds no selectable language or new
translation catalog requirement.

Terminal stopped/failed re-download recovery and non-interactive duplicate
diagnostics also use the existing translated queue/request-failure surface;
they add no selectable language or translation catalog requirement.

Retry/resume duplicate rejection and destination-replacement failures likewise
reuse existing translated queue/finalizer messages and introduce no new
language selection or catalog build step.

## Current language

- English

Thumbnail-remux, quiet-transfer progress-recovery, status, and error messages remain in the existing translation
surface; this artwork behavior change does not add a new selectable language or
change the translation build requirements.

Browser-cookie degraded-format recovery adds a translated worker status message
but does not add a selectable language or change the translation build
requirements.

Aria2c missing-output recovery uses a translated worker status message and
does not add a selectable language or change the translation build
requirements.

Audio-aware transfer status and the video-only quality-warning behavior use the
existing `tr()` translation surface; they do not add a selectable language or
change the translation build requirements. The warning title/source-link
context also uses the existing translation surface.

The corresponding regression test validates behavior without depending on a
particular locale; status text remains a translatable implementation detail.

## Planned translation targets

- Mandarin
- Spanish
- Hindi
- Portuguese
- Bengali
- Russian
- Japanese
- Western Punjabi
- Turkish
- Vietnamese
- Yue Chinese
- Egyptian Arabic
- Wu Chinese
- Marathi
- Telugu
- Korean
- Tamil
- Urdu
- Indonesian
- German
- French
- Javanese
- Iranian Persian
- Italian
- Hausa
- Gujarati
- Levantine Arabic
- Bhojpuri

## Translation requirements

- Keep source strings translatable with Qt's `tr()` mechanism.
- Add `.ts` catalogs and compile them to `.qm` files through CMake.
- Load a selected catalog before constructing the main window.
- Test narrow layouts after translation because longer strings must not hide
  download-row actions or binary-management controls.

## Translation status

The current release documentation and the synchronized playlist-probe,
accurate-cut, recovery-diagnostic, and download-history behavior descriptions
are maintained in English. New user-facing strings must continue to use Qt
translation calls so they can be extracted when `.ts`/`.qm` build automation
is enabled.
# Update handoff terminology

The updater exposes an `installingUpdate` lifecycle signal before installer launch. Any future translations or user-facing update strings should preserve the distinction between a completed download and the final shutdown/install handoff. Silent Windows installs relaunch the installed executable without using translated finish-page text.
Release-tooling diagnostics, including Linux AppImage deployment checks and the `build-release/tooling/` cache, remain English-only CI output and are not runtime translation entries.

The Discord bridge's `/downloads` and `/cancel` command names and local API JSON keys are integration contracts, not selectable Qt UI languages; cancellation status text remains within the existing English bridge/webhook surface.
