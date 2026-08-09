#pragma once

#include <QSet>
#include <QString>

class ConfigManager;

namespace DownloadTempCleanup {

// Resolve the same root used by all download argument builders and workers.
[[nodiscard]] QString resolveRoot(const ConfigManager *configManager);

// Build a per-download path. The caller is responsible for validating the ID
// before treating the result as an owned directory.
[[nodiscard]] QString pathForId(const QString &root, const QString &id);

// Remove an owned UUID directory after checking its exact name and parent.
[[nodiscard]] bool removeOwnedDirectory(const QString &id, const QString &candidatePath);

// Remove an owned directory only when it is empty. This is used before a
// process starts, where no resumable partial data can exist yet.
[[nodiscard]] bool removeEmptyOwnedDirectory(const QString &id, const QString &candidatePath);

// Remove stale application UUID directories while preserving queue entries.
// The shared root and non-UUID child directories are never removed.
[[nodiscard]] int removeOrphanedUuidDirectories(const QString &root, const QSet<QString> &preservedIds);

} // namespace DownloadTempCleanup
