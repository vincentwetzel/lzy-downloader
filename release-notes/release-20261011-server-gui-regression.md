# Server-mode GUI activation and shutdown fixes

This release restores the running desktop window when LzyDownloader is started
in server mode by the Discord bridge. The bridge no longer forces the process's
first window show to be hidden, and the app performs a native Windows restore
when activating the existing coordinator. Showing the GUI preserves
`--exit-after`, so the server process can exit after its queue becomes idle.

The release also includes regression coverage for window activation, fake-tool
URL validation, and isolated queue-state backups.
