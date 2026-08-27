# Contributing to LzyDownloader

Thank you for helping improve LzyDownloader, a Qt desktop video, audio,
playlist, and gallery downloader built around yt-dlp and gallery-dl.

## Before opening an issue

- Search existing issues to avoid duplicates.
- Include the application version, operating system, and whether the issue
  occurs in the GUI, Local API, or headless/server mode.
- For download failures, include the relevant sanitized diagnostic and the
  downloader/tool versions. Never include browser cookies, API tokens, or URLs
  containing credentials.
- Do not request hardcoded behavior for one website. Site support should come
  from upstream yt-dlp/gallery-dl, generic URL or metadata handling, or an
  explicit user setting.

## Building and testing

LzyDownloader uses C++20, Qt 6, CMake, and the vcpkg manifest. The README
contains the normal Windows installation and source-build commands.

For Windows debug configuration, use `cmake --preset debug` and the
`build-debug` tree. If compiler metadata is incomplete, use
`cmake --fresh --preset debug` or `tools/configure_debug.ps1`; do not create a
parallel debug build tree.

Before submitting a pull request, run the focused tests relevant to your
change. The full headless test workflow is:

```powershell
python tests/run_headless_tests.py --build-dir build --config Release
```

The helper builds before starting CTest and stops on compilation failure. For
Visual Studio builds it derives vcpkg manifest/MSBuild properties from the
CMake cache, so direct-Qt and vcpkg-toolchain configurations use their
corresponding integration mode. Use `--suspects` to rerun only the tests
recorded as failed by the preceding run.

Keep the GUI responsive, preserve the temporary-download-to-final-file
lifecycle, and update the maintained documentation when behavior changes.
Production code belongs under `src/`; tests and fixtures belong under `tests/`.

## Pull requests

1. Create a focused branch from the default branch.
2. Make the smallest complete change that solves the problem.
3. Add or update regression coverage when behavior changes.
4. Update user-facing docs/changelog and the relevant active documentation.
5. Describe the user-visible behavior and validation performed in the pull
   request.

Please do not commit downloaded binaries, build directories, credentials, or
personal settings.
