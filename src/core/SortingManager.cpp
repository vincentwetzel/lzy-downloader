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
    bool hasSpecial = false;
    for (QChar c : normalized) {
        if (!((c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9'))) {
            hasSpecial = true;
            break;
        }
    }
    if (hasSpecial) {
        static const QRegularExpression nonAlphaNumRe(QStringLiteral("[^a-z0-9]+"));
        normalized.replace(nonAlphaNumRe, QStringLiteral("_"));
    }
    if (normalized.startsWith(u'_') || normalized.endsWith(u'_')) {
        static const QRegularExpression trimRe(QStringLiteral("^_+|_+$"));
        normalized.remove(trimRe);
    }
    return normalized;
}

bool isDurationField(const QString &field) {
    const QString normalizedField = normalizedRuleText(field);
    return normalizedField == QStringLiteral("duration") || normalizedField == QStringLiteral("duration_seconds");
}

QString canonicalOperator(const QString &op) {
    const QString normalized = normalizedRuleText(op);
    if (normalized == u"equals") {
        return QStringLiteral("is");
    }
    if (normalized == u"is_one_of") {
        return QStringLiteral("is_one_of");
    }
    if (normalized == u"starts_with") {
        return QStringLiteral("starts_with");
    }
    if (normalized == u"ends_with") {
        return QStringLiteral("ends_with");
    }
    if (normalized == u"greater_than") {
        return QStringLiteral("greater_than");
    }
    if (normalized == u"less_than") {
        return QStringLiteral("less_than");
    }
    if (normalized == u"contains") {
        return QStringLiteral("contains");
    }
    return normalized;
}

QVariantMap mergedSortingMetadata(const QVariantMap &metadata, const QVariantMap &downloadOptions) {
    QVariantMap combined = downloadOptions;
    combined.insert(metadata);

    // Prevent the internal UUID from leaking into {id} sorting tokens if metadata doesn't have a real ID
    if (combined.contains(QStringLiteral("id")) && !metadata.contains(QStringLiteral("id"))) {
        // We intentionally remove it so parseAndReplaceTokens evaluates it as "Unknown" rather than a UUID
        combined.remove(QStringLiteral("id"));
    }

    QString metaPlaylistTitle = combined.value(QStringLiteral("playlist_title")).toString().trimmed();
    if (metaPlaylistTitle.isEmpty() || metaPlaylistTitle.toLower() == u"null" || metaPlaylistTitle == u"NA") {
        const QString optionsPlaylistTitle = downloadOptions.value(QStringLiteral("playlist_title")).toString().trimmed();
        if (!optionsPlaylistTitle.isEmpty() && optionsPlaylistTitle.toLower() != u"null" && optionsPlaylistTitle != u"NA") {
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

bool isValidMetadataString(const QVariant &val) {
    if (!val.isValid()) return false;
    const QString str = val.toString().trimmed();
    return !str.isEmpty() && str.compare(u"null", Qt::CaseInsensitive) != 0 && str != u"NA";
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
    return normalizedRuleText(key);
}

QVariant SortingManager::metadataValueForKey(const QString &key, const QVariantMap &metadata) const {
    if (key.isEmpty()) {
        return QVariant();
    }

    auto it = metadata.constFind(key);
    if (it != metadata.constEnd()) {
        return it.value();
    }

    const QString normalizedNeedle = normalizedMetadataKey(key);
    for (it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
        if (normalizedMetadataKey(it.key()) == normalizedNeedle) {
            return it.value();
        }
    }

    return QVariant();
}

QVariant SortingManager::metadataValueForField(const QString &field, const QVariantMap &metadata) const {
    const QString normalizedField = normalizedMetadataKey(field);

    if (normalizedField == u"duration_seconds") {
        return metadataValueForKey(QStringLiteral("duration"), metadata);
    }

    if (normalizedField == u"playlist_title" || normalizedField == u"playlist") {
        const QVariant playlistTitle = metadataValueForKey(QStringLiteral("playlist_title"), metadata);
        if (isValidMetadataString(playlistTitle)) {
            return playlistTitle;
        }
        const QVariant playlist = metadataValueForKey(QStringLiteral("playlist"), metadata);
        if (isValidMetadataString(playlist)) {
            return playlist;
        }
        return QVariant();
    }

    if (normalizedField == u"album") {
        const QVariant album = metadataValueForKey(QStringLiteral("album"), metadata);
        if (isValidMetadataString(album)) {
            return album;
        }

        const QVariant playlistTitle = metadataValueForField(QStringLiteral("playlist_title"), metadata);
        if (isValidMetadataString(playlistTitle)) {
            return playlistTitle;
        }
        return QVariant();
    }

    static const QHash<QString, QStringList> aliases = {
        {QStringLiteral("uploader"), {QStringLiteral("uploader"), QStringLiteral("channel"), QStringLiteral("artist"), QStringLiteral("album_artist"), QStringLiteral("creator")}},
        {QStringLiteral("title"), {QStringLiteral("title"), QStringLiteral("track"), QStringLiteral("alt_title")}},
        {QStringLiteral("id"), {QStringLiteral("id"), QStringLiteral("display_id")}}
    };

    auto it = aliases.constFind(normalizedField);
    if (it != aliases.constEnd()) {
        for (const QString &candidate : std::as_const(it.value())) {
            const QVariant value = metadataValueForKey(candidate, metadata);
            if (isValidMetadataString(value)) {
                return value;
            }
        }
    } else {
        const QVariant value = metadataValueForKey(normalizedField, metadata);
        if (isValidMetadataString(value)) {
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
        const QString key = u"rule_" + QString::number(i);
        
        QString ruleName = m_configManager->get(QStringLiteral("SortingRules"), key + u"_name").toString();
        QVariant appliesToVar = m_configManager->get(QStringLiteral("SortingRules"), key + u"_applies_to");
        QString appliesTo = appliesToVar.isValid() ? appliesToVar.toString() : QStringLiteral("All Downloads");
        QString targetFolder = m_configManager->get(QStringLiteral("SortingRules"), key + u"_target_folder").toString();
        
        // Skip invalid rules
        if (ruleName.isEmpty() || targetFolder.isEmpty()) {
            qDebug() << "  Rule" << i << "(" << key << ") is invalid (empty name or target), skipping.";
            continue;
        }
        
        qDebug() << "  Checking rule" << i << "(" << ruleName << "), appliesTo:" << appliesTo << "targetFolder:" << targetFolder;

        // 1. Check if the rule applies to this download type
        const QString downloadType = downloadOptions.value(QStringLiteral("type"), QStringLiteral("video")).toString();
        const bool isPlaylist = hasPlaylistContext(sortingMetadata, downloadOptions);

        bool typeMatch = false;
        const QString normalizedAppliesTo = normalizedRuleText(appliesTo);
        if (normalizedAppliesTo == u"any" || normalizedAppliesTo == u"all" || normalizedAppliesTo == u"all_downloads") {
            typeMatch = true;
        } else if ((normalizedAppliesTo == u"video" || normalizedAppliesTo == u"video_downloads") && downloadType == u"video" && !isPlaylist) {
            typeMatch = true;
        } else if ((normalizedAppliesTo == u"audio" || normalizedAppliesTo == u"audio_downloads") && downloadType == u"audio" && !isPlaylist) {
            typeMatch = true;
        } else if ((normalizedAppliesTo == u"gallery" || normalizedAppliesTo == u"gallery_downloads") && downloadType == u"gallery") {
            typeMatch = true;
        } else if ((normalizedAppliesTo == u"video_playlist" || normalizedAppliesTo == u"video_playlist_downloads") && downloadType == u"video" && isPlaylist) {
            typeMatch = true;
        } else if ((normalizedAppliesTo == u"audio_playlist" || normalizedAppliesTo == u"audio_playlist_downloads") && downloadType == u"audio" && isPlaylist) {
            typeMatch = true;
        }

        if (!typeMatch) {
            continue; // Skip to the next rule
        }

        int condSize = m_configManager->get(QStringLiteral("SortingRules"), key + u"_conditions_size", 0).toInt();
        // 2. Check if all conditions match
        qDebug() << "    Conditions count:" << condSize;
        bool allConditionsMatch = true;
        for (int c = 0; c < condSize; ++c) {
            QString condKey = key + u"_condition_" + QString::number(c);
            QString field = m_configManager->get(QStringLiteral("SortingRules"), condKey + u"_field").toString();
            QString op = m_configManager->get(QStringLiteral("SortingRules"), condKey + u"_operator").toString();
            QString value = m_configManager->get(QStringLiteral("SortingRules"), condKey + u"_value").toString();

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
                    if (normalizedOperator == u"is") {
                        match = (durationValue == conditionValue);
                    } else if (normalizedOperator == u"greater_than") {
                        match = (durationValue > conditionValue);
                    } else if (normalizedOperator == u"less_than") {
                        match = (durationValue < conditionValue);
                    }
                }
            } else {
                if (normalizedOperator == u"contains") {
                    match = metadataValue.toString().contains(value, Qt::CaseInsensitive);
                } else if (normalizedOperator == u"is") {
                    match = metadataValue.toString().compare(value, Qt::CaseInsensitive) == 0;
                } else if (normalizedOperator == u"starts_with") {
                    match = metadataValue.toString().startsWith(value, Qt::CaseInsensitive);
                } else if (normalizedOperator == u"ends_with") {
                    match = metadataValue.toString().endsWith(value, Qt::CaseInsensitive);
                } else if (normalizedOperator == u"is_one_of") {
                    const QStringList values = value.split(u'\n', Qt::SkipEmptyParts);
                    qDebug() << "      Is One Of has" << values.size() << "values. First 5:" << values.mid(0, 5);
                    for (const QString &v : std::as_const(values)) {
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

            QString subfolderPattern = m_configManager->get(QStringLiteral("SortingRules"), key + u"_subfolder_pattern").toString();
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
    if (!pattern.contains(u'{')) {
        return pattern;
    }

    QString result;
    int lastPos = 0;

    // Use regex to find all {token} patterns
    static const QRegularExpression re(QStringLiteral("\\{([^}]+)\\}"));
    auto it = re.globalMatch(pattern);
    
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        
        // Append the literal text before this token
        result += QStringView(pattern).mid(lastPos, match.capturedStart() - lastPos);
        lastPos = match.capturedEnd();
        
        QString key = match.capturedView(1).toString();   // e.g., "title"

        if (key.compare(u"upload_year", Qt::CaseInsensitive) == 0 ||
            key.compare(u"upload_month", Qt::CaseInsensitive) == 0 ||
            key.compare(u"upload_day", Qt::CaseInsensitive) == 0) {
            
            QString dateStr = metadataValueForKey(QStringLiteral("release_date"), metadata).toString();
            if (dateStr.isEmpty() || dateStr.length() != 8) {
                dateStr = metadataValueForKey(QStringLiteral("upload_date"), metadata).toString();
            }
            
            if (dateStr.length() == 8) {
                const QStringView dateView(dateStr);
                if (key.compare(u"upload_year", Qt::CaseInsensitive) == 0) result += dateView.left(4);
                else if (key.compare(u"upload_month", Qt::CaseInsensitive) == 0) result += dateView.mid(4, 2);
                else if (key.compare(u"upload_day", Qt::CaseInsensitive) == 0) result += dateView.right(2);
            } else {
                if (key.compare(u"upload_year", Qt::CaseInsensitive) == 0) result += u"Unknown Year";
                else if (key.compare(u"upload_month", Qt::CaseInsensitive) == 0) result += u"Unknown Month";
                else if (key.compare(u"upload_day", Qt::CaseInsensitive) == 0) result += u"Unknown Day";
            }
            continue;
        }

        QVariant value = metadataValueForField(key, metadata);
        if (!isValidMetadataString(value)) {
            value = metadataValueForKey(key, metadata);
        }

        if (isValidMetadataString(value)) {
            result += sanitize(value.toString());
        } else {
            result += QStringLiteral("Unknown");
        }
    }

    // Append any remaining text after the last token
    result += pattern.mid(lastPos);

    return result;
}

QString SortingManager::sanitize(const QString &name) {
    QString sanitized = name;
    // Remove illegal characters for Windows/Unix paths
    static const QStringView illegalChars = u"<>:\"/\\|?*";
    bool hasIllegal = false;
    for (QChar c : sanitized) {
        if (illegalChars.contains(c)) {
            hasIllegal = true;
            break;
        }
    }
    if (hasIllegal) {
        static const QRegularExpression illegalCharsRe(QStringLiteral("[<>:\"/\\\\|?*]"));
        sanitized.replace(illegalCharsRe, QStringLiteral("-"));
    }

    // Collapse multiple spaces into a single space
    if (sanitized.contains(u"  ")) {
        static const QRegularExpression multipleSpacesRe(QStringLiteral(" {2,}"));
        sanitized.replace(multipleSpacesRe, QStringLiteral(" "));
    }

    return sanitized.trimmed();
}
