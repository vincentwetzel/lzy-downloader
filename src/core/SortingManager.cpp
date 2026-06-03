#include "SortingManager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDate>
#include <cmath>

SortingManager::SortingManager(ConfigManager *configManager, QObject *parent)
    : QObject(parent), m_configManager(configManager) {
}

namespace {

QString normalizedRuleText(const QString &value) {
    QString normalized = value.trimmed().toLower();
    static const QRegularExpression nonAlphaNumRe(QStringLiteral("[^a-z0-9]+"));
    static const QRegularExpression trimRe(QStringLiteral("^_+|_+$"));
    normalized.replace(nonAlphaNumRe, QStringLiteral("_"));
    normalized.remove(trimRe);
    return normalized;
}

bool isDurationField(const QString &field) {
    const QString normalizedField = normalizedRuleText(field);
    return normalizedField == QStringLiteral("duration") || normalizedField == QStringLiteral("duration_seconds");
}

QString canonicalOperator(const QString &op) {
    const QString normalized = normalizedRuleText(op);
    if (normalized == QStringLiteral("equals")) {
        return QStringLiteral("is");
    }
    if (normalized == QStringLiteral("is_one_of")) {
        return QStringLiteral("is_one_of");
    }
    if (normalized == QStringLiteral("starts_with")) {
        return QStringLiteral("starts_with");
    }
    if (normalized == QStringLiteral("ends_with")) {
        return QStringLiteral("ends_with");
    }
    if (normalized == QStringLiteral("greater_than")) {
        return QStringLiteral("greater_than");
    }
    if (normalized == QStringLiteral("less_than")) {
        return QStringLiteral("less_than");
    }
    if (normalized == QStringLiteral("contains")) {
        return QStringLiteral("contains");
    }
    return normalized;
}

QVariantMap mergedSortingMetadata(const QVariantMap &metadata, const QVariantMap &downloadOptions) {
    QVariantMap combined = downloadOptions;
    for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
        combined.insert(it.key(), it.value());
    }

    // Prevent the internal UUID from leaking into {id} sorting tokens if metadata doesn't have a real ID
    if (combined.contains(QStringLiteral("id")) && !metadata.contains(QStringLiteral("id"))) {
        // We intentionally remove it so parseAndReplaceTokens evaluates it as "Unknown" rather than a UUID
        combined.remove(QStringLiteral("id"));
    }

    QString metaPlaylistTitle = combined.value(QStringLiteral("playlist_title")).toString().trimmed();
    if (metaPlaylistTitle.isEmpty() || metaPlaylistTitle.toLower() == QStringLiteral("null") || metaPlaylistTitle == QStringLiteral("NA")) {
        const QString optionsPlaylistTitle = downloadOptions.value(QStringLiteral("playlist_title")).toString().trimmed();
        if (!optionsPlaylistTitle.isEmpty() && optionsPlaylistTitle.toLower() != QStringLiteral("null") && optionsPlaylistTitle != QStringLiteral("NA")) {
            combined.insert(QStringLiteral("playlist_title"), optionsPlaylistTitle);
        }
    }

    if (!combined.contains(QStringLiteral("is_playlist"))) {
        combined.insert(QStringLiteral("is_playlist"), downloadOptions.value(QStringLiteral("is_playlist"), false).toBool());
    }

    if (!combined.contains(QStringLiteral("playlist_index")) && downloadOptions.contains(QStringLiteral("playlist_index"))) {
        combined.insert(QStringLiteral("playlist_index"), downloadOptions.value(QStringLiteral("playlist_index")));
    }

    return combined;
}

bool hasPlaylistContext(const QVariantMap &metadata, const QVariantMap &downloadOptions) {
    Q_UNUSED(metadata);

    if (downloadOptions.value(QStringLiteral("is_playlist"), false).toBool()) {
        return true;
    }

    const int optionsPlaylistIndex = downloadOptions.value(QStringLiteral("playlist_index"), -1).toInt();
    return optionsPlaylistIndex != -1;
}

}

