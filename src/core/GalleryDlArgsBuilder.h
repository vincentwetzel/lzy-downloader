#pragma once

#include <QStringList>
#include <QVariantMap>
#include "ConfigManager.h"

class GalleryDlArgsBuilder {
public:
    explicit GalleryDlArgsBuilder(ConfigManager *configManager);

    QStringList build(const QString &url, const QVariantMap &options);

private:
    ConfigManager *m_configManager;
};

