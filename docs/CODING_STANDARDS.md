# Coding Standards

Use this page for implementation, security, and test conventions. Behavioral
requirements live in [`SPEC.md`](SPEC.md), ownership in
[`ARCHITECTURE.md`](ARCHITECTURE.md), and settings in [`SETTINGS.md`](SETTINGS.md).
Read the relevant section only.

## Design and documentation

- Keep `src/ui/` presentation-only and `src/core/` responsible for behavior.
  Remove obsolete code instead of adding compatibility shims unless migration
  is requested.
- Keep production code under `src/`, tests/fixtures/helpers under `tests/`,
  and register Qt tests with `lzy_add_test(...)`.
- Keep every source file under 500 lines and Markdown under 100 KB. Update
  `CMakeLists.txt` for new source files, Qt modules, or dependencies; preserve
  existing build paths and use target-based CMake.
- Use Windows/MSVC-compatible code first and retain Qt 6.2 compatibility.
  Update affected documentation only; do not rewrite
  `docs/CHANGELOG_ARCHIVE.md`.

## C++ and Qt

- Use `PascalCase` classes, `camelCase` functions/locals, `UPPER_SNAKE_CASE`
  constants, and `m_` members. Prefer `const`, RAII, Qt parent ownership,
  smart pointers, `[[nodiscard]]`, `explicit`, and appropriate `noexcept`.
- Keep headers self-contained with `#pragma once`, forward declarations, and
  local -> Qt -> external -> standard-library includes. Use C++20 without
  sacrificing the supported Qt version.
- Use pointer-to-member `connect`, context-bound lambdas, and `deleteLater()`
  for event-driven QObjects. Guard external pointers and never emit while
  holding a mutex. Methods used with queued `invokeMethod` must be
  `Q_INVOKABLE`.
- Use `QStringLiteral`, `QString::arg()`/`std::format`, and `tr()` for UI text.
  Avoid magic values, C-style casts, old `SIGNAL()`/`SLOT()` macros, and
  blocking convenience APIs. Document public/complex APIs with Doxygen.
- Use palette-aware colors, flexible layouts, tooltips, accessible names, and
  descriptions. Do not add a native top-level `QMenuBar`, `QMenu`, or
  `QAction` navigation.

## Threads, processes, and data

- Never block the GUI thread with network, filesystem, database, or process
  work. Use signals/slots, worker threads, `QtConcurrent`, asynchronous
  `QProcess`, or queued calls; keep QWidget access on the GUI thread.
- Protect locks with RAII lockers and use `QPointer` for externally owned
  QObjects. Give child processes bounded watchdogs, UTF-8 byte-line parsing
  (including a final partial line), and process-tree cleanup.
- Start processes with `(program, QStringList)`, never a shell command string.
  Apply the managed Python UTF-8 environment. Build paths with `QDir`,
  `QFileInfo`, and `QStandardPaths`; use `QSaveFile` for critical state.
- Use prepared/bound SQLite queries and one Qt SQL connection per thread.
  Validate JSON types/keys, sanitize metadata before creating paths, and
  preserve resumable media partials during recovery.

## Security and external tools

- Validate external input, prevent traversal/injection, and never log tokens,
  cookies, credentials, or credential-bearing URLs. Keep user data on-device,
  use UUIDs for IDs, and protect `api_token.txt`.
- Keep Local API traffic bearer-authenticated, localhost/origin-checked,
  bounded, and routed through the manager. Use HTTPS and explicit network
  timeouts for remote traffic.
- Preserve explicit binary overrides and resolver ownership tracking. Route
  package-managed updates through their manager; never silently replace a
  standalone external FFmpeg. Keep paired Windows FFmpeg/FFprobe `.exe`
  paths together.
- Keep Qt/OpenSSL/plugin deployment in the shared CMake helper. Release-only
  prerequisites must not become runtime settings or dependencies.

## Tests and handoff

- Add focused regression tests for changed logic using isolated temporary
  paths; never touch real user settings, archives, credentials, or package
  trees. Use `QT_QPA_PLATFORM=offscreen` for headless Qt tests.
- `tests/run_headless_tests.py` builds before CTest, fails on build errors,
  timestamps output, summarizes pass/fail/not-run, and supports `--suspects`.
  For Visual Studio, preserve the cache-selected vcpkg/MSBuild integration.
- Before app updates, save resumable state and stop child processes. Release
  packaging must preserve version/tag checks, tag-only publication, and
  matching SHA-256 manifests. Child commands inherit the invoking terminal.
