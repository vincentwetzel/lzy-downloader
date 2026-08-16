#pragma once

#include <QRegularExpression>
#include <QString>
#include <QVariantMap>

namespace YtDlpLiveStatus {

inline bool isExplicitUpcomingDiagnostic(const QString &diagnostic)
{
    static const QRegularExpression upcomingPattern(
        QStringLiteral("premieres in|premiering in|premiere will begin|"
                       "live event will begin|is upcoming|offline \\(expected\\)|"
                       "offline expected|waiting for premiere|waiting for livestream"),
        QRegularExpression::CaseInsensitiveOption);
    return upcomingPattern.match(diagnostic).hasMatch();
}

inline void markUpcoming(QVariantMap *item)
{
    if (!item) {
        return;
    }

    item->insert(QStringLiteral("live_status"), QStringLiteral("is_upcoming"));
    item->insert(QStringLiteral("is_live"), true);
}

}
