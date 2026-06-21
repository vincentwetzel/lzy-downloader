#pragma once

#include <QDebug>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtAlgorithms>

/**
 * @struct Version
 * @brief Parses and chronologically compares complex version tags (SemVer or YYYY.MM.DD.HHMMSS).
 *
 * Handles versions like "v2026.05.01", "2026.05.01.123456", "2026.05.01-nightly", "1.2.3".
 * Comparison prioritizes numeric segments, then nightly status (nightly is newer if numeric segments are equal),
 * then lexicographical suffix comparison.
 */
struct Version {
    QVector<int> segments;
    QString suffix;
    bool isNightly = false;
    bool isDate = false;

    static Version parse(const QString& versionStr) {
        Version v;
        QString cleanStr = versionStr.trimmed().toLower();
        
        // Strip leading release tags like "v"
        if (cleanStr.startsWith(QLatin1Char('v'))) {
            cleanStr.remove(0, 1);
        }
        
        // Extract suffix: anything after the last numeric segment
        int lastNumericEnd = -1;
        QRegularExpression numericSegmentRe(QStringLiteral(R"(\d+)"));
        QRegularExpressionMatchIterator it = numericSegmentRe.globalMatch(cleanStr);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            lastNumericEnd = m.capturedEnd();
        }

        if (lastNumericEnd != -1 && lastNumericEnd < cleanStr.length()) {
            // Capture everything from the character after the last numeric part to the end
            v.suffix = cleanStr.mid(lastNumericEnd);
            cleanStr = cleanStr.left(lastNumericEnd);
            // Check if the suffix indicates a nightly/master/git build
            if (v.suffix.contains(QStringLiteral("nightly")) ||
                v.suffix.contains(QStringLiteral("master")) ||
                v.suffix.contains(QStringLiteral("git")) ||
                v.suffix.startsWith(QLatin1Char('.'))) { // For yt-dlp's .HHMMSS format
                v.isNightly = true;
            }
        }

        // Tokenize numeric groups, prioritizing dots, but also handling hyphens for date-like versions
        QStringList parts;
        if (cleanStr.contains(QLatin1Char('.'))) {
            parts = cleanStr.split(QLatin1Char('.'), Qt::SkipEmptyParts);
        } else if (cleanStr.contains(QLatin1Char('-')) && QRegularExpression(QStringLiteral("^\\d{4}-")).match(cleanStr).hasMatch()) { // Looks like YYYY-MM-DD...
            parts = cleanStr.split(QLatin1Char('-'), Qt::SkipEmptyParts);
        } else {
            parts << cleanStr; // Single segment or unhandled format
        }

        for (const QString& part : parts) {
            bool ok;
            int val = part.toInt(&ok);
            if (ok) {
                v.segments.append(val);
            } else {
                if (v.suffix.isEmpty()) {
                    v.suffix = part;
                    if (part.contains(QStringLiteral("nightly")) || part.contains(QStringLiteral("master")) || part.contains(QStringLiteral("git"))) {
                        v.isNightly = true;
                    }
                }
                break;
            }
        }

        if (v.segments.size() >= 3 && v.segments[0] >= 1900 && v.segments[0] <= 2100) {
            v.isDate = true;
        }

        return v;
    }

    bool operator<(const Version& other) const {
        // If one is a legacy date-based build (e.g. YYYY.MM.DD) and the other is semantic (e.g. 8.1.1),
        // we treat the stable semantic version as newer than any legacy date-tagged development build.
        if (isDate != other.isDate) {
            return isDate; // If 'this' is a date, it is older than the semantic 'other'.
        }

        const int maxLen = qMax(segments.size(), other.segments.size());
        for (int i = 0; i < maxLen; ++i) {
            const int selfVal = (i < segments.size()) ? segments[i] : 0;
            const int otherVal = (i < other.segments.size()) ? other.segments[i] : 0;
            if (selfVal < otherVal) return true;
            if (selfVal > otherVal) return false;
        }

        if (isNightly != other.isNightly) {
            return !isNightly && other.isNightly;
        }
        return suffix < other.suffix;
    }

    bool operator>(const Version& other) const { return other < *this; }
    bool operator==(const Version& other) const {
        return segments == other.segments && suffix == other.suffix && isNightly == other.isNightly && isDate == other.isDate;
    }
    bool operator!=(const Version& other) const { return !(*this == other); }
    bool operator<=(const Version& other) const { return !(*this > other); }
    bool operator>=(const Version& other) const { return !(*this < other); }

    QString toString() const {
        QString s = segments.isEmpty() ? QStringLiteral("0") : QString();
        for (int i = 0; i < segments.size(); ++i) {
            s += QString::number(segments[i]);
            if (i < segments.size() - 1) {
                s += QLatin1Char('.');
            }
        }
        if (!suffix.isEmpty()) {
            s += suffix;
        }
        return s;
    }
};