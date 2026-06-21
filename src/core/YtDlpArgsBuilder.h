#pragma once

#include <QStringList>
#include <QVariantMap>

class ConfigManager;

class YtDlpArgsBuilder {
public:
    YtDlpArgsBuilder();

    QStringList build(ConfigManager *configManager, const QString &url, const QVariantMap &options);
    QStringList buildValidationArgs(ConfigManager *configManager, const QString &url);

private:
    // Translates UI codec names to yt-dlp format names
    QString getCodecMapping(const QString& codecName) const;

};