QString SortingManager::normalizedMetadataKey(const QString &key) const {
    QString normalized = key.trimmed().toLower();
    static const QRegularExpression nonAlphaNumRe(QStringLiteral("[^a-z0-9]+"));
    static const QRegularExpression trimRe(QStringLiteral("^_+|_+$"));
    normalized.replace(nonAlphaNumRe, QStringLiteral("_"));
    normalized.remove(trimRe);
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

    if (normalizedField == QStringLiteral("duration_seconds")) {
        return metadataValueForKey(QStringLiteral("duration"), metadata);
    }

    if (normalizedField == QStringLiteral("playlist_title") || normalizedField == QStringLiteral("playlist")) {
        const QVariant playlistTitle = metadataValueForKey(QStringLiteral("playlist_title"), metadata);
        if (playlistTitle.isValid() && !playlistTitle.toString().trimmed().isEmpty() && 
            playlistTitle.toString().trimmed().toLower() != QStringLiteral("null") && playlistTitle.toString().trimmed() != QStringLiteral("NA")) {
            return playlistTitle;
        }
        const QVariant playlist = metadataValueForKey(QStringLiteral("playlist"), metadata);
        if (playlist.isValid() && !playlist.toString().trimmed().isEmpty() && 
            playlist.toString().trimmed().toLower() != QStringLiteral("null") && playlist.toString().trimmed() != QStringLiteral("NA")) {
            return playlist;
        }
        return QVariant();
    }

    if (normalizedField == QStringLiteral("album")) {
        const QVariant album = metadataValueForKey(QStringLiteral("album"), metadata);
        if (album.isValid() && !album.toString().trimmed().isEmpty() && 
            album.toString().trimmed().toLower() != QStringLiteral("null") && album.toString().trimmed() != QStringLiteral("NA")) {
            return album;
        }

        const QVariant playlistTitle = metadataValueForField(QStringLiteral("playlist_title"), metadata);
        if (playlistTitle.isValid() && !playlistTitle.toString().trimmed().isEmpty() && 
            playlistTitle.toString().trimmed().toLower() != QStringLiteral("null") && playlistTitle.toString().trimmed() != QStringLiteral("NA")) {
            return playlistTitle;
        }
        return QVariant();
    }

    static const QHash<QString, QStringList> aliases = {
        {QStringLiteral("uploader"), {QStringLiteral("uploader"), QStringLiteral("channel"), QStringLiteral("artist"), QStringLiteral("album_artist"), QStringLiteral("creator")}},
        {QStringLiteral("title"), {QStringLiteral("title"), QStringLiteral("track"), QStringLiteral("alt_title")}},
        {QStringLiteral("id"), {QStringLiteral("id"), QStringLiteral("display_id")}}
    };

    const QStringList aliasCandidates = aliases.value(normalizedField, {normalizedField});
    for (const QString &candidate : aliasCandidates) {
        const QVariant value = metadataValueForKey(candidate, metadata);
        if (value.isValid() && !value.toString().trimmed().isEmpty() &&
            value.toString().trimmed().toLower() != QStringLiteral("null") && value.toString().trimmed() != QStringLiteral("NA")) {
            return value;
        }
    }

    return QVariant();
}

