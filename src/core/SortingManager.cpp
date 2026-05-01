#include "SortingManager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDate>

SortingManager::SortingManager(ConfigManager *configManager, QObject *parent)
    : QObject(parent), m_configManager(configManager) {
}

namespace {

QString normalizedRuleText(const QString &value) {
    QString normalized = value.trimmed().toLower();
    normalized.replace(QRegularExpression("[^a-z0-9]+"), "_");
    normalized.remove(QRegularExpression("^_+|_+$"));
    return normalized;
}

bool isDurationField(const QString &field) {
    const QString normalizedField = normalizedRuleText(field);
    return normalizedField == "duration" || normalizedField == "duration_seconds";
}

QString canonicalOperator(const QString &op) {
    const QString normalized = normalizedRuleText(op);
    if (normalized == "equals") {
        return "is";
    }
    if (normalized == "is_one_of") {
        return "is_one_of";
    }
    if (normalized == "starts_with") {
        return "starts_with";
    }
    if (normalized == "ends_with") {
        return "ends_with";
    }
    if (normalized == "greater_than") {
        return "greater_than";
    }
    if (normalized == "less_than") {
        return "less_than";
    }
    if (normalized == "contains") {
        return "contains";
    }
    return normalized;
}

QVariantMap mergedSortingMetadata(const QVariantMap &metadata, const QVariantMap &downloadOptions) {
    QVariantMap combined = downloadOptions;
    for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
        combined.insert(it.key(), it.value());
    }

    if (!combined.contains("playlist_title") || combined.value("playlist_title").toString().trimmed().isEmpty()) {
        const QString playlistTitle = downloadOptions.value("playlist_title").toString().trimmed();
        if (!playlistTitle.isEmpty()) {
            combined.insert("playlist_title", playlistTitle);
        }
    }

    if (!combined.contains("is_playlist")) {
        combined.insert("is_playlist", downloadOptions.value("is_playlist", false).toBool());
    }

    if (!combined.contains("playlist_index") && downloadOptions.contains("playlist_index")) {
        combined.insert("playlist_index", downloadOptions.value("playlist_index"));
    }

    return combined;
}

bool hasPlaylistContext(const QVariantMap &metadata, const QVariantMap &downloadOptions) {
    Q_UNUSED(metadata);

    if (downloadOptions.value("is_playlist", false).toBool()) {
        return true;
    }

    const int optionsPlaylistIndex = downloadOptions.value("playlist_index", -1).toInt();
    return optionsPlaylistIndex != -1;
}

}

QString SortingManager::normalizedMetadataKey(const QString &key) const {
    QString normalized = key.trimmed().toLower();
    normalized.replace(QRegularExpression("[^a-z0-9]+"), "_");
    normalized.remove(QRegularExpression("^_+|_+$"));
    return normalized;
}

QVariant SortingManager::metadataValueForKey(const QString &key, const QVariantMap &metadata) const {
    if (key.isEmpty()) {
        return QVariant();
    }

    if (metadata.contains(key)) {
        return metadata.value(key);
    }

    const QString normalizedNeedle = normalizedMetadataKey(key);
    for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
        if (normalizedMetadataKey(it.key()) == normalizedNeedle) {
            return it.value();
        }
    }

    return QVariant();
}

QVariant SortingManager::metadataValueForField(const QString &field, const QVariantMap &metadata) const {
    const QString normalizedField = normalizedMetadataKey(field);

    if (normalizedField == "duration_seconds") {
        return metadataValueForKey("duration", metadata);
    }

    if (normalizedField == "playlist_title") {
        const QVariant playlistTitle = metadataValueForKey("playlist_title", metadata);
        if (playlistTitle.isValid() && !playlistTitle.toString().isEmpty()) {
            return playlistTitle;
        }
        return metadataValueForKey("playlist", metadata);
    }

    if (normalizedField == "album") {
        const QVariant album = metadataValueForKey("album", metadata);
        if (album.isValid() && !album.toString().isEmpty()) {
            return album;
        }

        const QVariant playlistTitle = metadataValueForField("playlist_title", metadata);
        if (playlistTitle.isValid() && !playlistTitle.toString().isEmpty()) {
            return playlistTitle;
        }
    }

    static const QHash<QString, QStringList> aliases = {
        {"uploader", {"uploader", "channel", "artist", "album_artist", "creator"}},
        {"title", {"title", "track", "alt_title"}},
        {"id", {"id", "display_id"}}
    };

    const QStringList aliasCandidates = aliases.value(normalizedField, {normalizedField});
    for (const QString &candidate : aliasCandidates) {
        const QVariant value = metadataValueForKey(candidate, metadata);
        if (value.isValid() && !value.toString().isEmpty()) {
            return value;
        }
    }

    return QVariant();
}

