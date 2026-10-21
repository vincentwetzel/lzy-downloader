#pragma once

#include "DownloadHistoryTab.h"

namespace DownloadHistoryStorage {
QList<HistoryItemData> loadHistoryFromPath(const QString &path);
void saveHistoryToPath(const QString &path, const QList<HistoryItemData> &items);
}
