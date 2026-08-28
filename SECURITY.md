# Security Policy

LzyDownloader launches external downloader and media-processing tools and can
optionally read browser cookies for user-authorized downloads. Please treat
security reports and diagnostic output carefully: never publish cookies, API
tokens, credentials, or private media URLs.

The Chrome companion sends cookies only for the requested HTTP(S) host. The
application rejects out-of-scope domains, unsafe cookie fields, secure cookies
for HTTP URLs, oversized bundles, and paths outside its generated temporary
cookie directory. Generated files are request-scoped, excluded from queue
backups and API snapshots, and removed during cleanup or expiry. The native
host accepts only versioned allowlisted operations and exact extension origins;
wildcard origins are unsupported.

External binaries may be discovered from package-manager directories, including
versioned WinGet payloads. Updates must continue through the package manager or
the tool's own updater; the application must not silently replace an unrelated
external executable or log credentials from an installer command.

Regression tests for binary discovery use isolated temporary directories and
fake executable files; they must not inspect or modify a user's real
`Microsoft\\WinGet\\Packages` tree, settings, credentials, or downloaded media.

## Reporting a vulnerability

Please do not report an undisclosed vulnerability in a public issue. Use
GitHub's private vulnerability reporting feature for this repository when it is
available. If that feature is unavailable, contact the repository maintainer
through GitHub and include a short description, affected version, reproduction
steps, and a safe proof of impact.

Security fixes may be released independently of the normal feature release
schedule. Users should update to the newest release available on the
[LzyDownloader Releases page](https://github.com/vincentwetzel/lzy-downloader/releases).