QString SortingManager::getSortedDirectory(const QVariantMap &videoMetadata, const QVariantMap &downloadOptions) {
    const QVariantMap sortingMetadata = mergedSortingMetadata(videoMetadata, downloadOptions);

    qDebug() << "SortingManager::getSortedDirectory called.";
    qDebug() << "  videoMetadata keys:" << sortingMetadata.keys();
    if (sortingMetadata.contains("uploader")) {
        qDebug() << "  uploader value:" << sortingMetadata["uploader"].toString();
    } else {
        qDebug() << "  uploader key NOT FOUND in metadata!";
    }
    qDebug() << "  downloadOptions type:" << downloadOptions.value("type", "video").toString();

    int size = m_configManager->get("SortingRules", "size", 0).toInt();
    qDebug() << "  SortingRules size:" << size;
    if (size == 0) {
        // No rules to process, return default directory
        QString baseDir = m_configManager->get("Paths", "completed_downloads_directory").toString();
        if (baseDir.isEmpty()) {
            baseDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        }
        return baseDir;
    }

    // Rules are stored with flat keys: rule_N_name, rule_N_applies_to, rule_N_target_folder, etc.
    for (int i = 0; i < size; ++i) {
        QString key = QString("rule_%1").arg(i);
        
        QString ruleName = m_configManager->get("SortingRules", key + "_name").toString();
        QVariant appliesToVar = m_configManager->get("SortingRules", key + "_applies_to");
        QString appliesTo = appliesToVar.isValid() ? appliesToVar.toString() : "All Downloads";
        QString targetFolder = m_configManager->get("SortingRules", key + "_target_folder").toString();
        QString subfolderPattern = m_configManager->get("SortingRules", key + "_subfolder_pattern").toString();
        
        // Load conditions
        int condSize = m_configManager->get("SortingRules", key + "_conditions_size", 0).toInt();
        QJsonArray conditionsArray;
        for (int j = 0; j < condSize; ++j) {
            QString condKey = key + QString("_condition_%1").arg(j);
            QJsonObject cond;
            cond["field"] = m_configManager->get("SortingRules", condKey + "_field").toString();
            cond["operator"] = m_configManager->get("SortingRules", condKey + "_operator").toString();
            cond["value"] = m_configManager->get("SortingRules", condKey + "_value").toString();
            conditionsArray.append(cond);
        }
        
        // Skip invalid rules
        if (ruleName.isEmpty() || targetFolder.isEmpty()) {
            qDebug() << "  Rule" << i << "(" << key << ") is invalid (empty name or target), skipping.";
            continue;
        }
        
        qDebug() << "  Checking rule" << i << "(" << ruleName << "), appliesTo:" << appliesTo << "targetFolder:" << targetFolder << "conditions:" << condSize;

        // 1. Check if the rule applies to this download type
        QString downloadType = downloadOptions.value("type", "video").toString();
        const bool isPlaylist = hasPlaylistContext(sortingMetadata, downloadOptions);

        bool typeMatch = false;
        const QString normalizedAppliesTo = normalizedRuleText(appliesTo);
        if (normalizedAppliesTo == "any" || normalizedAppliesTo == "all" || normalizedAppliesTo == "all_downloads") {
            typeMatch = true;
        } else if ((normalizedAppliesTo == "video" || normalizedAppliesTo == "video_downloads") && downloadType == "video" && !isPlaylist) {
            typeMatch = true;
        } else if ((normalizedAppliesTo == "audio" || normalizedAppliesTo == "audio_downloads") && downloadType == "audio" && !isPlaylist) {
            typeMatch = true;
        } else if ((normalizedAppliesTo == "gallery" || normalizedAppliesTo == "gallery_downloads") && downloadType == "gallery") {
            typeMatch = true;
        } else if ((normalizedAppliesTo == "video_playlist" || normalizedAppliesTo == "video_playlist_downloads") && downloadType == "video" && isPlaylist) {
            typeMatch = true;
        } else if ((normalizedAppliesTo == "audio_playlist" || normalizedAppliesTo == "audio_playlist_downloads") && downloadType == "audio" && isPlaylist) {
            typeMatch = true;
        }

        if (!typeMatch) {
            continue; // Skip to the next rule
        }

        // 2. Check if all conditions match
        qDebug() << "    Conditions count:" << conditionsArray.size();
        bool allConditionsMatch = true;
        for (int c = 0; c < conditionsArray.size(); ++c) {
            QJsonObject condition = conditionsArray[c].toObject();
            QString field = condition["field"].toString();
            QString op = condition["operator"].toString();
            QString value = condition["value"].toString();

            const QString normalizedOperator = canonicalOperator(op);
            QVariant metadataValue = metadataValueForField(field, sortingMetadata);

            qDebug() << "    Condition" << c << "- field:" << field << "op:" << op;
            qDebug() << "      metadataValue:" << metadataValue.toString() << "(isEmpty:" << metadataValue.toString().isEmpty() << ")";
            qDebug() << "      condition value:" << value.left(100);

            bool match = false;
            if (isDurationField(field)) {
                bool ok;
                const qlonglong durationValue = metadataValue.toLongLong();
                const qlonglong conditionValue = value.toLongLong(&ok);
                if (ok) {
                    if (normalizedOperator == "is") {
                        match = (durationValue == conditionValue);
                    } else if (normalizedOperator == "greater_than") {
                        match = (durationValue > conditionValue);
                    } else if (normalizedOperator == "less_than") {
                        match = (durationValue < conditionValue);
                    }
                }
            } else {
                if (normalizedOperator == "contains") {
                    match = metadataValue.toString().contains(value, Qt::CaseInsensitive);
                } else if (normalizedOperator == "is") {
                    match = metadataValue.toString().compare(value, Qt::CaseInsensitive) == 0;
                } else if (normalizedOperator == "starts_with") {
                    match = metadataValue.toString().startsWith(value, Qt::CaseInsensitive);
                } else if (normalizedOperator == "ends_with") {
                    match = metadataValue.toString().endsWith(value, Qt::CaseInsensitive);
                } else if (normalizedOperator == "is_one_of") {
                    QStringList values = value.split('\n', Qt::SkipEmptyParts);
                    qDebug() << "      Is One Of has" << values.size() << "values. First 5:" << values.mid(0, 5);
                    for (const QString &v : values) {
                        if (metadataValue.toString().compare(v.trimmed(), Qt::CaseInsensitive) == 0) {
                            match = true;
                            qDebug() << "      Is One Of MATCHED on:" << v.trimmed();
                            break;
                        }
                    }
                    if (!match) {
                        qDebug() << "      Is One Of did NOT match any value.";
                    }
                }
            }

            if (!match) {
                allConditionsMatch = false;
                qDebug() << "    Condition" << c << "FAILED (op:" << op << "), skipping rule.";
                break; // One condition failed, no need to check others
            } else {
                qDebug() << "    Condition" << c << "MATCHED.";
            }
        }

        // 3. If all conditions match, construct the directory path
        if (allConditionsMatch) {
            qDebug() << "  Rule" << i << "(" << ruleName << ") MATCHED!";

            QString finalSubfolder;
            if (!subfolderPattern.isEmpty()) {
                finalSubfolder = parseAndReplaceTokens(subfolderPattern, sortingMetadata);
            }

            return QDir(targetFolder).filePath(finalSubfolder);
        }
    }

    // No rule matched, return default directory
    qDebug() << "  No sorting rule matched. Returning default directory.";
    QString baseDir = m_configManager->get("Paths", "completed_downloads_directory").toString();
    if (baseDir.isEmpty()) {
        baseDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    }
    qDebug() << "  Default directory:" << baseDir;
    return baseDir;
}