QString SortingManager::getSortedDirectory(const QVariantMap &videoMetadata, const QVariantMap &downloadOptions) {
    const QVariantMap sortingMetadata = mergedSortingMetadata(videoMetadata, downloadOptions);

    qDebug() << "SortingManager::getSortedDirectory called.";
    qDebug() << "  videoMetadata keys:" << sortingMetadata.keys();
    if (sortingMetadata.contains(QStringLiteral("uploader"))) {
        qDebug() << "  uploader value:" << sortingMetadata[QStringLiteral("uploader")].toString();
    } else {
        qDebug() << "  uploader key NOT FOUND in metadata!";
    }
    qDebug() << "  downloadOptions type:" << downloadOptions.value(QStringLiteral("type"), QStringLiteral("video")).toString();

    int size = m_configManager->get(QStringLiteral("SortingRules"), QStringLiteral("size"), 0).toInt();
    qDebug() << "  SortingRules size:" << size;
    if (size == 0) {
        // No rules to process, return default directory
        QString baseDir = m_configManager->get(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory")).toString();
        if (baseDir.isEmpty()) {
            baseDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        }
        return baseDir;
    }

    // Rules are stored with flat keys: rule_N_name, rule_N_applies_to, rule_N_target_folder, etc.
    for (int i = 0; i < size; ++i) {
        QString key = QStringLiteral("rule_%1").arg(i);
        
        QString ruleName = m_configManager->get(QStringLiteral("SortingRules"), QStringLiteral("%1_name").arg(key)).toString();
        QVariant appliesToVar = m_configManager->get(QStringLiteral("SortingRules"), QStringLiteral("%1_applies_to").arg(key));
        QString appliesTo = appliesToVar.isValid() ? appliesToVar.toString() : QStringLiteral("All Downloads");
        QString targetFolder = m_configManager->get(QStringLiteral("SortingRules"), QStringLiteral("%1_target_folder").arg(key)).toString();
        QString subfolderPattern = m_configManager->get(QStringLiteral("SortingRules"), QStringLiteral("%1_subfolder_pattern").arg(key)).toString();
        
        // Load conditions
        int condSize = m_configManager->get(QStringLiteral("SortingRules"), QStringLiteral("%1_conditions_size").arg(key), 0).toInt();
        QJsonArray conditionsArray;
        for (int j = 0; j < condSize; ++j) {
            QString condKey = QStringLiteral("%1_condition_%2").arg(key).arg(j);
            QJsonObject cond;
            cond[QStringLiteral("field")] = m_configManager->get(QStringLiteral("SortingRules"), QStringLiteral("%1_field").arg(condKey)).toString();
            cond[QStringLiteral("operator")] = m_configManager->get(QStringLiteral("SortingRules"), QStringLiteral("%1_operator").arg(condKey)).toString();
            cond[QStringLiteral("value")] = m_configManager->get(QStringLiteral("SortingRules"), QStringLiteral("%1_value").arg(condKey)).toString();
            conditionsArray.append(cond);
        }
        
        // Skip invalid rules
        if (ruleName.isEmpty() || targetFolder.isEmpty()) {
            qDebug() << "  Rule" << i << "(" << key << ") is invalid (empty name or target), skipping.";
            continue;
        }
        
        qDebug() << "  Checking rule" << i << "(" << ruleName << "), appliesTo:" << appliesTo << "targetFolder:" << targetFolder << "conditions:" << condSize;

        // 1. Check if the rule applies to this download type
        const QString downloadType = downloadOptions.value(QStringLiteral("type"), QStringLiteral("video")).toString();
        const bool isPlaylist = hasPlaylistContext(sortingMetadata, downloadOptions);

        bool typeMatch = false;
        const QString normalizedAppliesTo = normalizedRuleText(appliesTo);
        if (normalizedAppliesTo == QStringLiteral("any") || normalizedAppliesTo == QStringLiteral("all") || normalizedAppliesTo == QStringLiteral("all_downloads")) {
            typeMatch = true;
        } else if ((normalizedAppliesTo == QStringLiteral("video") || normalizedAppliesTo == QStringLiteral("video_downloads")) && downloadType == QStringLiteral("video") && !isPlaylist) {
            typeMatch = true;
        } else if ((normalizedAppliesTo == QStringLiteral("audio") || normalizedAppliesTo == QStringLiteral("audio_downloads")) && downloadType == QStringLiteral("audio") && !isPlaylist) {
            typeMatch = true;
        } else if ((normalizedAppliesTo == QStringLiteral("gallery") || normalizedAppliesTo == QStringLiteral("gallery_downloads")) && downloadType == QStringLiteral("gallery")) {
            typeMatch = true;
        } else if ((normalizedAppliesTo == QStringLiteral("video_playlist") || normalizedAppliesTo == QStringLiteral("video_playlist_downloads")) && downloadType == QStringLiteral("video") && isPlaylist) {
            typeMatch = true;
        } else if ((normalizedAppliesTo == QStringLiteral("audio_playlist") || normalizedAppliesTo == QStringLiteral("audio_playlist_downloads")) && downloadType == QStringLiteral("audio") && isPlaylist) {
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
            QString field = condition[QStringLiteral("field")].toString();
            QString op = condition[QStringLiteral("operator")].toString();
            QString value = condition[QStringLiteral("value")].toString();

            const QString normalizedOperator = canonicalOperator(op);
            QVariant metadataValue = metadataValueForField(field, sortingMetadata);

            qDebug() << "    Condition" << c << "- field:" << field << "op:" << op;
            qDebug() << "      metadataValue:" << metadataValue.toString() << "(isEmpty:" << metadataValue.toString().isEmpty() << ")";
            qDebug() << "      condition value:" << value.left(100);

            bool match = false;
            if (isDurationField(field)) {
                bool ok;
                const qlonglong durationValue = static_cast<qlonglong>(std::round(metadataValue.toDouble()));
                const qlonglong conditionValue = static_cast<qlonglong>(std::round(value.toDouble(&ok)));
                if (ok) {
                    if (normalizedOperator == QStringLiteral("is")) {
                        match = (durationValue == conditionValue);
                    } else if (normalizedOperator == QStringLiteral("greater_than")) {
                        match = (durationValue > conditionValue);
                    } else if (normalizedOperator == QStringLiteral("less_than")) {
                        match = (durationValue < conditionValue);
                    }
                }
            } else {
                if (normalizedOperator == QStringLiteral("contains")) {
                    match = metadataValue.toString().contains(value, Qt::CaseInsensitive);
                } else if (normalizedOperator == QStringLiteral("is")) {
                    match = metadataValue.toString().compare(value, Qt::CaseInsensitive) == 0;
                } else if (normalizedOperator == QStringLiteral("starts_with")) {
                    match = metadataValue.toString().startsWith(value, Qt::CaseInsensitive);
                } else if (normalizedOperator == QStringLiteral("ends_with")) {
                    match = metadataValue.toString().endsWith(value, Qt::CaseInsensitive);
                } else if (normalizedOperator == QStringLiteral("is_one_of")) {
                    QStringList values = value.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
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
    QString baseDir = m_configManager->get(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory")).toString();
    if (baseDir.isEmpty()) {
        baseDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    }
    qDebug() << "  Default directory:" << baseDir;
    return baseDir;
}

QString SortingManager::parseAndReplaceTokens(const QString &pattern, const QVariantMap &metadata) {
    QString result = pattern;

    // Handle special date tokens first
    QString dateStr = metadataValueForKey(QStringLiteral("release_date"), metadata).toString();
    if (dateStr.isEmpty() || dateStr.length() != 8) {
        dateStr = metadataValueForKey(QStringLiteral("upload_date"), metadata).toString();
    }
    
    if (dateStr.length() == 8) {
        result.replace(QStringLiteral("{upload_year}"), dateStr.left(4), Qt::CaseInsensitive);
        result.replace(QStringLiteral("{upload_month}"), dateStr.mid(4, 2), Qt::CaseInsensitive);
        result.replace(QStringLiteral("{upload_day}"), dateStr.right(2), Qt::CaseInsensitive);
    } else {
        result.replace(QStringLiteral("{upload_year}"), QStringLiteral("Unknown Year"), Qt::CaseInsensitive);
        result.replace(QStringLiteral("{upload_month}"), QStringLiteral("Unknown Month"), Qt::CaseInsensitive);
        result.replace(QStringLiteral("{upload_day}"), QStringLiteral("Unknown Day"), Qt::CaseInsensitive);
    }

    // Use regex to find all {token} patterns
    static const QRegularExpression re(QStringLiteral("\\{([^}]+)\\}"));
    auto it = re.globalMatch(pattern);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString token = match.captured(0); // e.g., "{title}"
        QString key = match.captured(1);   // e.g., "title"

        if (key.compare(QStringLiteral("upload_year"), Qt::CaseInsensitive) == 0 ||
            key.compare(QStringLiteral("upload_month"), Qt::CaseInsensitive) == 0 ||
            key.compare(QStringLiteral("upload_day"), Qt::CaseInsensitive) == 0) {
            continue;
        }

        QVariant value = metadataValueForField(key, metadata);
        if (!value.isValid() || value.toString().trimmed().isEmpty() || 
            value.toString().trimmed().toLower() == QStringLiteral("null") || value.toString().trimmed() == QStringLiteral("NA")) {
            value = metadataValueForKey(key, metadata);
        }

        if (value.isValid() && !value.toString().trimmed().isEmpty() && 
            value.toString().trimmed().toLower() != QStringLiteral("null") && value.toString().trimmed() != QStringLiteral("NA")) {
            result.replace(token, sanitize(value.toString()), Qt::CaseInsensitive);
        } else {
            result.replace(token, QLatin1String("Unknown"), Qt::CaseInsensitive);
        }
    }

    return result;
}

QString SortingManager::sanitize(const QString &name) {
    QString sanitized = name;
    // Remove illegal characters for Windows/Unix paths
    static const QRegularExpression illegalCharsRe(QStringLiteral("[<>:\"/\\\\|?*]"));
    sanitized.replace(illegalCharsRe, QStringLiteral("-"));

    // Collapse multiple spaces into a single space
    static const QRegularExpression multipleSpacesRe(QStringLiteral(" {2,}"));
    sanitized.replace(multipleSpacesRe, QStringLiteral(" "));

    return sanitized.trimmed();
}
