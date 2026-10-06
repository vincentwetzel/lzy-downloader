#include "UrlUtils.h"
#include <QRegularExpression>

namespace UrlUtils {
    QString extractUrl(const QString &text) {
        static const QRegularExpression markdownRegex(
            QStringLiteral(R"URL(\]\((https?://[^\s)]+)\))URL"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression urlRegex(
            QStringLiteral(R"(https?://[^\s<>"']+)"),
            QRegularExpression::CaseInsensitiveOption);

        QRegularExpressionMatch match = markdownRegex.match(text);
        const bool hasMarkdownTarget = match.hasMatch();
        if (!hasMarkdownTarget) {
            match = urlRegex.match(text);
        }
        if (!match.hasMatch()) {
            return text.trimmed();
        }

        QString url = hasMarkdownTarget ? match.captured(1) : match.captured(0);
        const qsizetype markdownBoundary = url.indexOf(QStringLiteral("]("));
        if (markdownBoundary >= 0) {
            url.truncate(markdownBoundary);
        }
        while (!url.isEmpty() && QStringLiteral(".,;!?").contains(url.back())) {
            url.chop(1);
        }

        const auto trimUnmatchedCloser = [&url](QChar closer, QChar opener) {
            while (url.endsWith(closer) && url.count(closer) > url.count(opener)) {
                url.chop(1);
            }
        };
        trimUnmatchedCloser(QLatin1Char(')'), QLatin1Char('('));
        trimUnmatchedCloser(QLatin1Char(']'), QLatin1Char('['));
        trimUnmatchedCloser(QLatin1Char('}'), QLatin1Char('{'));
        return url.isEmpty() ? text.trimmed() : url;
    }
}