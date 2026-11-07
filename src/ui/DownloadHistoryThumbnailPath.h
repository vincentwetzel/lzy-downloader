#pragma once

#include <QString>

namespace DownloadHistoryThumbnailPath {
inline QString applyCacheResult(const QString &existingPath, const QString &cachedPath)
{
    return cachedPath.isEmpty() ? existingPath : cachedPath;
}
}
