# Supported Languages

The application currently ships with an English interface. The languages below
are the planned translation targets; they are not selectable UI languages until
Qt Linguist catalogs and the corresponding CMake build steps are added. The
implementation work remains tracked in `TODO.md`.

- English
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

## Translation requirements

- Keep source strings translatable with Qt's `tr()` mechanism.
- Add `.ts` catalogs and compile them to `.qm` files through CMake.
- Load a selected catalog before constructing the main window.
- Test narrow layouts after translation because longer strings must not hide
  download-row actions or binary-management controls.
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
# Translation status

The current release documentation and the newly synchronized playlist-probe and accurate-cut behavior descriptions are maintained in English. Translation support remains planned; new user-facing strings must continue to use Qt translation calls so they can be extracted when `.ts`/`.qm` build automation is enabled.
