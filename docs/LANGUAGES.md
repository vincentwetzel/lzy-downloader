# Supported Languages

The application currently ships an English interface. Other languages are
planned but are not selectable until Qt Linguist catalogs and CMake build steps
exist; implementation is tracked in `TODO.md`.

## Current language

- English

## Planned translation targets

Mandarin, Spanish, Hindi, Portuguese, Bengali, Russian, Japanese, Western
Punjabi, Turkish, Vietnamese, Yue Chinese, Egyptian Arabic, Wu Chinese,
Marathi, Telugu, Korean, Tamil, Urdu, Indonesian, German, French, Javanese,
Iranian Persian, Italian, Hausa, Gujarati, Levantine Arabic, and Bhojpuri.

## Translation requirements

- Wrap every user-facing Qt string in `tr()`/`QObject::tr()`.
- Add `.ts` catalogs, compile them to `.qm` through CMake, and load the
  selected catalog before constructing the main window.
- Test translated text at narrow widths; longer strings must not hide download
  actions or binary-management controls.

Developer logs, CI/release documentation, repository metadata, license text,
asset names, API keys, webhook fields, test-runner output, binary source labels,
and checksum manifests are not runtime translations. Existing status, warning,
progress, cancellation, update-handoff, and error strings remain within the
same translatable surface; feature additions must continue using `tr()`.
