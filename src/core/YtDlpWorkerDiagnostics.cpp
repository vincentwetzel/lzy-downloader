#include "YtDlpWorker.h"

#include <QRegularExpression>

namespace {

const QRegularExpression &fatalDownloadRegex()
{
    static const QRegularExpression regex(
        QStringLiteral("this video is unavailable|"
                       "video unavailable|"
                       "video is private|"
                       "private video|"
                       "this video has been removed|"
                       "this content is no longer available|"
                       "requested tweet is unavailable|"
                       "requested content was removed|"
                       "suspended|"
                       "violating youtube's terms of service|"
                       "not available in your country|"
                       "no video formats found|"
                       "sign in to confirm|"
                       "did not get any data blocks|"
                       "fragment not found|"
                       "error reading header|"
                       "invalid data found when processing input|"
                       "error opening input files"),
        QRegularExpression::CaseInsensitiveOption);
    return regex;
}

template <typename Predicate>
bool anyDiagnosticMatches(const QStringList &lines, Predicate predicate)
{
    for (const QString &line : lines) {
        if (predicate(line)) {
            return true;
        }
    }
    return false;
}

}

bool YtDlpWorker::isIncompleteMediaDiagnosticLine(const QString &line) const
{
    if (line.isEmpty()) {
        return false;
    }

    if (line.contains(QStringLiteral("did not get any data blocks"), Qt::CaseInsensitive)) {
        return line.startsWith(QStringLiteral("ERROR:"), Qt::CaseInsensitive) ||
               line.startsWith(QStringLiteral("[download]"), Qt::CaseInsensitive);
    }
    if (line.contains(QStringLiteral("fragment not found"), Qt::CaseInsensitive)) {
        return line.startsWith(QStringLiteral("[download]"), Qt::CaseInsensitive) ||
               line.startsWith(QStringLiteral("ERROR:"), Qt::CaseInsensitive);
    }
    if (line.contains(QStringLiteral("error reading header"), Qt::CaseInsensitive)) {
        return line.startsWith(QStringLiteral("[in#"), Qt::CaseInsensitive) ||
               line.startsWith(QStringLiteral("ERROR:"), Qt::CaseInsensitive) ||
               line.contains(QStringLiteral("ffmpeg"), Qt::CaseInsensitive);
    }
    if (line.contains(QStringLiteral("invalid data found when processing input"), Qt::CaseInsensitive)) {
        return line.startsWith(QStringLiteral("[in#"), Qt::CaseInsensitive) ||
               line.startsWith(QStringLiteral("ERROR:"), Qt::CaseInsensitive) ||
               line.contains(QStringLiteral("error opening input"), Qt::CaseInsensitive);
    }
    return false;
}

bool YtDlpWorker::hasFatalDownloadDiagnostic() const
{
    const auto matches = [](const QString &line) {
        return fatalDownloadRegex().match(line).hasMatch();
    };
    return anyDiagnosticMatches(m_errorLines, matches) || anyDiagnosticMatches(m_allOutputLines, [this](const QString &line) {
        return isIncompleteMediaDiagnosticLine(line);
    });
}

bool YtDlpWorker::hasIncompleteMediaDiagnostic() const
{
    const auto matches = [this](const QString &line) {
        return isIncompleteMediaDiagnosticLine(line);
    };
    return anyDiagnosticMatches(m_errorLines, matches) || anyDiagnosticMatches(m_allOutputLines, matches);
}
