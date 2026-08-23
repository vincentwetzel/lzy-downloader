# Supported Languages

The application currently ships with an English interface. The languages
below are planned translation targets; they are not selectable UI languages
until Qt Linguist catalogs and the corresponding CMake build steps are added.
The implementation work remains tracked in `TODO.md`.

## Current language

- English

Thumbnail-remux status and error messages remain in the existing translation
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
change the translation build requirements.

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