QString SortingManager::parseAndReplaceTokens(const QString &pattern, const QVariantMap &metadata) {
    QString result = pattern;

    // Handle special date tokens first
    QString dateStr = metadataValueForKey("release_date", metadata).toString();
    if (dateStr.isEmpty() || dateStr.length() != 8) {
        dateStr = metadataValueForKey("upload_date", metadata).toString();
    }
    
    if (dateStr.length() == 8) {
        result.replace("{upload_year}", dateStr.left(4), Qt::CaseInsensitive);
        result.replace("{upload_month}", dateStr.mid(4, 2), Qt::CaseInsensitive);
        result.replace("{upload_day}", dateStr.right(2), Qt::CaseInsensitive);
    } else {
        result.replace("{upload_year}", "Unknown Year", Qt::CaseInsensitive);
        result.replace("{upload_month}", "Unknown Month", Qt::CaseInsensitive);
        result.replace("{upload_day}", "Unknown Day", Qt::CaseInsensitive);
    }

    // Use regex to find all {token} patterns
    QRegularExpression re("\\{([^}]+)\\}");
    auto it = re.globalMatch(pattern);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString token = match.captured(0); // e.g., "{title}"
        QString key = match.captured(1);   // e.g., "title"

        if (key.compare("upload_year", Qt::CaseInsensitive) == 0 ||
            key.compare("upload_month", Qt::CaseInsensitive) == 0 ||
            key.compare("upload_day", Qt::CaseInsensitive) == 0) {
            continue;
        }

        QVariant value = metadataValueForField(key, metadata);
        if (!value.isValid() || value.toString().trimmed().isEmpty()) {
            value = metadataValueForKey(key, metadata);
        }

        if (value.isValid() && !value.toString().trimmed().isEmpty()) {
            result.replace(token, sanitize(value.toString()), Qt::CaseInsensitive);
        } else {
            result.replace(token, "Unknown", Qt::CaseInsensitive);
        }
    }

    return result;
}

QString SortingManager::sanitize(const QString &name) {
    QString sanitized = name;
    // Remove illegal characters for Windows/Unix paths
    sanitized.remove(QRegularExpression("[<>:\"/\\\\|?*]"));
    return sanitized.trimmed();
}
